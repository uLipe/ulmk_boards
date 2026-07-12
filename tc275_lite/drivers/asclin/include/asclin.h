/* SPDX-License-Identifier: MIT */
#ifndef ASCLIN_H
#define ASCLIN_H

#include <ulmk/microkernel.h>
#include <stdint.h>
#include <stddef.h>

#define ASCLIN_MAX	4u

/*
 * Pin description for one ASCLIN instance.
 * port/pin select the PORT pad; rx_alti is ASCLIN.IOCR.ALTI;
 * tx_alt is GPIO alternate (2 = ASCLIN0 TX on P14.0 for the Lite Kit).
 */
typedef struct {
	uint8_t tx_port;
	uint8_t tx_pin;
	uint8_t tx_alt;	/* IfxPort alt 1..7, or 0 = skip pinmux */
	uint8_t rx_port;
	uint8_t rx_pin;
	uint8_t rx_alti;	/* ASCLIN IOCR.ALTI */
} asclin_pins_t;

ulmk_tid_t asclin_init(uint8_t n, const asclin_pins_t *pins,
		       uint32_t baud, uint32_t fa_hz);

int asclin_tx_byte(uint8_t n, uint8_t byte);
int asclin_tx_buf(uint8_t n, const uint8_t *buf, size_t len);
int asclin_rx_byte(uint8_t n, uint8_t *out);	/* blocking poll */
int asclin_rx_byte_nb(uint8_t n, uint8_t *out);	/* non-blocking */
int asclin_set_baud(uint8_t n, uint32_t baud);

#endif /* ASCLIN_H */
