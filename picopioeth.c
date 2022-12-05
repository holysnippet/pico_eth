/*
    RP2040 Ethernet/PIO Firmware
    This software is released under the same license, terms and conditions as the RP2040 "Pico" SDK
    0.1.2-beta - https://github.com/holysnippet/pico_eth/
*/

#include "picopioeth.h"

uint8_t eth_tx_neg_pin, eth_rx_pos_pin;
PIO eth_pio;
const uint8_t ser_sm = 0u, des_sm = 1u, det_sm = 2u;

uint8_t ser_offset, ser_basepin;
uint8_t des_offset, des_basepin;
uint8_t det_offset, det_irq;

int32_t ser_dma_chan, des_dma_chan, copy_dma_chan;
dma_channel_config ser_dma_cfg, des_dma_cfg, copy_dma_cfg;

static uint8_t ser_buffer[TX_BUFFER_SIZE] __attribute__((aligned(4)));
const uint8_t *eth_tx_netif_base = &ser_buffer[8];

absolute_time_t last_tx;

uint8_t des_rx_buf[RX_RING_SIZE] __attribute__((aligned(2 * RX_RING_SIZE)));
uint16_t sizes_tab[RX_NCHUNKS_MAX] __attribute__((aligned(4))), bases_tab[RX_NCHUNKS_MAX] __attribute__((aligned(4)));
uint16_t fifo_read __attribute__((aligned(4))), fifo_start __attribute__((aligned(4))), fifo_stop __attribute__((aligned(4)));
uint32_t ring_len, last_wr_ptr;

eth_pio_diag_t eth_pio_diag __attribute__((aligned(4)));

inline uint16_t eth_rx_siz()
{
    uint16_t result = 0u;
    uint16_t size;
    bool err = false;

    const uint16_t start = fifo_start;
    const uint16_t stop = fifo_stop;

    if (fifo_read != stop)
    {
        if (stop > start)
        {
            if ((fifo_read < start) || (fifo_read >= stop))
                fifo_read = start;
        }
        else if ((fifo_read >= stop) && (fifo_read < start))
            fifo_read = start;

        do
        {
            err = false;
            size = sizes_tab[fifo_read];

            if ((size > (HDR_SIZE + PICO_PIO_ETH_MTU + FCS_SIZE)) || (size < ((HDR_SIZE + FCS_SIZE) - 2)))
            {
                err = true;
                fifo_read = (fifo_read + 1) % RX_NCHUNKS_MAX;
                eth_pio_diag.diag_rx_framing++;
            }

        } while (err && (fifo_read != stop));

        if (!err)
            result = size;
    }

    return result;
}

void dma_set_sniff(int32_t channel, bool reset);

inline bool eth_rx_get(uint8_t *data)
{
    const uint16_t base = bases_tab[fifo_read];
    const uint16_t length = sizes_tab[fifo_read];

    const uint16_t flen = base + length > RX_RING_SIZE ? RX_RING_SIZE - base : length;
    const uint16_t slen = length - flen;

    dma_channel_configure(copy_dma_chan, &copy_dma_cfg, data, &des_rx_buf[base], flen, false);
    dma_set_sniff(copy_dma_chan, true);
    dma_channel_start(copy_dma_chan);
    dma_channel_wait_for_finish_blocking(copy_dma_chan);

    if (slen)
    {
        dma_channel_configure(copy_dma_chan, &copy_dma_cfg, &data[flen], des_rx_buf, slen, false);
        dma_set_sniff(copy_dma_chan, false);
        dma_channel_start(copy_dma_chan);
        dma_channel_wait_for_finish_blocking(copy_dma_chan);
    }

    const bool result = dma_hw->sniff_data == CHCK_CRC32_802_3;

    if (!result)
        eth_pio_diag.diag_rx_badcrc++;

    return result;
}

void eth_rx_next(void)
{
    if (fifo_read != fifo_stop)
        fifo_read = (fifo_read + 1) % RX_NCHUNKS_MAX;
}

void des_setup(void)
{
    pio_sm_claim(eth_pio, des_sm);

    des_offset = pio_add_program(eth_pio, &eth_des_program);
    eth_des_program_init(eth_pio, des_sm, des_offset, eth_rx_pos_pin);

    des_dma_cfg = dma_channel_get_default_config(des_dma_chan);
    channel_config_set_transfer_data_size(&des_dma_cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&des_dma_cfg, false);
    channel_config_set_write_increment(&des_dma_cfg, true);
    channel_config_set_dreq(&des_dma_cfg, pio_get_dreq(eth_pio, des_sm, false));
    channel_config_set_ring(&des_dma_cfg, true, RX_RING_BITS);
    channel_config_set_irq_quiet(&des_dma_cfg, true);

    dma_channel_configure(des_dma_chan, &des_dma_cfg, des_rx_buf, &((uint8_t *)(&eth_pio->rxf[des_sm]))[3], UINT32_MAX, false);

    last_wr_ptr = 0u;

    for (int i = 0; i < RX_NCHUNKS_MAX; i++)
    {
        sizes_tab[i] = 0u;
        bases_tab[i] = 0u;
    }

    fifo_read = 0u;
    ring_len = 0u;
    fifo_start = 0u;
    fifo_stop = 0u;
}

void _det_irq(void)
{
    eth_pio->ctrl |= 1u << (PIO_CTRL_SM_RESTART_LSB + det_sm);

    while (eth_pio->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + des_sm)) == 0)
        tight_loop_contents();

    eth_pio->ctrl |= 1u << (PIO_CTRL_SM_RESTART_LSB + des_sm);

    dma_hw->abort = 1u << des_dma_chan;

    const uint32_t wr_ptr = dma_hw->ch[des_dma_chan].write_addr - (uint32_t)(des_rx_buf);

    dma_hw->ch[des_dma_chan].al1_transfer_count_trig = UINT32_MAX;

    hw_set_bits(&eth_pio->irq, 1u);

    const uint32_t rx_size = wr_ptr >= last_wr_ptr ? wr_ptr - last_wr_ptr : (RX_RING_SIZE - last_wr_ptr) + wr_ptr;

    eth_pio_diag.diag_rx_raw_frames++;
    eth_pio_diag.diag_rx_raw_bytes += rx_size;

    ring_len += rx_size;

    bases_tab[fifo_stop] = last_wr_ptr;
    sizes_tab[fifo_stop] = rx_size;

    fifo_stop = (fifo_stop + 1) % RX_NCHUNKS_MAX;

    if (fifo_stop == fifo_start)
    {
        eth_pio_diag.diag_rx_fifo_overrun++;
        ring_len -= sizes_tab[fifo_start];
        fifo_start = (fifo_start + 1) % RX_NCHUNKS_MAX;
    }

    while (ring_len > RX_RING_SIZE)
    {
        ring_len -= sizes_tab[fifo_start];
        fifo_start = (fifo_start + 1) % RX_NCHUNKS_MAX;
    }

    last_wr_ptr = wr_ptr;
}

void det_setup(void)
{
    pio_sm_claim(eth_pio, det_sm);
    det_offset = pio_add_program(eth_pio, &eth_det_program);
    eth_det_program_init(eth_pio, det_sm, det_offset, eth_rx_pos_pin);

    pio_set_irq0_source_enabled(eth_pio, pis_interrupt0, true);
    irq_set_exclusive_handler(det_irq, _det_irq);
}

void _ser_dma(void)
{
    irq_set_enabled(DMA_IRQ_0, false);
    dma_channel_acknowledge_irq0(ser_dma_chan);
    last_tx = get_absolute_time();
}

void ser_setup(void)
{
    pio_sm_claim(eth_pio, ser_sm);
    ser_offset = pio_add_program(eth_pio, &eth_ser_program);
    eth_ser_program_init(eth_pio, ser_sm, ser_offset, eth_tx_neg_pin);

    ser_dma_cfg = dma_channel_get_default_config(ser_dma_chan);

    channel_config_set_transfer_data_size(&ser_dma_cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&ser_dma_cfg, true);
    channel_config_set_write_increment(&ser_dma_cfg, false);
    channel_config_set_dreq(&ser_dma_cfg, pio_get_dreq(eth_pio, ser_sm, true));

    dma_set_irq0_channel_mask_enabled(1 << ser_dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, _ser_dma);
    irq_set_enabled(DMA_IRQ_0, false);
}

bool eth_hw_send(uint16_t size)
{
    static uint32_t dummy __attribute__((aligned(4)));
    int pktIdx;

    while (eth_tx_busy())
        sleep_us(10);

    for (pktIdx = 0; pktIdx < 7; pktIdx++)
        ser_buffer[pktIdx] = 0x55;
    ser_buffer[pktIdx++] = 0xD5;

    pktIdx += size;

    while (pktIdx < ((MINIMUM_TX_FRAME_LENGTH - FCS_SIZE) + SFD_SIZE))
        ser_buffer[pktIdx++] = 0x00;

    channel_config_set_write_increment(&copy_dma_cfg, false);
    dma_channel_configure(copy_dma_chan, &copy_dma_cfg, &dummy, &ser_buffer[8], pktIdx - 8, false);
    channel_config_set_write_increment(&copy_dma_cfg, true);
    dma_set_sniff(copy_dma_chan, true);
    dma_channel_start(copy_dma_chan);
    dma_channel_wait_for_finish_blocking(copy_dma_chan);

    const uint32_t fcs = dma_hw->sniff_data ^ 0xFFFFFFFF;

    ser_buffer[pktIdx++] = (fcs >> 0) & 0xFF;
    ser_buffer[pktIdx++] = (fcs >> 8) & 0xFF;
    ser_buffer[pktIdx++] = (fcs >> 16) & 0xFF;
    ser_buffer[pktIdx++] = (fcs >> 24) & 0xFF;

    eth_pio_diag.diag_tx_raw_bytes += pktIdx;

    while (absolute_time_diff_us(last_tx, get_absolute_time()) <= 50) // ~ EOT + IPG
        tight_loop_contents();

    irq_set_enabled(DMA_IRQ_0, true);
    dma_channel_configure(ser_dma_chan, &ser_dma_cfg, &((uint8_t *)(&eth_pio->txf[ser_sm]))[3], ser_buffer, pktIdx, true);
    pio_sm_exec(eth_pio, ser_sm, pio_encode_jmp(ser_offset + 0x05));

    return true;
}

inline bool eth_tx_busy()
{
    return irq_is_enabled(DMA_IRQ_0);
}

void eth_sw_task()
{
    const absolute_time_t now = get_absolute_time();

    if (absolute_time_diff_us(last_tx, now) >= 16000)
    {
        if (!eth_tx_busy())
        {
            pio_jump_nlp(eth_pio, ser_sm, ser_offset);
            last_tx = now;
        }
    }
}

bool eth_check_params(PIO pio, uint8_t tx_neg_pin, uint8_t rx_pin)
{
    if ((pio == pio0) || (pio == pio1))
        if ((tx_neg_pin < 28) && (rx_pin < 29))
            if ((rx_pin != tx_neg_pin) && (rx_pin != (tx_neg_pin + 1)))
                return true;
    return false;
}

bool eth_set_params(PIO pio, uint8_t tx_neg_pin, uint8_t rx_pin)
{
    if (!eth_check_params(pio, tx_neg_pin, rx_pin))
        return false;

    gpio_disable_pulls(rx_pin);
    gpio_set_dir(tx_neg_pin, true);
    gpio_set_dir(tx_neg_pin + 1, true);

    eth_pio = pio;
    eth_tx_neg_pin = tx_neg_pin;
    eth_rx_pos_pin = rx_pin;

    if (pio == pio0)
        det_irq = PIO0_IRQ_0;
    else if (pio == pio1)
        det_irq = PIO1_IRQ_0;

    ser_dma_chan = dma_claim_unused_channel(false);
    if (ser_dma_chan == -1)
        return false;
    des_dma_chan = dma_claim_unused_channel(false);
    if (des_dma_chan == -1)
        return false;
    copy_dma_chan = dma_claim_unused_channel(false);
    if (copy_dma_chan == -1)
        return false;

    copy_dma_cfg = dma_channel_get_default_config(copy_dma_chan);
    channel_config_set_transfer_data_size(&copy_dma_cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&copy_dma_cfg, true);
    channel_config_set_write_increment(&copy_dma_cfg, true);

    last_tx = get_absolute_time();

    return true;
}

bool eth_hw_init(PIO pio, uint8_t tx_neg_pin, uint8_t rx_pin)
{
    if (!eth_set_params(pio, tx_neg_pin, rx_pin))
        return false;

    memset(&eth_pio_diag, 0u, sizeof(eth_pio_diag_t));

    ser_setup();
    des_setup();
    det_setup();

    dma_channel_start(des_dma_chan);

    pio_interrupt_clear(eth_pio, 0);
    irq_clear(det_irq);
    irq_set_enabled(det_irq, true);

    pio_sm_set_enabled(eth_pio, ser_sm, true);
    pio_sm_set_enabled(eth_pio, des_sm, true);
    pio_sm_set_enabled(eth_pio, det_sm, true);

    return true;
}

inline void dma_set_sniff(int32_t channel, bool reset)
{
    dma_sniffer_enable(channel, 0x01, true);
    dma_sniffer_set_byte_swap_enabled(true);
    hw_set_bits(&dma_hw->sniff_ctrl, DMA_SNIFF_CTRL_OUT_REV_BITS);
    if (reset)
        dma_hw->sniff_data = 0xffffffff;
}