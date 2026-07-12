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

static void pack_tx(ulmk_msg_t *msg, const uint8_t *data, size_t len)
{
	size_t i;

	msg->words[2] = 0u;
	msg->words[3] = 0u;
	if (!data || len == 0u)
		return;
	for (i = 0u; i < len && i < 4u; i++)
		msg->words[2] |= (uint32_t)data[i] << (i * 8u);
	for (; i < len && i < 8u; i++)
		msg->words[3] |= (uint32_t)data[i] << ((i - 4u) * 8u);
}

static void unpack_rx(const ulmk_msg_t *msg, uint8_t *data, size_t len)
{
	size_t i;

	if (!data || len == 0u)
		return;
	for (i = 0u; i < len && i < 4u; i++)
		data[i] = (uint8_t)((msg->words[1] >> (i * 8u)) & 0xFFu);
	for (; i < len && i < 8u; i++)
		data[i] = (uint8_t)((msg->words[2] >> ((i - 4u) * 8u)) & 0xFFu);
}

int i2c_write(uint8_t n, uint8_t addr7, const uint8_t *data, size_t len)
{
	ulmk_msg_t msg;
	int rc;

	if (len > 8u)
		return ULMK_EINVAL;
	msg.label = I2C_MSG_WRITE;
	msg.words[0] = addr7 & 0x7Fu;
	msg.words[1] = (uint32_t)len;
	pack_tx(&msg, data, len);
	rc = i2c_call(n, &msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}

int i2c_read(uint8_t n, uint8_t addr7, uint8_t *data, size_t len)
{
	ulmk_msg_t msg;
	int rc;
	int nread;

	if (len > 8u)
		return ULMK_EINVAL;
	msg.label = I2C_MSG_READ;
	msg.words[0] = addr7 & 0x7Fu;
	msg.words[1] = (uint32_t)len;
	msg.words[2] = 0u;
	msg.words[3] = 0u;
	rc = i2c_call(n, &msg);
	if (rc != ULMK_OK)
		return rc;
	nread = (int)(int32_t)msg.words[0];
	if (nread < 0)
		return nread;
	unpack_rx(&msg, data, (size_t)nread);
	return nread;
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

int i2c_probe(uint8_t n, uint8_t addr7)
{
	return i2c_write(n, addr7, NULL, 0u);
}
