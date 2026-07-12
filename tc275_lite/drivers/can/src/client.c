/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include "can_internal.h"

static uint32_t g_can_last_diag;

uint32_t can_last_diag(void)
{
	return g_can_last_diag;
}

int can_send(uint8_t n, const can_frame_t *frame)
{
	ulmk_msg_t msg;
	int rc;

	if (!frame || n >= CAN_MAX || g_can_eps[n] == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	if (frame->dlc > 8u)
		return ULMK_EINVAL;
	msg.label    = CAN_MSG_SEND;
	msg.words[0] = frame->id;
	msg.words[1] = frame->dlc;
	msg.words[2] = (uint32_t)frame->data[0] |
		((uint32_t)frame->data[1] << 8) |
		((uint32_t)frame->data[2] << 16) |
		((uint32_t)frame->data[3] << 24);
	msg.words[3] = (uint32_t)frame->data[4] |
		((uint32_t)frame->data[5] << 8) |
		((uint32_t)frame->data[6] << 16) |
		((uint32_t)frame->data[7] << 24);
	rc = ulmk_ep_call(g_can_eps[n], &msg);
	if (rc != ULMK_OK)
		return rc;
	g_can_last_diag = msg.words[1];
	return (int)(int32_t)msg.words[0];
}

int can_recv(uint8_t n, can_frame_t *frame)
{
	ulmk_msg_t msg;
	int rc;
	uint32_t id_dlc;

	if (!frame || n >= CAN_MAX || g_can_eps[n] == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	msg.label = CAN_MSG_RECV;
	rc = ulmk_ep_call(g_can_eps[n], &msg);
	if (rc != ULMK_OK)
		return rc;
	if ((int)(int32_t)msg.words[0] != ULMK_OK)
		return (int)(int32_t)msg.words[0];
	id_dlc = msg.words[1];
	frame->id  = id_dlc & 0x1FFFu;
	frame->dlc = (uint8_t)((id_dlc >> 16) & 0xFu);
	frame->data[0] = (uint8_t)(msg.words[2] & 0xFFu);
	frame->data[1] = (uint8_t)((msg.words[2] >> 8) & 0xFFu);
	frame->data[2] = (uint8_t)((msg.words[2] >> 16) & 0xFFu);
	frame->data[3] = (uint8_t)((msg.words[2] >> 24) & 0xFFu);
	frame->data[4] = (uint8_t)(msg.words[3] & 0xFFu);
	frame->data[5] = (uint8_t)((msg.words[3] >> 8) & 0xFFu);
	frame->data[6] = (uint8_t)((msg.words[3] >> 16) & 0xFFu);
	frame->data[7] = (uint8_t)((msg.words[3] >> 24) & 0xFFu);
	return ULMK_OK;
}
