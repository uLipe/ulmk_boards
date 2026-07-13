/* SPDX-License-Identifier: MIT */
/*
 * board_services.c — bring up drivers the board itself depends on.
 */
#include <ulmk/microkernel.h>
#include "board_services.h"
#include "board_console.h"
#include "board_timer.h"
#include "board_leds.h"
#include <pinmux.h>
#include <gpio.h>

void ulmk_board_hil_mark(uint32_t n);

void board_services_init(const ulmk_boot_info_t *info)
{
	ulmk_tid_t tid;

	ulmk_board_hil_mark(1u);

	tid = pinmux_init(0u);
	if (tid == ULMK_TID_INVALID)
		ulmk_board_hil_mark(0x70u);

	board_console_start(info);
	ulmk_board_hil_mark(2u);

	tid = board_timer_start(info);
	if (tid == ULMK_TID_INVALID)
		ulmk_board_hil_mark(0x71u);
	else
		ulmk_board_hil_mark(3u);

	tid = gpio_init(0u);
	if (tid != ULMK_TID_INVALID)
		(void)board_leds_init();
}
