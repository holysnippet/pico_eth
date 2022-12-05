/*
    RP2040 Ethernet/PIO lwIP Driver
    This software is released under the same license, terms and conditions as the RP2040 "Pico" SDK
    0.1.2-beta - https://github.com/holysnippet/pico_eth/
*/

#include "ethpio_arch.h"

static err_t netif_set_opts(struct netif *netif);
int process_frames(const void *frame, int frame_len);
static err_t netif_output(struct netif *netif, struct pbuf *p);

struct netif netif;
ip4_addr_t addr;
ip4_addr_t netmask;
ip4_addr_t gw;
char hostname_str[16];
uint8_t mac_address[6];
absolute_time_t last_to_check;

void eth_pio_arch_poll(void)
{
    eth_sw_task();

    const absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(last_to_check, now) >= LWIP_TO_CHK_PERIOD_US)
    {
        last_to_check = now;
        sys_check_timeouts();
    }

    bool cFlag = true;
    uint16_t siz = eth_rx_siz();

    while (cFlag && siz > 0)
    {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, siz, PBUF_RAM);
        
        if (p != NULL)
        {
            if (eth_rx_get(p->payload))
            {
                if (netif.input(p, &netif) != ERR_OK)
                {
                    pbuf_free(p);
                    cFlag = false;
                }
            }
            else
                pbuf_free(p);
        }
        else
            cFlag = false;

        if (cFlag)
        {
            eth_rx_next();
            siz = eth_rx_siz();
        }
    }
}

bool eth_pio_arch_init(ethpio_parameters_t *params)
{
    if (eth_hw_init((params->pioNum == 0) ? pio0 : pio1, params->tx_neg_pin, params->rx_pin))
    {
        memcpy(mac_address, params->mac_address, 6);
        strcpy(hostname_str, params->hostname);
        lwip_init();
        netif_add(&netif, &addr, &netmask, &gw, NULL, netif_set_opts, netif_input);

        netif.name[0] = 'e';
        netif.name[1] = '0';

        netif_set_default(&netif);
        netif_set_up(&netif);

        if (params->enable_dhcp_client)
            dhcp_start(&netif);

        return true;
    }
    return false;
}

static err_t netif_set_opts(struct netif *netif)
{
    netif->hostname = hostname_str;
    netif->linkoutput = netif_output;
    netif->output = etharp_output;
    netif->mtu = PICO_PIO_ETH_MTU;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP;
    netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, mac_address, 6);

    return ERR_OK;
}

static err_t netif_output(struct netif *netif, struct pbuf *p)
{
    while (eth_tx_busy())
        sleep_us(10);

    pbuf_copy_partial(p, (void *)eth_tx_netif_base, p->tot_len, 0);
    eth_hw_send(p->tot_len);

    return ERR_OK;
}