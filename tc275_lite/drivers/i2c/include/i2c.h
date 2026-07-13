/* SPDX-License-Identifier: MIT */
#ifndef I2C_H
#define I2C_H

#include <ulmk/microkernel.h>
#include <stdint.h>
#include <stddef.h>
#include "board_config.h"

#define I2C_MAX	ULMK_BOARD_I2C_MAX

/* Pins / PISEL come from board_config for instance @p n. */
ulmk_tid_t i2c_init(uint8_t n, uint32_t bitrate_hz);
int i2c_write(uint8_t n, uint8_t addr7, const uint8_t *data, size_t len);
int i2c_read(uint8_t n, uint8_t addr7, uint8_t *data, size_t len);
int i2c_writeread(uint8_t n, uint8_t addr7,
		  const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen);
/* Address-only write; 0 = ACK, ULMK_ESRCH = NACK (blocks until protocol IRQ). */
int i2c_probe(uint8_t n, uint8_t addr7);

#endif /* I2C_H */
