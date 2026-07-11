/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/board_printk_stub.c — kernel printk sink is intentionally empty.
 *
 * All character output goes through the userspace board_console service
 * (ASCLIN0).  ulmk_sim_exit is a weak trap for integration tests on hardware.
 */

#include <stdint.h>

void ulmk_printk_char_out(char c)
{
	(void)c;
}

__attribute__((weak, noreturn)) void ulmk_sim_exit(int code)
{
	(void)code;
	for (;;)
		;
}
