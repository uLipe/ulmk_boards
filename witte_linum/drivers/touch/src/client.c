/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "touch_internal.h"

static int touch_call(ulmk_msg_t *msg)
{
	int ret;

	if (g_touch_ep == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	ret = ulmk_ep_call(g_touch_ep, msg);
	if (ret != ULMK_OK)
		return ret;
	return (int)(int32_t)msg->words[0];
}

int touch_wait(uint8_t n, uint32_t timeout_ms)
{
	ulmk_msg_t msg = {0};

	(void)n;
	msg.label = TOUCH_MSG_WAIT;
	msg.words[0] = timeout_ms;
	return touch_call(&msg);
}

int touch_read_xy(uint8_t n, uint16_t *x, uint16_t *y)
{
	ulmk_msg_t msg = {0};
	int ret;

	(void)n;
	if (!x || !y)
		return ULMK_EINVAL;
	msg.label = TOUCH_MSG_READ_XY;
	ret = touch_call(&msg);
	if (ret == ULMK_OK) {
		*x = (uint16_t)msg.words[1];
		*y = (uint16_t)msg.words[2];
	}
	return ret;
}

int touch_poll(uint8_t n, uint16_t *x, uint16_t *y)
{
	ulmk_msg_t msg = {0};
	int ret;

	(void)n;
	if (!x || !y)
		return ULMK_EINVAL;
	msg.label = TOUCH_MSG_POLL;
	ret = touch_call(&msg);
	if (ret == 1) {
		*x = (uint16_t)msg.words[1];
		*y = (uint16_t)msg.words[2];
	}
	return ret;
}
