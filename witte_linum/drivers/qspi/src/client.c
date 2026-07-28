/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include <stddef.h>
#include "qspi_internal.h"

static uint32_t g_last_cr;

static int qspi_call(ulmk_msg_t *msg)
{
	int ret;

	if (g_qspi_ep == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	ret = ulmk_ep_call(g_qspi_ep, msg);
	if (ret != ULMK_OK)
		return ret;
	return (int)(int32_t)msg->words[0];
}

static void unpack(const ulmk_msg_t *msg, uint8_t *data, size_t len)
{
	size_t i;

	for (i = 0u; i < len && i < 16u; i++)
		data[i] = (uint8_t)((msg->words[2u + (i / 4u)] >>
				     ((i % 4u) * 8u)) & 0xFFu);
}

int qspi_cmd_read(uint8_t n, uint8_t cmd, uint8_t *data, size_t len)
{
	ulmk_msg_t msg = {0};
	int ret;

	(void)n;
	if (!data || len == 0u || len > 16u)
		return ULMK_EINVAL;
	msg.label = QSPI_MSG_CMD_READ;
	msg.words[0] = cmd;
	msg.words[1] = (uint32_t)len;
	ret = qspi_call(&msg);
	if (ret == ULMK_OK)
		unpack(&msg, data, len);
	return ret;
}

int qspi_cmd_read_dbg(uint8_t n, uint8_t cmd, uint8_t *data, size_t len,
		      uint32_t *sr_out)
{
	ulmk_msg_t msg = {0};
	int ret;

	(void)n;
	if (!data || len == 0u || len > 16u)
		return ULMK_EINVAL;
	msg.label = QSPI_MSG_CMD_READ;
	msg.words[0] = cmd;
	msg.words[1] = (uint32_t)len;
	ret = qspi_call(&msg);
	g_last_cr = msg.words[5];
	if (sr_out)
		*sr_out = msg.words[1];
	if (ret == ULMK_OK)
		unpack(&msg, data, len);
	return ret;
}

uint32_t qspi_last_cr(void)
{
	return g_last_cr;
}

int qspi_read(uint8_t n, uint32_t addr, uint8_t *data, size_t len)
{
	ulmk_msg_t msg = {0};
	int ret;

	(void)n;
	if (!data || len == 0u || len > 16u)
		return ULMK_EINVAL;
	msg.label = QSPI_MSG_READ;
	msg.words[0] = addr;
	msg.words[1] = (uint32_t)len;
	ret = qspi_call(&msg);
	if (ret == ULMK_OK)
		unpack(&msg, data, len);
	return ret;
}
