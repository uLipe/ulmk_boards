/* SPDX-License-Identifier: MIT */
/*
 * LVGL display port — DIRECT dual-buffer (STM32 LTDC).
 *
 * Cache contract (Write-Back SDRAM):
 *   - Clean+invalidate each flush rectangle (writeback to SDRAM for LTDC,
 *     drop lines — same pattern as several LVGL draw-unit / LCD ports).
 *   - On last flush, also flush previous frame's dirty rects: LVGL DIRECT
 *     synced them into this buffer via CPU memcpy before draw, and those
 *     lines are not always covered by this frame's inv areas.
 *   - display_present() waits vsync via notif (no busy-wait).
 * LVGL owns buffer sync (sync_areas); we do not memcpy between FBs here.
 */
#include <stdint.h>
#include <string.h>
#include <lvgl.h>
#include <display.h>
#include "board_config.h"
#include "board_cache.h"
#include "board_console.h"
#include "board_timer.h"
#include "port_disp.h"

#include "src/display/lv_display_private.h"

#define FB_STRIDE	(DISPLAY_W * DISPLAY_BPP)
#define MAX_DIRTY	32

static uint16_t *g_fb0;
static uint16_t *g_fb1;
static uint32_t g_fps_frames;
static uint32_t g_fps_t0_ms;

/* Previous frame's unjoined dirty rects — cleaned after DIRECT sync. */
static lv_area_t g_prev_dirty[MAX_DIRTY];
static uint16_t g_prev_n;

/* This frame's dirty rects — become g_prev_* after present. */
static lv_area_t g_cur_dirty[MAX_DIRTY];
static uint16_t g_cur_n;

static void clean_fb_area(void *fb, const lv_area_t *a)
{
	int32_t w;
	int32_t h;

	if (!fb || !a)
		return;
	w = a->x2 - a->x1 + 1;
	h = a->y2 - a->y1 + 1;
	if (w <= 0 || h <= 0)
		return;

	board_dcache_clean_invalidate_area(fb, FB_STRIDE, a->x1, a->y1, w, h,
					   DISPLAY_BPP);
}

static void fps_kick(void)
{
	uint32_t now;

	g_fps_frames++;
	now = board_timer_now_ticks() / (ULMK_BOARD_FSTM_HZ / 1000u);
	if (g_fps_t0_ms == 0u) {
		g_fps_t0_ms = now;
		return;
	}
	if ((now - g_fps_t0_ms) >= 5000u) {
		board_console_printf("lvgl fps=%u/5s (~%u)\r\n", g_fps_frames,
				     g_fps_frames / 5u);
		g_fps_frames = 0u;
		g_fps_t0_ms = now;
	}
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
	uint16_t i;

	clean_fb_area(px_map, area);

	if (g_cur_n < MAX_DIRTY)
		g_cur_dirty[g_cur_n++] = *area;

	if (!lv_display_flush_is_last(disp)) {
		lv_display_flush_ready(disp);
		return;
	}

	/*
	 * DIRECT sync (refr_sync_areas) already memcpy'd g_prev_dirty into
	 * px_map before this frame's draw. Those writes sit in D-cache and
	 * must hit SDRAM before LTDC points CFBAR here — otherwise banner /
	 * black-pixel glitches on regions not redrawn this frame.
	 */
	for (i = 0; i < g_prev_n; i++)
		clean_fb_area(px_map, &g_prev_dirty[i]);

	(void)display_present(px_map);
	fps_kick();

	g_prev_n = g_cur_n;
	if (g_prev_n > 0u) {
		memcpy(g_prev_dirty, g_cur_dirty,
		       (size_t)g_prev_n * sizeof(g_prev_dirty[0]));
	}
	g_cur_n = 0u;

	lv_display_flush_ready(disp);
}

lv_display_t *port_disp_init(void)
{
	lv_display_t *disp;

	g_fb0 = display_fb(0u);
	g_fb1 = display_fb(1u);
	if (!g_fb0 || !g_fb1)
		return NULL;

	memset(g_fb0, 0, DISPLAY_FB_BYTES);
	memset(g_fb1, 0, DISPLAY_FB_BYTES);
	board_dcache_clean_invalidate(g_fb0, DISPLAY_FB_BYTES);
	board_dcache_clean_invalidate(g_fb1, DISPLAY_FB_BYTES);
	(void)display_present(g_fb1);

	g_prev_n = 0u;
	g_cur_n = 0u;

	disp = lv_display_create(DISPLAY_W, DISPLAY_H);
	if (!disp)
		return NULL;

	lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
	lv_display_set_flush_cb(disp, flush_cb);
	lv_display_set_buffers(disp, g_fb0, g_fb1, DISPLAY_FB_BYTES,
			       LV_DISPLAY_RENDER_MODE_DIRECT);
	board_console_printf("lvgl DIRECT %ux%u mem=%uKiB@%08x cache=%s\r\n",
			     DISPLAY_W, DISPLAY_H,
			     (unsigned)(ULMK_BOARD_LVGL_HEAP_SIZE / 1024u),
			     (unsigned)ULMK_BOARD_LVGL_HEAP_ADDR,
			     (ULMK_BOARD_ENABLE_CPU_CACHE ? "on" : "off"));
	return disp;
}

void port_disp_blank(void)
{
	lv_display_t *disp = lv_display_get_default();

	if (g_fb0)
		memset(g_fb0, 0, DISPLAY_FB_BYTES);
	if (g_fb1)
		memset(g_fb1, 0, DISPLAY_FB_BYTES);

	if (disp) {
		lv_ll_clear(&disp->sync_areas);
		disp->inv_p = 0;
		if (g_fb0 && g_fb1) {
			lv_display_set_buffers(disp, g_fb0, g_fb1,
					       DISPLAY_FB_BYTES,
					       LV_DISPLAY_RENDER_MODE_DIRECT);
		}
	}

	g_prev_n = 0u;
	g_cur_n = 0u;

	board_dcache_clean_invalidate(g_fb0, DISPLAY_FB_BYTES);
	board_dcache_clean_invalidate(g_fb1, DISPLAY_FB_BYTES);
	if (g_fb1)
		(void)display_present(g_fb1);
}
