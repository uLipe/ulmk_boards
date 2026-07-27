/* SPDX-License-Identifier: MIT */
/*
 * board_rtt.c — thin wrapper over SEGGER RTT channel 0 (Terminal).
 *
 * Safe after .bss zero.  board_init must not call this (pre-relocation).
 */
#include <stdint.h>
#include "board_rtt.h"
#include "SEGGER_RTT.h"

static uint8_t g_rtt_ready;

void board_rtt_init(void)
{
	if (g_rtt_ready)
		return;
	SEGGER_RTT_Init();
	g_rtt_ready = 1u;
}

void board_rtt_putc(char c)
{
	board_rtt_init();
	(void)SEGGER_RTT_PutCharSkipNoLock(0u, c);
}

void board_rtt_write(const char *buf, uint32_t len)
{
	board_rtt_init();
	if (!buf || len == 0u)
		return;
	(void)SEGGER_RTT_WriteSkipNoLock(0u, buf, (unsigned)len);
}

int board_rtt_getc(char *out)
{
	int k;

	board_rtt_init();
	if (!out)
		return -1;
	k = SEGGER_RTT_GetKey();
	if (k < 0)
		return -1;
	*out = (char)k;
	return 0;
}
