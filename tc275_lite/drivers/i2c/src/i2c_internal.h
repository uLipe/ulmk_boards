/* SPDX-License-Identifier: MIT */
#ifndef I2C_INTERNAL_H
#define I2C_INTERNAL_H

#include <ulmk/microkernel.h>
#include <i2c.h>

#define I2C_MSG_WRITE		1u
#define I2C_MSG_READ		2u
#define I2C_MSG_WRITEREAD	3u

extern ulmk_ep_t g_i2c_eps[I2C_MAX];

#endif /* I2C_INTERNAL_H */
