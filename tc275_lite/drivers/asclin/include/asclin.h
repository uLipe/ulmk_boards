/* SPDX-License-Identifier: MIT */
#ifndef ASCLIN_H
#define ASCLIN_H

#include <ulmk/microkernel.h>
#include <stdint.h>
#include <stddef.h>
#include "board_config.h"

#define ASCLIN_MAX	ULMK_BOARD_ASCLIN_MAX

/*
 * Optional pin override.  If NULL, board_config defaults for instance @p n
 * are used (alt function applied via pinmux — not by the app).
 */
typedef struct {
	uint8_t tx_port;
	uint8_t tx_pin;
	uint8_t rx_port;
	uint8_t rx_pin;
	uint8_t rx_alti;	/* ASCLIN IOCR.ALTI */
} asclin_pins_t;

ulmk_tid_t asclin_init(uint8_t n, const asclin_pins_t *pins,
		       uint32_t baud, uint32_t fa_hz);

int asclin_tx_byte(uint8_t n, uint8_t byte);
int asclin_tx_buf(uint8_t n, const uint8_t *buf, size_t len);
int asclin_rx_byte(uint8_t n, uint8_t *out);
int asclin_rx_byte_nb(uint8_t n, uint8_t *out);
int asclin_set_baud(uint8_t n, uint32_t baud);

#endif /* ASCLIN_H */
