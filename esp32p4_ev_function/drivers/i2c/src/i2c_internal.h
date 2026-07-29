/* SPDX-License-Identifier: MIT */
#ifndef I2C_INTERNAL_H
#define I2C_INTERNAL_H

#include <stdint.h>
#include <ulmk/microkernel.h>

#define I2C_MSG_PROBE	1u
#define I2C_MSG_WRITE	2u
#define I2C_MSG_READ	3u

#define I2C_XFER_MAX	32u

extern ulmk_ep_t g_i2c_ep;

#endif /* I2C_INTERNAL_H */
