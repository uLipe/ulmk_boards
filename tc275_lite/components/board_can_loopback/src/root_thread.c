/* SPDX-License-Identifier: MIT */
/*
 * board_can_loopback — MultiCAN node 0 internal loopback @ 5 Hz.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <can.h>
#include "board_config.h"
#include "board_console.h"

void board_services_init(const ulmk_boot_info_t *info);
void board_timer_sleep_us(uint32_t us);

#define CAN_NODE		0u
#define CAN_BITRATE		500000u
#define CAN_ID			0x321u
#define PERIOD_US		200000u

static void print_frame(const char *tag, const can_frame_t *f)
{
	board_console_printf(
		"%s id=0x%X dlc=%u data=%X %X %X %X %X %X %X %X\r\n",
		tag, f->id, (uint32_t)f->dlc,
		(uint32_t)f->data[0], (uint32_t)f->data[1],
		(uint32_t)f->data[2], (uint32_t)f->data[3],
		(uint32_t)f->data[4], (uint32_t)f->data[5],
		(uint32_t)f->data[6], (uint32_t)f->data[7]);
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_tid_t tid;
	can_frame_t tx;
	can_frame_t rx;
	uint32_t seq;
	int rc;

	board_services_init(info);

	tid = can_init(CAN_NODE, CAN_BITRATE, 1);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("can_init failed\r\n");
		ulmk_thread_exit();
	}

	board_console_puts("\r\n");
	board_console_puts(
		"ulmk: TC275 Lite CAN loopback (N0→N1 LBM, P20.8/7)\r\n");
	board_console_puts(
		"500 kbit/s, TLE9251 #NEN low, TX/RX echo @ 5 Hz\r\n");

	seq = 0u;
	tx.id = CAN_ID;
	tx.dlc = 8u;
	for (;;) {
		tx.data[0] = (uint8_t)(seq & 0xFFu);
		tx.data[1] = (uint8_t)((seq >> 8) & 0xFFu);
		tx.data[2] = (uint8_t)((seq >> 16) & 0xFFu);
		tx.data[3] = (uint8_t)((seq >> 24) & 0xFFu);
		tx.data[4] = 0xCAu;
		tx.data[5] = 0xFEu;
		tx.data[6] = 0xBAu;
		tx.data[7] = 0xBEu;

		rc = can_send(CAN_NODE, &tx);
		if (rc != ULMK_OK) {
			board_console_printf(
				"can_send failed rc=%d diag=0x%X\r\n",
				rc, can_last_diag());
		} else {
			print_frame("tx", &tx);
			rc = can_recv(CAN_NODE, &rx);
			if (rc != ULMK_OK)
				board_console_printf(
					"can_recv failed %d\r\n", rc);
			else
				print_frame("rx", &rx);
		}
		seq++;
		board_timer_sleep_us(PERIOD_US);
	}
}
