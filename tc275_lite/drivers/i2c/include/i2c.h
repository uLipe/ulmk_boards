/* SPDX-License-Identifier: MIT */
#ifndef I2C_H
#define I2C_H

#include <ulmk/microkernel.h>
#include <stdint.h>
#include <stddef.h>

#define I2C_MAX	2u

typedef struct {
	uint8_t scl_port;
	uint8_t scl_pin;
	uint8_t scl_alt;
	uint8_t sda_port;
	uint8_t sda_pin;
	uint8_t sda_alt;
	uint8_t pisel;	/* I2C GPCTL.PISEL (a=0 … h=7) */
} i2c_pins_t;

ulmk_tid_t i2c_init(uint8_t n, const i2c_pins_t *pins, uint32_t bitrate_hz);
int i2c_write(uint8_t n, uint8_t addr7, const uint8_t *data, size_t len);
int i2c_read(uint8_t n, uint8_t addr7, uint8_t *data, size_t len);
int i2c_writeread(uint8_t n, uint8_t addr7,
		  const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen);
/* Address-only write; 0 = ACK, ULMK_ESRCH = NACK, ULMK_ETIMEOUT = bus hang. */
int i2c_probe(uint8_t n, uint8_t addr7);

#endif /* I2C_H */
