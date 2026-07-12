/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include <stddef.h>
#include "i2c_internal.h"

static int i2c_call(uint8_t n, ulmk_msg_t *msg)
{
	if (n >= I2C_MAX || g_i2c_eps[n] == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	return ulmk_ep_call(g_i2c_eps[n], msg);
}

int i2c_write(uint8_t n, uint8_t addr7, const uint8_t *data, size_t len)
{
	ulmk_msg_t msg;
	int rc;

	(void)data;
	msg.label    = I2C_MSG_WRITE;
	msg.words[0] = addr7;
	msg.words[1] = (uint32_t)len;
	msg.words[2] = (len > 0u && data) ? data[0] : 0u;
	rc = i2c_call(n, &msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}

int i2c_read(uint8_t n, uint8_t addr7, uint8_t *data, size_t len)
{
	ulmk_msg_t msg;
	int rc;

	msg.label    = I2C_MSG_READ;
	msg.words[0] = addr7;
	msg.words[1] = (uint32_t)len;
	rc = i2c_call(n, &msg);
	if (rc != ULMK_OK)
		return rc;
	if ((int)(int32_t)msg.words[0] < 0)
		return (int)(int32_t)msg.words[0];
	if (data && len > 0u)
		data[0] = (uint8_t)msg.words[1];
	return (int)(int32_t)msg.words[0];
}

int i2c_writeread(uint8_t n, uint8_t addr7,
		  const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen)
{
	int rc;

	rc = i2c_write(n, addr7, w, wlen);
	if (rc < 0)
		return rc;
	return i2c_read(n, addr7, r, rlen);
}
