/* SPDX-License-Identifier: MIT */
/*
 * board_services.c — pinmux, console (SEGGER RTT), timer, gpio, leds.
 */
#include <ulmk/microkernel.h>
#include "board_services.h"
#include "board_console.h"
#include "board_timer.h"
#include "board_leds.h"
#include <pinmux.h>
#include <gpio.h>

void board_services_init(const ulmk_boot_info_t *info)
{
	ulmk_tid_t tid;

	tid = pinmux_init(0u);
	(void)tid;

	(void)board_console_start(info);
	(void)board_timer_start(info);

	tid = gpio_init(0u);
	if (tid != ULMK_TID_INVALID)
		(void)board_leds_init();
}
