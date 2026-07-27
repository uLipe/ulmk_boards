/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "can_internal.h"

static int can_call(uint8_t n, ulmk_msg_t *msg)
{
	int ret;

	if (n >= CAN_MAX || g_can_eps[n] == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	ret = ulmk_ep_call(g_can_eps[n], msg);
	if (ret != ULMK_OK)
		return ret;
	return (int)(int32_t)msg->words[0];
}

int can_send(uint8_t n, const can_frame_t *frame)
{
	ulmk_msg_t msg = {0};

	if (!frame || frame->id > 0x7FFu || frame->dlc > 8u)
		return ULMK_EINVAL;
	msg.label = CAN_MSG_SEND;
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
	return can_call(n, &msg);
}

int can_recv(uint8_t n, can_frame_t *frame)
{
	ulmk_msg_t msg = {0};
	int ret;

	if (!frame)
		return ULMK_EINVAL;
	msg.label = CAN_MSG_RECV;
	ret = can_call(n, &msg);
	if (ret != ULMK_OK)
		return ret;
	frame->id = msg.words[1] & 0x7FFu;
	frame->dlc = (uint8_t)(msg.words[1] >> 16);
	frame->data[0] = (uint8_t)msg.words[2];
	frame->data[1] = (uint8_t)(msg.words[2] >> 8);
	frame->data[2] = (uint8_t)(msg.words[2] >> 16);
	frame->data[3] = (uint8_t)(msg.words[2] >> 24);
	frame->data[4] = (uint8_t)msg.words[3];
	frame->data[5] = (uint8_t)(msg.words[3] >> 8);
	frame->data[6] = (uint8_t)(msg.words[3] >> 16);
	frame->data[7] = (uint8_t)(msg.words[3] >> 24);
	return ULMK_OK;
}
