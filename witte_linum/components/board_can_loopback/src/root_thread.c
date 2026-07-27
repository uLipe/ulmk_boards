/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <can.h>
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	can_frame_t tx;
	can_frame_t rx;
	uint32_t seq;

	board_services_init(info);
	if (can_init(0u, 500000u, 0u) == ULMK_TID_INVALID ||
	    can_init(1u, 500000u, 0u) == ULMK_TID_INVALID) {
		board_console_puts("CAN loopback init failed\r\n");
		ulmk_thread_exit();
	}
	for (seq = 0u;; seq++) {
		tx.id = 0x123u;
		tx.dlc = 4u;
		tx.data[0] = (uint8_t)seq;
		tx.data[1] = (uint8_t)(seq >> 8);
		tx.data[2] = 0xCAu;
		tx.data[3] = 0x4Eu;
		tx.data[4] = 0u;
		tx.data[5] = 0u;
		tx.data[6] = 0u;
		tx.data[7] = 0u;
		if (can_send(0u, &tx) == ULMK_OK) {
			board_console_printf("tx id=%x\r\n", tx.id);
			if (can_recv(1u, &rx) == ULMK_OK)
				board_console_printf("rx id=%x\r\n", rx.id);
		}
		board_timer_sleep_us(100000u);
	}
}
