/* SPDX-License-Identifier: MIT */
/* Thin IPC shims onto the TWAI server; no MMIO here. */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "can.h"
#include "can_internal.h"

int can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
	ulmk_msg_t msg;
	uint8_t i;

	if (g_can_eps[0] == ULMK_EP_INVALID)
		return ULMK_EINVAL;
	if (len > 8u)
		len = 8u;

	msg.label = CAN_MSG_SEND;
	msg.words[0] = id;
	msg.words[1] = len;
	msg.words[2] = 0u;
	msg.words[3] = 0u;
	for (i = 0u; i < len && i < 4u; i++)
		msg.words[2] |= (uint32_t)data[i] << (8u * i);
	for (i = 4u; i < len; i++)
		msg.words[3] |= (uint32_t)data[i] << (8u * (i - 4u));

	if (ulmk_ep_call(g_can_eps[0], &msg) != ULMK_OK)
		return ULMK_EINVAL;
	return (int)msg.words[0];
}

int can_recv(uint32_t *id, uint8_t *data, uint8_t *len)
{
	ulmk_msg_t msg;
	uint8_t n;
	uint8_t i;

	if (g_can_eps[0] == ULMK_EP_INVALID)
		return ULMK_EINVAL;

	msg.label = CAN_MSG_RECV;
	if (ulmk_ep_call(g_can_eps[0], &msg) != ULMK_OK)
		return ULMK_EINVAL;
	if ((int)msg.words[0] != ULMK_OK)
		return (int)msg.words[0];

	n = (uint8_t)msg.words[2];
	if (n > 8u)
		n = 8u;
	if (id)
		*id = msg.words[1];
	if (len)
		*len = n;
	if (data) {
		for (i = 0u; i < n && i < 4u; i++)
			data[i] = (uint8_t)(msg.words[3] >> (8u * i));
		for (i = 4u; i < n; i++)
			data[i] = (uint8_t)(msg.words[4] >> (8u * (i - 4u)));
	}
	return ULMK_OK;
}
