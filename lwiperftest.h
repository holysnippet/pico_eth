#ifndef __LWIP_PERF_TEST__
#define __LWIP_PERF_TEST__

#include <stdint.h>
#include <stdio.h>
#include "pico/stdlib.h"

#include "lwip/apps/lwiperf.h"

static void
lwiperf_report(void *arg, enum lwiperf_report_type report_type,
               const ip_addr_t *local_addr, u16_t local_port, const ip_addr_t *remote_addr, u16_t remote_port,
               u32_t bytes_transferred, u32_t ms_duration, u32_t bandwidth_kbitpsec)
{
    LWIP_UNUSED_ARG(arg);
    LWIP_UNUSED_ARG(local_addr);
    LWIP_UNUSED_ARG(local_port);

    printf("IPERF report: type=%d, remote: %s:%d, total bytes: %d, duration in ms: %d, kbits/s: %d\r\n",
           (int)report_type,
           ipaddr_ntoa(remote_addr), (int)remote_port,
           (unsigned)bytes_transferred,
           (unsigned)ms_duration,
           (unsigned)bandwidth_kbitpsec);
}

#endif