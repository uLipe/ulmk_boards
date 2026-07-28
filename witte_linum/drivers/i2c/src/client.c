/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include <stddef.h>
#include "i2c_internal.h"

static int i2c_call(uint8_t n, ulmk_msg_t *msg)
{
	int ret;

	if (n >= I2C_MAX || g_i2c_eps[n] == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	ret = ulmk_ep_call(g_i2c_eps[n], msg);
	if (ret != ULMK_OK)
		return ret;
	return (int)(int32_t)msg->words[0];
}

static void pack_bytes(ulmk_msg_t *msg, uint32_t w0, const uint8_t *data,
		       size_t len)
{
	size_t i;

	msg->words[w0] = 0u;
	msg->words[w0 + 1u] = 0u;
	msg->words[w0 + 2u] = 0u;
	msg->words[w0 + 3u] = 0u;
	if (!data)
		return;
	for (i = 0u; i < len && i < I2C_XFER_MAX; i++)
		msg->words[w0 + (i / 4u)] |=
			(uint32_t)data[i] << ((i % 4u) * 8u);
}

static void unpack_bytes(const ulmk_msg_t *msg, uint32_t w0, uint8_t *data,
			 size_t len)
{
	size_t i;

	if (!data)
		return;
	for (i = 0u; i < len && i < I2C_XFER_MAX; i++)
		data[i] = (uint8_t)((msg->words[w0 + (i / 4u)] >>
				     ((i % 4u) * 8u)) & 0xFFu);
}

int i2c_write(uint8_t n, uint8_t addr7, const uint8_t *data, size_t len)
{
	ulmk_msg_t msg = {0};

	if (len > I2C_XFER_MAX)
		return ULMK_EINVAL;
	msg.label = I2C_MSG_WRITE;
	msg.words[0] = addr7 & 0x7Fu;
	msg.words[1] = (uint32_t)len;
	pack_bytes(&msg, 2u, data, len);
	return i2c_call(n, &msg);
}

int i2c_read(uint8_t n, uint8_t addr7, uint8_t *data, size_t len)
{
	ulmk_msg_t msg = {0};
	int ret;

	if (!data || len > I2C_XFER_MAX)
		return ULMK_EINVAL;
	msg.label = I2C_MSG_READ;
	msg.words[0] = addr7 & 0x7Fu;
	msg.words[1] = (uint32_t)len;
	ret = i2c_call(n, &msg);
	if (ret == ULMK_OK)
		unpack_bytes(&msg, 2u, data, len);
	return ret;
}

int i2c_writeread(uint8_t n, uint8_t addr7,
		  const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen)
{
	ulmk_msg_t msg = {0};
	int ret;

	if ((!w && wlen) || (!r && rlen) || wlen > 4u || rlen > I2C_XFER_MAX)
		return ULMK_EINVAL;
	msg.label = I2C_MSG_WRITEREAD;
	msg.words[0] = addr7 & 0x7Fu;
	msg.words[1] = ((uint32_t)wlen & 0xFFu) | (((uint32_t)rlen & 0xFFu) << 8);
	pack_bytes(&msg, 2u, w, wlen);
	ret = i2c_call(n, &msg);
	if (ret == ULMK_OK)
		unpack_bytes(&msg, 2u, r, rlen);
	return ret;
}

int i2c_probe(uint8_t n, uint8_t addr7)
{
	ulmk_msg_t msg = {0};

	msg.label = I2C_MSG_PROBE;
	msg.words[0] = addr7 & 0x7Fu;
	return i2c_call(n, &msg);
}
