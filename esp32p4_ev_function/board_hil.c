/* SPDX-License-Identifier: MIT */
/*
 * esp32p4_ev_function/board_hil.c — milestone marker for HIL scripts.
 *
 * Lives in .user_bss so DRIVER threads may write without a PMP fault.
 */
#include <stdint.h>

volatile uint32_t g_ulmk_board_hil_scratch __attribute__((section(".user_bss")));

void ulmk_board_hil_mark(uint32_t n)
{
	g_ulmk_board_hil_scratch = n;
}
