/* SPDX-License-Identifier: MIT */
#ifndef UART_INTERNAL_H
#define UART_INTERNAL_H

#include <ulmk/microkernel.h>
#include <uart.h>

#define UART_MSG_TX_BYTE	1u
#define UART_MSG_RX_BYTE	2u
#define UART_MSG_RX_BYTE_NB	3u
#define UART_MSG_SET_BAUD	4u

#define UART_NOTIF_IRQ		0u

extern ulmk_ep_t g_uart_eps[UART_MAX];

#endif /* UART_INTERNAL_H */
