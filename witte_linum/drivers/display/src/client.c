/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "display_internal.h"

static void *g_fb_map;

static int display_call(ulmk_msg_t *msg)
{
	int ret;

	if (g_display_ep == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	ret = ulmk_ep_call(g_display_ep, msg);
	if (ret != ULMK_OK)
		return ret;
	return (int)(int32_t)msg->words[0];
}

static void *display_fb_map(void)
{
	if (g_fb_map == NULL) {
		g_fb_map = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_SDRAM_BASE,
					DISPLAY_FB_MAP_SIZE,
					ULMK_PERM_READ | ULMK_PERM_WRITE,
					ULMK_MMAP_SHARED);
	}
	return g_fb_map;
}

uint16_t *display_write(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	ulmk_msg_t msg = {0};
	int ret;

	if (display_fb_map() == NULL)
		return NULL;
	if (x != 0u || y != 0u || w != DISPLAY_W || h != DISPLAY_H)
		return NULL;
	msg.label = DISPLAY_MSG_WRITE;
	msg.words[0] = x;
	msg.words[1] = y;
	msg.words[2] = w;
	msg.words[3] = h;
	ret = display_call(&msg);
	if (ret != ULMK_OK)
		return NULL;
	return (uint16_t *)((uintptr_t)g_fb_map +
			    ((uintptr_t)msg.words[1] -
			     (uintptr_t)ULMK_BOARD_SDRAM_BASE));
}

int display_flip(void)
{
	ulmk_msg_t msg = {0};

	msg.label = DISPLAY_MSG_FLIP;
	return display_call(&msg);
}

int display_on(int on)
{
	ulmk_msg_t msg = {0};

	msg.label = DISPLAY_MSG_ON;
	msg.words[0] = on ? 1u : 0u;
	return display_call(&msg);
}
