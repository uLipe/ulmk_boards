/* SPDX-License-Identifier: MIT */
#ifndef I2C_H
#define I2C_H

#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>

ulmk_tid_t i2c_init(uint8_t n);
int i2c_probe(uint8_t addr7);
int i2c_write(uint8_t addr7, const uint8_t *data, size_t len);
int i2c_read(uint8_t addr7, uint8_t *data, size_t len);

#endif /* I2C_H */
