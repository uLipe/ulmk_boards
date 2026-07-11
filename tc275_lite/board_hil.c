/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/board_hil.c — userspace milestone marker for HIL / OpenOCD polling.
 *
 * Lives in .user_bss so driver threads may write without tripping MPU Class 4.
 * Resolve g_ulmk_board_hil_scratch from the ELF for JTAG scripts (not a fixed
 * DSPR address — kernel RAM below _ulmk_user_ram_start is supervisor-only).
 */

#include <stdint.h>

volatile uint32_t g_ulmk_board_hil_scratch __attribute__((section(".user_bss")));

void ulmk_board_hil_mark(uint32_t n)
{
	g_ulmk_board_hil_scratch = n;
}
