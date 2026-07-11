/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/board_services.c — spawn board driver threads from the root thread.
 */

#include <ulmk/microkernel.h>
#include "board_services.h"
#include "board_console.h"
#include "board_timer.h"
#include "board_config.h"

void ulmk_board_hil_mark(uint32_t n);

void board_services_init(const ulmk_boot_info_t *info)
{
	ulmk_tid_t tid;

	ulmk_board_hil_mark(1u);
	board_console_start(info);
	ulmk_board_hil_mark(2u);
	tid = board_timer_start(info);
	if (tid == ULMK_TID_INVALID)
		ulmk_board_hil_mark(0x71u); /* timer init failed */
	else
		ulmk_board_hil_mark(3u);
}
