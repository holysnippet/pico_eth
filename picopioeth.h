/*
    RP2040 Ethernet/PIO Firmware
    This software is released under the same license, terms and conditions as the RP2040 "Pico" SDK
    0.1.0-beta - https://github.com/holysnippet/pico_eth/
*/

#ifndef __PICO_PIO_ETH__
#define __PICO_PIO_ETH__

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/lock_core.h"
#include "pico/critical_section.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "ethserdes.pio.h"

#define PICO_PIO_ETH_MTU 1500u
#define SFD_SIZE 8u
#define FCS_SIZE 4u
#define HDR_SIZE 14u + 2u
#define MINIMUM_TX_FRAME_LENGTH 64u
#define TX_BUFFER_SIZE (SFD_SIZE + HDR_SIZE + PICO_PIO_ETH_MTU + FCS_SIZE)

#define RX_NCHUNKS_MAX 64u
#define RX_RING_SIZE 16384u
#define RX_RING_BITS 14u

#define POLY_CRC32_802_3 0xEDB88320
#define CHCK_CRC32_802_3 0xDEBB20E3

bool eth_hw_init(PIO pio, uint8_t tx_neg_pin, uint8_t rx_pin);
void eth_sw_task();

bool eth_tx_busy();
extern const uint8_t *eth_tx_netif_base;
bool eth_hw_send(uint16_t size);

uint16_t eth_rx_siz();
bool eth_rx_get(uint8_t *data);
void eth_rx_next(void);

struct eth_pio_diag_struct
{
    uint32_t diag_rx_raw_frames;
    uint32_t diag_rx_raw_bytes;
    uint32_t diag_rx_badcrc;
    uint32_t diag_rx_framing;
    uint32_t diag_rx_ring_overrun;
    uint32_t diag_rx_fifo_overrun;
    
    uint32_t diag_tx_raw_bytes;
};

typedef struct eth_pio_diag_struct eth_pio_diag_t;

extern eth_pio_diag_t eth_pio_diag;

#endif