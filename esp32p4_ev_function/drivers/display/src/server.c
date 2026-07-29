/* SPDX-License-Identifier: MIT */
/*
 * Display — MIPI-DSI EK79007. Dual PSRAM FB; DPI DMA starts on first flip
 * after the client has painted (avoids PSRAM writeback vs scanout underrun).
 */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include "display.h"
#include "display_internal.h"
#include "board_config.h"
#include "board_psram.h"
#include "board_cache.h"
#include "dsi.h"
#include "board_lcd.h"

#include "board_console.h"

#define SOFT_FB_W		160u
#define SOFT_FB_H		120u
#define SOFT_FB_PIXELS		(SOFT_FB_W * SOFT_FB_H)

static uint16_t g_soft0[SOFT_FB_PIXELS];
static uint16_t g_soft1[SOFT_FB_PIXELS];
static uint16_t *g_fb[2];
static unsigned g_front;
static int g_ready;
static int g_psram_fb;
static int g_dpi_on;
static uint32_t g_frame;
static uint16_t g_fb_w = SOFT_FB_W;
static uint16_t g_fb_h = SOFT_FB_H;
static uint32_t g_fb_pixels = SOFT_FB_PIXELS;

ulmk_ep_t g_display_eps[DISPLAY_MAX_INST];

static uint16_t *fb_nc(uint16_t *cached)
{
	void *nc = board_psram_nc(cached);

	return nc ? (uint16_t *)nc : cached;
}

static uint16_t *back_buffer(void)
{
	return g_ready ? g_fb[g_front ^ 1u] : NULL;
}

static int do_flip(void)
{
	uint16_t *front;

	if (!g_ready)
		return ULMK_EINVAL;
	g_front ^= 1u;
	g_frame++;
	front = g_fb[g_front];
	if (g_psram_fb) {
		if (!g_dpi_on) {
			if (dsi_fb_start(front,
					 (uint32_t)ULMK_BOARD_DISPLAY_FB_BYTES) != 0)
				board_console_printf("ulmk: dsi fb start fail\n");
			else
				g_dpi_on = 1;
		} else if (dsi_fb_ready()) {
			(void)dsi_fb_set(front);
		}
	}
	return ULMK_OK;
}

static void display_server(void *arg)
{
	ulmk_ep_t ep = g_display_eps[0];
	ulmk_msg_t msg, reply;
	ulmk_tid_t sender;

	(void)arg;
	for (;;) {
		if (ulmk_ep_recv(ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		reply.words[1] = 0u;
		switch (msg.label) {
		case DISPLAY_MSG_WRITE:
			if (g_ready) {
				reply.words[0] = (uint32_t)ULMK_OK;
				reply.words[1] = (uint32_t)(uintptr_t)
					back_buffer();
			}
			break;
		case DISPLAY_MSG_FLIP:
			reply.words[0] = (uint32_t)do_flip();
			break;
		case DISPLAY_MSG_ON:
			if (g_ready) {
				board_lcd_backlight_gpio(msg.words[0] ? 1 : 0);
				reply.words[0] = (uint32_t)ULMK_OK;
			}
			break;
		case DISPLAY_MSG_FB:
			if (g_ready && msg.words[0] <= 1u) {
				reply.words[0] = (uint32_t)ULMK_OK;
				reply.words[1] = (uint32_t)(uintptr_t)
					g_fb[msg.words[0]];
			}
			break;
		case DISPLAY_MSG_FB_NC:
			reply.words[0] = (uint32_t)ULMK_OK;
			reply.words[1] = (uint32_t)(uintptr_t)
				fb_nc((uint16_t *)(uintptr_t)msg.words[0]);
			break;
		case DISPLAY_MSG_INFO:
			reply.words[0] = (uint32_t)(g_ready ? ULMK_OK :
						    ULMK_EINVAL);
			reply.words[1] = g_fb_w;
			reply.words[2] = g_fb_h;
			reply.words[3] = (uint32_t)g_psram_fb;
			break;
		default:
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t display_init(uint8_t mod)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;
	uint32_t i;
	void *psram;
	uint32_t need;
	(void)mod;
	if (dsi_init() != 0)
		return ULMK_TID_INVALID;

	psram = board_psram_ready() ? board_psram_base() : NULL;
	need = (uint32_t)ULMK_BOARD_DISPLAY_FB_BYTES * 2u;
	if (psram && board_psram_size() >= need) {
		g_fb[0] = (uint16_t *)psram;
		g_fb[1] = (uint16_t *)((uint8_t *)psram +
				       ULMK_BOARD_DISPLAY_FB_BYTES);
		g_fb_w = (uint16_t)ULMK_BOARD_DISPLAY_W;
		g_fb_h = (uint16_t)ULMK_BOARD_DISPLAY_H;
		g_fb_pixels = (uint32_t)g_fb_w * (uint32_t)g_fb_h;
		g_psram_fb = 1;
	} else {
		g_fb[0] = g_soft0;
		g_fb[1] = g_soft1;
		g_fb_w = SOFT_FB_W;
		g_fb_h = SOFT_FB_H;
		g_fb_pixels = SOFT_FB_PIXELS;
		g_psram_fb = 0;
	}

	for (i = 0u; i < g_fb_pixels; i++) {
		g_fb[0][i] = 0u;
		g_fb[1][i] = 0u;
	}
	board_dcache_clean(g_fb[0], (size_t)g_fb_pixels * 2u);
	board_dcache_clean(g_fb[1], (size_t)g_fb_pixels * 2u);
	g_front = 0u;
	g_dpi_on = 0;
	g_ready = 1;

	g_display_eps[0] = ulmk_ep_create();
	if (g_display_eps[0] == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	/*
	 * DPI/video starts on the first flip, inside the server: dsi_fb_start
	 * attaches the DW_GDMA rearm callback, so this thread needs CAP_IRQ
	 * as well as MMIO.
	 */
	attr.name = "disp";
	attr.entry = display_server;
	attr.priority = 2u;
	attr.stack_size = 4096u;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	ulmk_cap_grant(tid, ULMK_CAP_IRQ);
	return tid;
}
