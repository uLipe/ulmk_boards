/* SPDX-License-Identifier: MIT */
/*
 * Minimal board services for C29 bring-up.
 * Full console IPC server can replace this once UP scheduling is proven.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>

void board_leds_init(void);

void board_services_init(const ulmk_boot_info_t *info)
{
	(void)info;
	ulmk_tick_start();
	board_leds_init();
}
