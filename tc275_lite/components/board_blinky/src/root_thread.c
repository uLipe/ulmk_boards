/* SPDX-License-Identifier: MIT */
/*
 * board_blinky — alternate LEDs @ 100 ms with a small serial shell.
 *
 * TC275 Lite board component (console, timer, gpio, leds).
 */

#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include "board_console.h"

void board_services_init(const ulmk_boot_info_t *info);
void board_timer_sleep_us(uint32_t us);
int board_leds_set(uint32_t led, int on);
int board_leds_get(uint32_t led, int *on);

#define BOARD_LED_1	0u
#define BOARD_LED_2	1u

#define SHELL_LINE_MAX	32u
#define TICK_US		100000u
#define POLL_SLICE_US	50000u

static ULMK_PRIVATE char g_line[SHELL_LINE_MAX];
static ULMK_PRIVATE uint32_t g_llen;
static ULMK_PRIVATE int g_phase;

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static void print_status(void)
{
	int on1;
	int on2;

	(void)board_leds_get(BOARD_LED_1, &on1);
	(void)board_leds_get(BOARD_LED_2, &on2);
	board_console_printf("led1=%c led2=%c\n",
			     on1 ? '1' : '0', on2 ? '1' : '0');
}

static void shell_exec(void)
{
	if (g_llen == 0u)
		return;
	g_line[g_llen] = '\0';

	if (streq(g_line, "help")) {
		board_console_puts("cmds: help status led1 on|off led2 on|off\n\r");
	} else if (streq(g_line, "status")) {
		print_status();
	} else if (streq(g_line, "led1 on")) {
		(void)board_leds_set(BOARD_LED_1, 1);
	} else if (streq(g_line, "led1 off")) {
		(void)board_leds_set(BOARD_LED_1, 0);
	} else if (streq(g_line, "led2 on")) {
		(void)board_leds_set(BOARD_LED_2, 1);
	} else if (streq(g_line, "led2 off")) {
		(void)board_leds_set(BOARD_LED_2, 0);
	} else {
		board_console_puts("?\n");
	}
	g_llen = 0u;
}

static void shell_poll(void)
{
	char c;

	while (board_console_getc(&c) == ULMK_OK) {
		if (c == '\r' || c == '\n') {
			shell_exec();
			board_console_puts("> ");
		} else if (c == 0x7Fu || c == '\b') {
			if (g_llen > 0u)
				g_llen--;
		} else if (g_llen + 1u < SHELL_LINE_MAX) {
			g_line[g_llen++] = c;
			board_console_putc(c);
		}
	}
}

static void sleep_with_shell(uint32_t us)
{
	uint32_t left = us;

	while (left > 0u) {
		uint32_t slice = (left > POLL_SLICE_US) ? POLL_SLICE_US : left;

		shell_poll();
		board_timer_sleep_us(slice);
		left -= slice;
	}
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	board_services_init(info);

	board_console_puts("\n");
	board_console_puts("ulmk Microkernel sample: TC275 Lite blinky\n\r");
	board_console_puts("LEDs alternate @ 100 ms. Type help for shell cmds.\n\r");
	board_console_puts("> ");

	g_phase = 0;
	g_llen = 0u;

	for (;;) {
		if (g_phase == 0) {
			(void)board_leds_set(BOARD_LED_1, 1);
			(void)board_leds_set(BOARD_LED_2, 0);
		} else {
			(void)board_leds_set(BOARD_LED_1, 0);
			(void)board_leds_set(BOARD_LED_2, 1);
		}
		g_phase ^= 1;
		sleep_with_shell(TICK_US);
	}
}
