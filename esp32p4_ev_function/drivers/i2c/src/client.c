/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include "i2c.h"
#include "i2c_internal.h"

static int i2c_call(ulmk_msg_t *msg)
{
	if (g_i2c_ep == ULMK_EP_INVALID || g_i2c_ep == 0)
		return ULMK_ESRCH;
	return ulmk_ep_call(g_i2c_ep, msg);
}

int i2c_probe(uint8_t addr7)
{
	ulmk_msg_t msg;
	int rc;

	msg.label = I2C_MSG_PROBE;
	msg.words[0] = addr7;
	msg.words[1] = 0u;
	msg.words[2] = 0u;
	msg.words[3] = 0u;
	rc = i2c_call(&msg);
	return rc != ULMK_OK ? rc : (int)(int32_t)msg.words[0];
}

int i2c_write(uint8_t addr7, const uint8_t *data, size_t len)
{
	ulmk_msg_t msg;
	uint32_t i;
	int rc;

	if (len > I2C_XFER_MAX)
		return ULMK_EINVAL;
	msg.label = I2C_MSG_WRITE;
	msg.words[0] = addr7;
	msg.words[1] = (uint32_t)len;
	msg.words[2] = 0u;
	msg.words[3] = 0u;
	for (i = 0u; i < 4u; i++)
		msg.words[2u + i] = 0u;
	for (i = 0u; i < len && i < 16u; i++)
		msg.words[2u + i / 4u] |=
			((uint32_t)(data ? data[i] : 0u) << ((i % 4u) * 8u));
	rc = i2c_call(&msg);
	return rc != ULMK_OK ? rc : (int)(int32_t)msg.words[0];
}

int i2c_read(uint8_t addr7, uint8_t *data, size_t len)
{
	ulmk_msg_t msg;
	uint32_t i;
	int rc;

	if (!data || len == 0u || len > 12u)
		return ULMK_EINVAL;
	msg.label = I2C_MSG_READ;
	msg.words[0] = addr7;
	msg.words[1] = (uint32_t)len;
	msg.words[2] = 0u;
	msg.words[3] = 0u;
	rc = i2c_call(&msg);
	if (rc != ULMK_OK)
		return rc;
	if ((int)(int32_t)msg.words[0] != ULMK_OK)
		return (int)(int32_t)msg.words[0];
	for (i = 0u; i < len; i++)
		data[i] = (uint8_t)((msg.words[1u + i / 4u] >>
				     ((i % 4u) * 8u)) & 0xFFu);
	return ULMK_OK;
}
