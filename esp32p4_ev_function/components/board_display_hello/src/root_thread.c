/* SPDX-License-Identifier: MIT */
/*
 * board_display_hello — paint via PSRAM NC alias (no multi-MiB WriteBack),
 * then flip → DW_GDMA→DPI with LPCLK AUTO (0x3).
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <display.h>
#include <dsi.h>
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"
#include "board_config.h"
#include "board_cache.h"
#include "board_psram.h"

#define RGB565(r, g, b) \
	((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | (((b) & 0xF8u) >> 3)))

#define COL_BG		RGB565(0x18, 0x40, 0x80)
#define COL_PANEL	RGB565(0x00, 0x00, 0x00)
#define COL_FG		RGB565(0xFF, 0xFF, 0xFF)
#define COL_ACCENT	RGB565(0x40, 0xE0, 0xFF)

#define FONT_W		5u
#define FONT_H		7u
#define FONT_SCALE	4u
#define FONT_GAP	1u
#define LINE_GAP	((int)(FONT_H * FONT_SCALE) + 16)
#define PANEL_PAD	((int)(FONT_SCALE * 4u))

#define TITLE_LINE	"ulmk Microkernel"
#define BOARD_LINE	"Hello ESP32-P4!"

static const struct {
	char c;
	uint8_t g[5];
} g_font5x7[] = {
	{ ' ', { 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ '0', { 0x3E, 0x51, 0x49, 0x45, 0x3E } },
	{ '1', { 0x00, 0x42, 0x7F, 0x40, 0x00 } },
	{ '2', { 0x42, 0x61, 0x51, 0x49, 0x46 } },
	{ '3', { 0x21, 0x41, 0x45, 0x4B, 0x31 } },
	{ '4', { 0x18, 0x14, 0x12, 0x7F, 0x10 } },
	{ '5', { 0x27, 0x45, 0x45, 0x45, 0x39 } },
	{ '6', { 0x3C, 0x4A, 0x49, 0x49, 0x30 } },
	{ '7', { 0x01, 0x71, 0x09, 0x05, 0x03 } },
	{ '8', { 0x36, 0x49, 0x49, 0x49, 0x36 } },
	{ '9', { 0x06, 0x49, 0x49, 0x29, 0x1E } },
	{ 'A', { 0x7E, 0x11, 0x11, 0x11, 0x7E } },
	{ 'E', { 0x7F, 0x49, 0x49, 0x49, 0x41 } },
	{ 'H', { 0x7F, 0x08, 0x08, 0x08, 0x7F } },
	{ 'L', { 0x7F, 0x40, 0x40, 0x40, 0x40 } },
	{ 'M', { 0x7F, 0x02, 0x0C, 0x02, 0x7F } },
	{ 'P', { 0x7F, 0x09, 0x09, 0x09, 0x06 } },
	{ 'S', { 0x46, 0x49, 0x49, 0x49, 0x31 } },
	{ 'W', { 0x3F, 0x40, 0x38, 0x40, 0x3F } },
	{ 'a', { 0x20, 0x54, 0x54, 0x54, 0x78 } },
	{ 'c', { 0x38, 0x44, 0x44, 0x44, 0x20 } },
	{ 'e', { 0x38, 0x54, 0x54, 0x54, 0x18 } },
	{ 'i', { 0x00, 0x44, 0x7D, 0x40, 0x00 } },
	{ 'k', { 0x7F, 0x10, 0x28, 0x44, 0x00 } },
	{ 'l', { 0x00, 0x41, 0x7F, 0x40, 0x00 } },
	{ 'm', { 0x7C, 0x04, 0x18, 0x04, 0x78 } },
	{ 'n', { 0x7C, 0x08, 0x04, 0x04, 0x78 } },
	{ 'o', { 0x38, 0x44, 0x44, 0x44, 0x38 } },
	{ 'p', { 0xFC, 0x24, 0x24, 0x24, 0x18 } },
	{ 'r', { 0x7C, 0x08, 0x04, 0x04, 0x08 } },
	{ 's', { 0x48, 0x54, 0x54, 0x54, 0x24 } },
	{ 't', { 0x04, 0x3F, 0x44, 0x40, 0x20 } },
	{ 'u', { 0x3C, 0x40, 0x40, 0x20, 0x7C } },
	{ '-', { 0x08, 0x08, 0x08, 0x08, 0x08 } },
	{ ':', { 0x00, 0x36, 0x36, 0x00, 0x00 } },
	{ '!', { 0x00, 0x00, 0x5F, 0x00, 0x00 } },
};

static uint16_t g_fb_w;
static uint16_t g_fb_h;
static int g_up_y;
static int g_up_h;

static const uint8_t *font_lookup(char c)
{
	uint32_t i;

	for (i = 0u; i < (sizeof(g_font5x7) / sizeof(g_font5x7[0])); i++) {
		if (g_font5x7[i].c == c)
			return g_font5x7[i].g;
	}
	return g_font5x7[0].g;
}

static void fb_fill(uint16_t *fb, uint16_t color)
{
	uint32_t *p = (uint32_t *)(void *)fb;
	uint32_t n = ((uint32_t)g_fb_w * (uint32_t)g_fb_h) / 2u;
	uint32_t word = ((uint32_t)color << 16) | (uint32_t)color;
	uint32_t i;

	for (i = 0u; i < n; i++)
		p[i] = word;
}

static void fb_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color)
{
	int dx;
	int dy;
	uint16_t *row;
	int x1;

	if (w <= 0 || h <= 0)
		return;
	for (dy = 0; dy < h; dy++) {
		if (y + dy < 0 || y + dy >= (int)g_fb_h)
			continue;
		row = &fb[(uint32_t)(y + dy) * (uint32_t)g_fb_w];
		x1 = x + w;
		for (dx = x; dx < x1; dx++) {
			if (dx >= 0 && dx < (int)g_fb_w)
				row[dx] = color;
		}
	}
}

static void draw_char(uint16_t *fb, int x, int y, char c, uint16_t color)
{
	const uint8_t *glyph = font_lookup(c);
	uint32_t col;
	uint32_t row;

	for (row = 0u; row < FONT_H; row++) {
		for (col = 0u; col < FONT_W; col++) {
			if ((glyph[col] & (1u << row)) == 0u)
				continue;
			fb_rect(fb,
				x + (int)(col * FONT_SCALE),
				y + (int)(row * FONT_SCALE),
				(int)FONT_SCALE, (int)FONT_SCALE, color);
		}
	}
}

static int string_width(const char *s)
{
	int n = 0;

	while (s[n])
		n++;
	if (n == 0)
		return 0;
	return n * (int)((FONT_W + FONT_GAP) * FONT_SCALE) -
	       (int)(FONT_GAP * FONT_SCALE);
}

static void draw_string(uint16_t *fb, int x, int y, const char *s,
			uint16_t color)
{
	int cx = x;

	while (*s) {
		draw_char(fb, cx, y, *s, color);
		cx += (int)((FONT_W + FONT_GAP) * FONT_SCALE);
		s++;
	}
}

static void draw_string_centered(uint16_t *fb, int y, const char *s,
				 uint16_t color)
{
	int x = ((int)g_fb_w - string_width(s)) / 2;

	draw_string(fb, x, y, s, color);
}

static void fmt_uptime(char *buf, uint32_t sec)
{
	buf[0] = 'u';
	buf[1] = 'p';
	buf[2] = 't';
	buf[3] = 'i';
	buf[4] = 'm';
	buf[5] = 'e';
	buf[6] = ':';
	buf[7] = ' ';
	buf[8] = (char)('0' + ((sec / 100000u) % 10u));
	buf[9] = (char)('0' + ((sec / 10000u) % 10u));
	buf[10] = (char)('0' + ((sec / 1000u) % 10u));
	buf[11] = (char)('0' + ((sec / 100u) % 10u));
	buf[12] = (char)('0' + ((sec / 10u) % 10u));
	buf[13] = (char)('0' + (sec % 10u));
	buf[14] = ' ';
	buf[15] = 's';
	buf[16] = '\0';
}

static int max3(int a, int b, int c)
{
	if (a < b)
		a = b;
	if (a < c)
		a = c;
	return a;
}

static void draw_static(uint16_t *fb)
{
	char up[20];
	int th = (int)(FONT_H * FONT_SCALE);
	int block_h;
	int block_w;
	int y0;
	int x0;

	fmt_uptime(up, 0u);
	block_h = 3 * th + 2 * LINE_GAP;
	block_w = max3(string_width(TITLE_LINE), string_width(BOARD_LINE),
		       string_width(up));
	y0 = ((int)g_fb_h - block_h) / 2;
	x0 = ((int)g_fb_w - block_w) / 2;
	g_up_y = y0 + 2 * LINE_GAP;
	g_up_h = th + PANEL_PAD;

	fb_fill(fb, COL_BG);
	fb_rect(fb, x0 - PANEL_PAD, y0 - PANEL_PAD,
		block_w + 2 * PANEL_PAD, block_h + 2 * PANEL_PAD, COL_PANEL);
	draw_string_centered(fb, y0, TITLE_LINE, COL_FG);
	draw_string_centered(fb, y0 + LINE_GAP, BOARD_LINE, COL_ACCENT);
	draw_string_centered(fb, g_up_y, up, COL_FG);
}

static void draw_uptime(uint16_t *fb, uint32_t sec)
{
	char up[20];
	int tw;
	int x0;
	int y;
	int x;
	uint16_t *row;

	fmt_uptime(up, sec);
	tw = string_width(up) + 2 * PANEL_PAD;
	x0 = ((int)g_fb_w - tw) / 2;
	for (y = g_up_y; y < g_up_y + g_up_h && y < (int)g_fb_h; y++) {
		if (y < 0)
			continue;
		row = &fb[(uint32_t)y * (uint32_t)g_fb_w];
		for (x = x0; x < x0 + tw; x++) {
			if (x >= 0 && x < (int)g_fb_w)
				row[x] = COL_PANEL;
		}
	}
	draw_string_centered(fb, g_up_y, up, COL_FG);
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	uint16_t *fb0;
	uint16_t *fb1;
	uint16_t *n0;
	uint16_t *n1;
	uint32_t sec;
	uint32_t pixels;
	uint32_t t0;
	uint32_t t1;
	uint32_t t2;

	board_services_init(info);
	board_console_puts("\r\n");
	board_console_puts("display hello\r\n");

	if (display_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("display init failed\r\n");
		ulmk_thread_exit();
	}
	(void)display_on(1);

	g_fb_w = display_width();
	g_fb_h = display_height();
	pixels = (uint32_t)g_fb_w * (uint32_t)g_fb_h;

	board_console_printf(
		"display hello running fb=%ux%u psram=%d dpi=%d\r\n",
		(unsigned)g_fb_w, (unsigned)g_fb_h,
		display_fb_in_psram(), dsi_fb_ready());

	fb0 = display_fb(0u);
	fb1 = display_fb(1u);
	if (!fb0 || !fb1) {
		board_console_puts("display fb missing\r\n");
		ulmk_thread_exit();
	}
	n0 = fb0;
	n1 = fb1;
	board_console_printf("display paint cached=%p/%p\r\n",
			     (void *)n0, (void *)n1);

	fb_fill(n0, RGB565(0xFF, 0x00, 0x00));
	fb_fill(n1, RGB565(0xFF, 0x00, 0x00));
	board_dcache_clean(fb0, pixels * 2u);
	board_dcache_clean(fb1, pixels * 2u);
	board_console_printf("display px0=0x%04x (expect red)\r\n",
			     (unsigned)n0[0]);

	if (display_flip() != ULMK_OK) {
		board_console_puts("display flip failed\r\n");
		ulmk_thread_exit();
	}
	board_console_printf("display dpi=%d solid red\r\n", dsi_fb_ready());
	dsi_fb_diag(1000u);
	(void)ulmk_sleep_ms(1500u);

	/*
	 * Paint both buffers directly rather than painting one and copying:
	 * the copy adds a full-frame read stream on top of the writes, and
	 * both buffers must carry the static banner because the uptime loop
	 * alternates between them.
	 */
	t0 = dsi_fb_frames();
	draw_static(n0);
	draw_static(n1);
	t1 = dsi_fb_frames();
	board_dcache_clean(fb0, pixels * 2u);
	board_dcache_clean(fb1, pixels * 2u);
	t2 = dsi_fb_frames();

	if (display_flip() != ULMK_OK) {
		board_console_puts("display flip failed\r\n");
		ulmk_thread_exit();
	}
	board_console_printf("display banner on: paint=%u writeback=%u "
			     "frames of 16.7ms\r\n",
			     (unsigned)(t1 - t0), (unsigned)(t2 - t1));
	(void)display_on(1);

	for (sec = 1u;; sec++) {
		fb0 = display_write(0u, 0u, g_fb_w, g_fb_h);
		if (!fb0) {
			board_console_puts("display write failed\r\n");
			ulmk_thread_exit();
		}
		draw_uptime(fb0, sec);
		board_dcache_clean(
			&fb0[(uint32_t)g_up_y * (uint32_t)g_fb_w],
			(size_t)((uint32_t)g_up_h * (uint32_t)g_fb_w * 2u));
		if (display_flip() != ULMK_OK) {
			board_console_puts("display flip failed\r\n");
			ulmk_thread_exit();
		}
		(void)display_on(1);
		board_timer_sleep_us(1000000u);
	}
}
