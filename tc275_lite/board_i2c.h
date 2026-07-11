/* SPDX-License-Identifier: MIT */
#ifndef BOARD_I2C_H
#define BOARD_I2C_H

#include <ulmk/microkernel.h>
#include <stdint.h>
#include <stddef.h>

ulmk_tid_t board_i2c_start(const ulmk_boot_info_t *info);
int board_i2c_config(uint32_t bitrate_hz);
int board_i2c_write(uint8_t addr7, const uint8_t *buf, size_t len);
int board_i2c_read(uint8_t addr7, uint8_t *buf, size_t len);
int board_i2c_subscribe(ulmk_notif_t n, uint32_t bit);

#endif /* BOARD_I2C_H */
