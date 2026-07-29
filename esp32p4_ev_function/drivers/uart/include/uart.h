/* SPDX-License-Identifier: MIT */
#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>

typedef struct {
	uint8_t tx_gpio;
	uint8_t rx_gpio;
} uart_pins_t;

ulmk_tid_t uart_init(uint8_t n, const uart_pins_t *pins, uint32_t baud,
		     uint32_t pclk_hz);
int uart_tx_byte(uint8_t n, uint8_t b);
int uart_tx_buf(uint8_t n, const uint8_t *buf, size_t len);
int uart_rx_byte(uint8_t n, uint8_t *b);
int uart_set_baud(uint8_t n, uint32_t baud);

#endif /* UART_H */
