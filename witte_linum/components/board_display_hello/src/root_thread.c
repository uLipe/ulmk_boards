/* SPDX-License-Identifier: MIT */
/*
 * board_display_hello — centered banner + live uptime on RGB565 FB.
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <display.h>
#include <pwm.h>
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"
#include "board_config.h"
#include "board_cache.h"

#define RGB565(r, g, b) \
	((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | (((b) & 0xF8u) >> 3)))

#define COL_BG		RGB565(0x10, 0x18, 0x30)
#define COL_PANEL	RGB565(0x00, 0x00, 0x00)
#define COL_FG		RGB565(0xFF, 0xFF, 0xFF)
#define COL_ACCENT	RGB565(0x40, 0xC8, 0xFF)

#define FONT_W		5u
#define FONT_H		7u
#define FONT_SCALE	4u
#define FONT_GAP	1u
#define LINE_GAP	((int)(FONT_H * FONT_SCALE) + 16)
#define PANEL_PAD	((int)(FONT_SCALE * 4u))

#define TITLE_LINE	"ulmk Microkernel"
#define BOARD_LINE	"Hello Linum!"

/*
 * 5x7 glyphs, one byte per column, bit0 = top row.
 */
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
	{ 'H', { 0x7F, 0x08, 0x08, 0x08, 0x7F } },
	{ 'L', { 0x7F, 0x40, 0x40, 0x40, 0x40 } },
	{ 'M', { 0x7F, 0x02, 0x0C, 0x02, 0x7F } },
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
	uint32_t n;

	for (n = 0u; n < (uint32_t)DISPLAY_W * DISPLAY_H; n++)
		fb[n] = color;
}

static void fb_pixel(uint16_t *fb, int x, int y, uint16_t color)
{
	if (x < 0 || y < 0 || x >= (int)DISPLAY_W || y >= (int)DISPLAY_H)
		return;
	fb[(uint32_t)y * DISPLAY_W + (uint32_t)x] = color;
}

static void fb_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color)
{
	int dx;
	int dy;

	for (dy = 0; dy < h; dy++) {
		for (dx = 0; dx < w; dx++)
			fb_pixel(fb, x + dx, y + dy, color);
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
				FONT_SCALE, FONT_SCALE, color);
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

static void draw_string(uint16_t *fb, int x, int y, const char *s, uint16_t color)
{
	int cx = x;

	while (*s) {
		draw_char(fb, cx, y, *s, color);
		cx += (int)((FONT_W + FONT_GAP) * FONT_SCALE);
		s++;
	}
}

static void draw_string_centered(uint16_t *fb, int y, const char *s, uint16_t color)
{
	int x = ((int)DISPLAY_W - string_width(s)) / 2;

	draw_string(fb, x, y, s, color);
}

static void fmt_uptime(char *buf, uint32_t sec)
{
	/* "uptime: NNNNNN s" */
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

static void draw_frame(uint16_t *fb, uint32_t sec)
{
	char up[20];
	int th = (int)(FONT_H * FONT_SCALE);
	int block_h;
	int block_w;
	int y0;
	int x0;

	fmt_uptime(up, sec);
	block_h = 3 * th + 2 * LINE_GAP;
	block_w = max3(string_width(TITLE_LINE), string_width(BOARD_LINE),
		       string_width(up));
	y0 = ((int)DISPLAY_H - block_h) / 2;
	x0 = ((int)DISPLAY_W - block_w) / 2;

	fb_fill(fb, COL_BG);
	fb_rect(fb, x0 - PANEL_PAD, y0 - PANEL_PAD,
		block_w + 2 * PANEL_PAD, block_h + 2 * PANEL_PAD, COL_PANEL);
	draw_string_centered(fb, y0, TITLE_LINE, COL_FG);
	draw_string_centered(fb, y0 + LINE_GAP, BOARD_LINE, COL_ACCENT);
	draw_string_centered(fb, y0 + 2 * LINE_GAP, up, COL_FG);
	board_dcache_clean(fb, DISPLAY_FB_BYTES);
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	uint16_t *fb;
	uint32_t sec;

	board_services_init(info);
	board_console_puts("\r\n");
	board_console_puts("display hello\r\n");

	if (display_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("display init failed\r\n");
		ulmk_thread_exit();
	}
	(void)display_on(1);

	if (pwm_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("display backlight init failed\r\n");
		ulmk_thread_exit();
	}
	(void)pwm_config(ULMK_BOARD_PWM_BACKLIGHT, 1000u, 900u);
	(void)pwm_enable(ULMK_BOARD_PWM_BACKLIGHT, 1);

	board_console_puts("display hello running\r\n");

	for (sec = 0u;; sec++) {
		fb = display_write(0u, 0u, DISPLAY_W, DISPLAY_H);
		if (!fb) {
			board_console_puts("display write failed\r\n");
			ulmk_thread_exit();
		}
		draw_frame(fb, sec);
		if (display_flip() != ULMK_OK) {
			board_console_puts("display flip failed\r\n");
			ulmk_thread_exit();
		}
		board_timer_sleep_us(1000000u);
	}
}
