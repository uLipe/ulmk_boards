/* SPDX-License-Identifier: MIT */
/*
 * board_console.c — kit policy: ASCLIN0 on P14.0/P14.1 + RAM log for HIL.
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_console.h"
#include <asclin.h>

#define CONSOLE_LOG_SIZE	2048u
#define CONSOLE_ASCLIN		0u

volatile uint32_t g_ulmk_console_log_len
	__attribute__((section(".user_bss")));
volatile char g_ulmk_console_log[CONSOLE_LOG_SIZE]
	__attribute__((section(".user_bss")));

static void console_log_putc(char c)
{
	uint32_t n;

	n = g_ulmk_console_log_len;
	if (n >= CONSOLE_LOG_SIZE - 1u)
		return;
	g_ulmk_console_log[n] = c;
	g_ulmk_console_log_len = n + 1u;
}

void board_console_putc(char c)
{
	console_log_putc(c);
	(void)asclin_tx_byte(CONSOLE_ASCLIN, (uint8_t)c);
}

void board_console_puts(const char *s)
{
	if (!s)
		return;
	while (*s)
		board_console_putc(*s++);
}

int board_console_getc(char *out)
{
	uint8_t b;
	int rc;

	rc = asclin_rx_byte_nb(CONSOLE_ASCLIN, &b);
	if (rc != ULMK_OK)
		return rc;
	if (out)
		*out = (char)b;
	return ULMK_OK;
}

ulmk_tid_t board_console_start(const ulmk_boot_info_t *info)
{
	asclin_pins_t pins;
	ulmk_tid_t tid;

	(void)info;
	g_ulmk_console_log_len = 0u;

	/* Lite Kit USB–UART bridge: P14.0 TX alt2, P14.1 RX ALTI0. */
	pins.tx_port = 14u;
	pins.tx_pin  = 0u;
	pins.tx_alt  = 2u;
	pins.rx_port = 14u;
	pins.rx_pin  = 1u;
	pins.rx_alti = 0u;

	tid = asclin_init(CONSOLE_ASCLIN, &pins, ULMK_BOARD_CONSOLE_BAUD,
			  ULMK_BOARD_FA_HZ);
	return tid;
}
