/* SPDX-License-Identifier: MIT */
#ifndef UART_H
#define UART_H

#include <ulmk/microkernel.h>
#include <stdint.h>
#include <stddef.h>
#include "board_config.h"

#define UART_MAX	ULMK_BOARD_UART_MAX

typedef struct {
	uint8_t tx_port;
	uint8_t tx_pin;
	uint8_t rx_port;
	uint8_t rx_pin;
	uint8_t af;
} uart_pins_t;

ulmk_tid_t uart_init(uint8_t n, const uart_pins_t *pins,
		     uint32_t baud, uint32_t pclk_hz);

int uart_tx_byte(uint8_t n, uint8_t byte);
int uart_tx_buf(uint8_t n, const uint8_t *buf, size_t len);
int uart_rx_byte(uint8_t n, uint8_t *out);
int uart_rx_byte_nb(uint8_t n, uint8_t *out);
int uart_set_baud(uint8_t n, uint32_t baud);

#endif /* UART_H */
