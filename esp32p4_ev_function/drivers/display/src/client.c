/* SPDX-License-Identifier: MIT */
/*
 * Display client — thin IPC shims.  No MMIO and no driver state is read
 * here: the framebuffer geometry lives in the server, so it stays reachable
 * once the driver owns its own memory domain.
 */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include "display.h"
#include "display_internal.h"

static int display_call(uint32_t label, uint32_t a0, ulmk_msg_t *msg)
{
	int rc;

	if (g_display_eps[0] == ULMK_EP_INVALID)
		return ULMK_EINVAL;
	msg->label = label;
	msg->words[0] = a0;
	rc = ulmk_ep_call(g_display_eps[0], msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg->words[0];
}

uint16_t *display_write(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	ulmk_msg_t msg;

	(void)x;
	(void)y;
	(void)w;
	(void)h;
	if (display_call(DISPLAY_MSG_WRITE, 0u, &msg) != ULMK_OK)
		return NULL;
	return (uint16_t *)(uintptr_t)msg.words[1];
}

int display_flip(void)
{
	ulmk_msg_t msg;

	return display_call(DISPLAY_MSG_FLIP, 0u, &msg);
}

int display_on(int on)
{
	ulmk_msg_t msg;

	return display_call(DISPLAY_MSG_ON, (uint32_t)(on ? 1 : 0), &msg);
}

uint16_t *display_fb(unsigned idx)
{
	ulmk_msg_t msg;

	if (display_call(DISPLAY_MSG_FB, (uint32_t)idx, &msg) != ULMK_OK)
		return NULL;
	return (uint16_t *)(uintptr_t)msg.words[1];
}

void *display_fb_base(void)
{
	return (void *)display_fb(0u);
}

uint16_t *display_fb_nc(uint16_t *cached)
{
	ulmk_msg_t msg;

	if (display_call(DISPLAY_MSG_FB_NC, (uint32_t)(uintptr_t)cached,
			 &msg) != ULMK_OK)
		return cached;
	return (uint16_t *)(uintptr_t)msg.words[1];
}

static int display_info(ulmk_msg_t *msg)
{
	return display_call(DISPLAY_MSG_INFO, 0u, msg);
}

uint16_t display_width(void)
{
	ulmk_msg_t msg;

	if (display_info(&msg) != ULMK_OK)
		return (uint16_t)ULMK_BOARD_DISPLAY_W;
	return (uint16_t)msg.words[1];
}

uint16_t display_height(void)
{
	ulmk_msg_t msg;

	if (display_info(&msg) != ULMK_OK)
		return (uint16_t)ULMK_BOARD_DISPLAY_H;
	return (uint16_t)msg.words[2];
}

int display_fb_in_psram(void)
{
	ulmk_msg_t msg;

	if (display_info(&msg) != ULMK_OK)
		return 0;
	return (int)msg.words[3];
}

int display_present(const void *fb)
{
	ulmk_msg_t msg;

	(void)fb;
	return display_info(&msg);
}
