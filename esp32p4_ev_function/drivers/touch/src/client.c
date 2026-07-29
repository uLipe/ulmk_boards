/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "touch.h"
#include "touch_internal.h"

int touch_read(int *x, int *y, int *pressed)
{
	ulmk_msg_t msg;
	int rc;

	if (x)
		*x = 0;
	if (y)
		*y = 0;
	if (pressed)
		*pressed = 0;
	if (g_touch_eps[0] == ULMK_EP_INVALID)
		return ULMK_EINVAL;

	msg.label = TOUCH_MSG_READ;
	rc = ulmk_ep_call(g_touch_eps[0], &msg);
	if (rc != ULMK_OK)
		return rc;
	rc = (int)(int32_t)msg.words[0];
	if (rc != ULMK_OK)
		return rc;
	if (x)
		*x = (int)msg.words[1];
	if (y)
		*y = (int)msg.words[2];
	if (pressed)
		*pressed = (int)msg.words[3];
	return ULMK_OK;
}
