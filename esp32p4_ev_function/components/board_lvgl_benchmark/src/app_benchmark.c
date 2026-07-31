/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <lvgl.h>
#include <demos/lv_demos.h>
#include <display.h>
#include "board_config.h"
#include "board_console.h"
#include "app_benchmark.h"
#include "port_tick.h"
#include "port_disp.h"
#include "port_indev.h"
#include "psram_stress.h"

#ifndef ULMK_BANNER_VERSION
#define ULMK_BANNER_VERSION	"0.3.0"
#endif
#ifndef ULMK_BANNER_BSP
#define ULMK_BANNER_BSP		"ESP32-P4 Function EV"
#endif

#define SPLASH_FADE_MS		700u
#define SPLASH_HOLD_MS		1800u

LV_IMAGE_DECLARE(img_benchmark_lvgl_logo_argb);

enum {
	SPLASH_TITLE_IN = 0,
	SPLASH_TITLE_HOLD,
	SPLASH_TITLE_OUT,
	SPLASH_LOGO_IN,
	SPLASH_LOGO_HOLD,
	SPLASH_LOGO_OUT,
	SPLASH_BSP_IN,
	SPLASH_BSP_HOLD,
	SPLASH_BSP_OUT,
	SPLASH_DONE,
};

static lv_obj_t *g_splash;
static lv_obj_t *g_splash_body;
static uint8_t g_splash_phase;
static uint8_t g_splash_done;
static uint8_t g_bench_started;
static lv_timer_t *g_splash_timer;

static void lvgl_log_print(lv_log_level_t level, const char *buf)
{
	(void)level;
	board_console_puts(buf);
}

void app_lvgl_assert(void)
{
	static uint32_t seen;

	/* An assertion inside the render loop repeats every frame. */
	if (seen < 8u)
		board_console_printf("lvgl ASSERT #%u\r\n", (unsigned)++seen);
	else
		seen++;
}

static void splash_set_opa(void *obj, int32_t v)
{
	lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void splash_clear_body(void)
{
	if (g_splash_body)
		lv_obj_clean(g_splash_body);
}

static void splash_fade(lv_obj_t *obj, int32_t from, int32_t to,
			lv_anim_completed_cb_t done_cb)
{
	lv_anim_t a;

	lv_obj_set_style_opa(obj, (lv_opa_t)from, 0);
	lv_anim_init(&a);
	lv_anim_set_var(&a, obj);
	lv_anim_set_values(&a, from, to);
	lv_anim_set_duration(&a, SPLASH_FADE_MS);
	lv_anim_set_exec_cb(&a, splash_set_opa);
	lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
	if (done_cb)
		lv_anim_set_completed_cb(&a, done_cb);
	lv_anim_start(&a);
}

static void splash_build_title(void)
{
	lv_obj_t *title;

	splash_clear_body();
	title = lv_label_create(g_splash_body);
	lv_label_set_text(title, "LVGL benchmark demo");
	lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_color(title, lv_color_white(), 0);
	lv_obj_center(title);
}

static void splash_build_logo(void)
{
	lv_obj_t *row;
	lv_obj_t *img;
	lv_obj_t *plus;
	lv_obj_t *os;

	splash_clear_body();
	lv_obj_set_flex_flow(g_splash_body, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(g_splash_body, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	row = lv_obj_create(g_splash_body);
	lv_obj_remove_style_all(row);
	lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(row, 20, 0);

	img = lv_image_create(row);
	lv_image_set_src(img, &img_benchmark_lvgl_logo_argb);

	plus = lv_label_create(row);
	lv_label_set_text(plus, "+");
	lv_obj_set_style_text_font(plus, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_color(plus, lv_color_white(), 0);

	os = lv_label_create(row);
	lv_label_set_text(os, "ulmk microkernel");
	lv_obj_set_style_text_font(os, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(os, lv_color_white(), 0);
}

static void splash_build_bsp(void)
{
	lv_obj_t *line1;
	lv_obj_t *line2;

	splash_clear_body();
	lv_obj_set_flex_flow(g_splash_body, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(g_splash_body, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_row(g_splash_body, 12, 0);

	line1 = lv_label_create(g_splash_body);
	lv_label_set_text(line1, "BSP: " ULMK_BANNER_BSP);
	lv_obj_set_style_text_font(line1, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(line1, lv_color_white(), 0);

	line2 = lv_label_create(g_splash_body);
	lv_label_set_text(line2, "Version: " ULMK_BANNER_VERSION);
	lv_obj_set_style_text_font(line2, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(line2, lv_color_white(), 0);
}

static void splash_phase_advance(lv_anim_t *a);

static void splash_hold_cb(lv_timer_t *t)
{
	(void)t;
	lv_timer_delete(g_splash_timer);
	g_splash_timer = NULL;
	g_splash_phase++;
	splash_fade(g_splash_body, LV_OPA_COVER, LV_OPA_TRANSP,
		    splash_phase_advance);
}

static void splash_schedule_hold(void)
{
	g_splash_timer = lv_timer_create(splash_hold_cb, SPLASH_HOLD_MS, NULL);
	lv_timer_set_repeat_count(g_splash_timer, 1);
}

static void splash_phase_advance(lv_anim_t *a)
{
	(void)a;

	switch (g_splash_phase) {
	case SPLASH_TITLE_IN:
		splash_build_title();
		splash_fade(g_splash_body, LV_OPA_TRANSP, LV_OPA_COVER, NULL);
		g_splash_phase = SPLASH_TITLE_HOLD;
		splash_schedule_hold();
		break;
	case SPLASH_TITLE_OUT:
		g_splash_phase = SPLASH_LOGO_IN;
		splash_build_logo();
		splash_fade(g_splash_body, LV_OPA_TRANSP, LV_OPA_COVER, NULL);
		g_splash_phase = SPLASH_LOGO_HOLD;
		splash_schedule_hold();
		break;
	case SPLASH_LOGO_OUT:
		g_splash_phase = SPLASH_BSP_IN;
		splash_build_bsp();
		splash_fade(g_splash_body, LV_OPA_TRANSP, LV_OPA_COVER, NULL);
		g_splash_phase = SPLASH_BSP_HOLD;
		splash_schedule_hold();
		break;
	case SPLASH_BSP_OUT:
		if (g_splash) {
			lv_obj_delete(g_splash);
			g_splash = NULL;
			g_splash_body = NULL;
		}
		g_splash_phase = SPLASH_DONE;
		g_splash_done = 1u;
		break;
	default:
		break;
	}
}

static void splash_start(lv_display_t *disp)
{
	lv_obj_t *scr = lv_display_get_screen_active(disp);

	g_splash_phase = SPLASH_TITLE_IN;
	g_splash_done = 0u;
	g_bench_started = 0u;

	g_splash = lv_obj_create(scr);
	lv_obj_remove_style_all(g_splash);
	lv_obj_set_size(g_splash, LV_PCT(100), LV_PCT(100));
	lv_obj_set_style_bg_opa(g_splash, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(g_splash, lv_color_black(), 0);
	lv_obj_center(g_splash);

	g_splash_body = lv_obj_create(g_splash);
	lv_obj_remove_style_all(g_splash_body);
	lv_obj_set_size(g_splash_body, LV_PCT(100), LV_PCT(100));
	lv_obj_center(g_splash_body);
	lv_obj_set_style_opa(g_splash_body, LV_OPA_TRANSP, 0);

	splash_phase_advance(NULL);
}

static void bench_end_cb(const lv_demo_benchmark_summary_t *summary)
{
	int32_t n = summary->valid_scene_cnt;
	int32_t fps = 0;
	int32_t cpu = 0;
	int32_t render = 0;
	int32_t flush = 0;

	if (n > 0) {
		fps = summary->total_avg_fps / n;
		cpu = summary->total_avg_cpu / n;
		render = summary->total_avg_render_time / n;
		flush = summary->total_avg_flush_time / n;
	}
	board_console_printf(
		"lvgl bench DONE scenes=%d avg_fps=%d avg_cpu=%d%% "
		"render=%dms flush=%dms\r\n",
		(int)n, (int)fps, (int)cpu, (int)render, (int)flush);
	lv_demo_benchmark_summary_display(summary);
}

static void bench_start(lv_display_t *disp)
{
	/* Wipe splash leftovers once; LVGL sync_areas keep dual-FB coherent. */
	port_disp_blank();
	lv_obj_invalidate(lv_display_get_screen_active(disp));

	lv_sysmon_show_performance(disp);
	lv_sysmon_show_memory(disp);
	lv_demo_benchmark_set_end_cb(bench_end_cb);
	board_console_puts("lvgl benchmark\r\n");
	lv_demo_benchmark();
}

void app_benchmark_run(void)
{
	lv_display_t *disp;
	uint32_t ms;
	lv_mem_monitor_t mon;

	/*
	 * LV_MEM_ADR points into PSRAM after the dual FBs.  PMP already
	 * grants the window; display_fb_base() confirms the dual FBs exist.
	 */
	if (display_fb_base() == NULL || !display_fb_in_psram()) {
		board_console_puts("lvgl: PSRAM FB unavailable\r\n");
		ulmk_thread_exit();
	}
#if LV_MEM_ADR != 0
	_Static_assert(LV_MEM_ADR == ULMK_BOARD_LVGL_HEAP_ADDR,
		       "lv_conf.h LV_MEM_ADR != board_config heap");
	_Static_assert(LV_MEM_SIZE == ULMK_BOARD_LVGL_HEAP_SIZE,
		       "lv_conf.h LV_MEM_SIZE != board_config heap");
#endif
	board_console_printf("lvgl mem pool %uKiB @ %08x\r\n",
			     (unsigned)(ULMK_BOARD_LVGL_HEAP_SIZE / 1024u),
			     (unsigned)ULMK_BOARD_LVGL_HEAP_ADDR);

	lv_init();
	lv_log_register_print_cb(lvgl_log_print);
	board_console_puts("lvgl init ok\r\n");
	port_tick_init();

	disp = port_disp_init();
	if (!disp) {
		board_console_puts("lvgl: display port failed\r\n");
		ulmk_thread_exit();
	}

	/*
	 * Display is already streaming from PSRAM via DW_GDMA.  Stress the
	 * free region next to the LVGL heap before any tlsf traffic so a
	 * bus/timing bug cannot be mistaken for an LVGL logic bug.
	 */
	if (psram_stress_under_scanout() != 0) {
		board_console_puts("lvgl: aborting - PSRAM unreliable\r\n");
		ulmk_thread_exit();
	}

	(void)port_indev_init(disp);

	lv_mem_monitor(&mon);
	board_console_printf("lvgl heap total=%u free=%u\r\n",
			     mon.total_size, mon.free_size);

	splash_start(disp);

	for (;;) {
		ms = lv_timer_handler();
		if (g_splash_done && !g_bench_started) {
			g_bench_started = 1u;
			bench_start(disp);
		}
		if (ms == 0u)
			continue;
		if (ms == LV_NO_TIMER_READY || ms > 10u)
			ms = 5u;
		(void)ulmk_sleep_ms(ms);
	}
}
