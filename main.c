/*
    This software is released under the same license, terms and conditions as the RP2040 "Pico" SDK
    Version 0.1.0-beta - https://github.com/holysnippet/pico_eth/ - ROBIN Guillaume

    RP2040 Ethernet/PIO Demo (Polling mode for now)
    -----------------------------------------------

        - Manual IP / DCHP Client (see config)
        - lwIP httpd server demo (on port 80/HTTP)
        - lwIP iPerf 2 (TCP only, no UDP) stack test server (beware, this lwIP app only supports v2)
        - lwIP NTP Client demo (prints the grabbed time on the USB serial port)

        - USB Serial port emulation is enabled (End of CMakeLists.txt)
*/

#include <stdint.h>
#include <stdio.h>

// RP2040 Pico SDK 1.4.0 is required !
#include "pico/stdlib.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/apps/httpd.h"
#include "lwip/apps/mqtt.h"
#include "lwip/apps/mqtt_opts.h"

#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/ip.h"

// Pico W like header
#include "ethpio_arch.h"

// Demo tests headers (taken from pico-examples)
#include "ntptest.h"
#include "lwiperftest.h"

void nework_init(void)
{
    // Wrap the network configuration in a function to free the memory occupied by it at the end of the call
    ethpio_parameters_t config;

    config.pioNum = 0; // Pio, 0 or 1 (should be 0 for now, 1 untested)

    config.rx_pin = 18; // RX pin (RX+ : positive RX ethernet pair side)

    // The two pins of the TX must follow each other ! TX- is ALWAYS first, TX+ next
    config.tx_neg_pin = 16; // TX pin (TX- : negative TX ethernet pair side, 17 will be TX+)

    // Network MAC address (6 bytes) - PLEASE CHANGE IT !
    // Recover MAC and Ethernet transformers from network cards (or motherboards) that you throw away to give them a second life on your Pico-E !
    MAC_ADDR(config.mac_address, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05);

    // Network default values (When DHCP disabled or unavailable)
    IP4_ADDR(&config.default_ip_v4, 192, 168, 1, 110);      // Network IPv4
    IP4_ADDR(&config.default_netmask_v4, 255, 255, 255, 0); // Net mask
    IP4_ADDR(&config.default_gateway_v4, 192, 168, 1, 1);   // Gateway

    // Network host name, you'll probably need some sort of DNS server to see it
    // Not to be confused with Netbios name, Netbios not activated but available (pico_lwip_netbios lib, see Pico SDK)
    strcpy(config.hostname, "lwIP_Pico");

    config.enable_dhcp_client = true; // Enable DHCP client

    eth_pio_arch_init(&config); // Apply, ARP & DHCP take time to set up : network will not be available immediatly
    // You could use dhcp_supplied_address(netif) (see lwIP docs) to determine if you're online
}

int main(void)
{
    // Board init
    set_sys_clock_khz(120000, true);

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    stdio_init_all();
    stdio_usb_init();

    // Network init
    nework_init();

    printf("Hello from lwIp/Pico !\n");

    NTP_T *state = ntp_init();

    lwiperf_start_tcp_server_default(lwiperf_report, NULL);
    httpd_init();

    // Main loop
    while (1)
    {
        // eth_pio_arch_poll() should be called periodically (polling mode)
        eth_pio_arch_poll();

        // NTP Client demo task
        run_ntp_test_task(state);
    }

    return 0u;
}