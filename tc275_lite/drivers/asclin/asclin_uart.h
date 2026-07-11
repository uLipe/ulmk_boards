/* SPDX-License-Identifier: MIT */
#ifndef ASCLIN_UART_H
#define ASCLIN_UART_H

#include <stdint.h>

void asclin_uart_init(void *base, uint8_t rx_alt, uint8_t tx_alt,
		      uint32_t baud, uint32_t fa_hz);
int asclin_uart_tx_byte(void *base, uint8_t byte);
int asclin_uart_rx_byte(void *base, uint8_t *out);
int asclin_uart_rx_byte_nb(void *base, uint8_t *out);

#endif /* ASCLIN_UART_H */
