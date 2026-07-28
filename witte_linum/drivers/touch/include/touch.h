/* SPDX-License-Identifier: MIT */
#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"

#define TOUCH_MAX	1u

/*
 * FT5446 on I2C3.  Call i2c_init() first.  EXTI/SYSCFG live in the touch
 * server thread so the root does not burn MPU regions.
 */
ulmk_tid_t touch_init(uint8_t n);
int touch_wait(uint8_t n, uint32_t timeout_ms);
int touch_read_xy(uint8_t n, uint16_t *x, uint16_t *y);
/* Non-blocking: 1=pressed (+x/y), 0=released, <0=error. */
int touch_poll(uint8_t n, uint16_t *x, uint16_t *y);

#endif /* TOUCH_H */
