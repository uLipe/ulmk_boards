/* SPDX-License-Identifier: MIT */
/*
 * Kernel-side early console — SEGGER RTT channel 0.
 * Lazy-init after .bss (do not call from ulmk_board_init).
 */
#include <stdint.h>
#include "board_rtt.h"

void ulmk_printk_char_out(char c)
{
	if (c == '\n')
		board_rtt_putc('\r');
	board_rtt_putc(c);
}
