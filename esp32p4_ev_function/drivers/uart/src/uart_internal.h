/* SPDX-License-Identifier: MIT */
#ifndef UART_INTERNAL_H
#define UART_INTERNAL_H

#include <ulmk/microkernel.h>

/* ESP32-P4 has UART0..UART4 at a uniform 0x1000 stride. */
#define UART_MAX_INST	5u

enum {
	UART_MSG_TXB = 1u,
	UART_MSG_TXBUF = 2u,
	UART_MSG_RXB = 3u,
	UART_MSG_BAUD = 4u,
};

/* One endpoint per instance; clients index by controller number. */
extern ulmk_ep_t g_uart_eps[UART_MAX_INST];

#endif /* UART_INTERNAL_H */
