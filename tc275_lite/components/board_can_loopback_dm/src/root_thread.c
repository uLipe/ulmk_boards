/* SPDX-License-Identifier: MIT */
/*
 * board_can_loopback_dm — MultiCAN loopback via /dev/can0.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <ulmk_device.h>
#include <ulmk_device_can.h>
#include <can_dm.h>
#include "board_config.h"
#include "board_console.h"
#include "board_devices.h"

void board_services_init(const ulmk_boot_info_t *info);
void board_timer_sleep_us(uint32_t us);

#define CAN_NODE		0u
#define CAN_BITRATE		500000u
#define CAN_ID			0x321u
#define PERIOD_US		200000u

static void print_frame(const char *tag, const struct ulmk_can_frame *f)
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
	ulmk_dev_t can;
	struct ulmk_can_frame tx;
	struct ulmk_can_frame rx;
	uint32_t seq;
	int rc;

	board_services_init(info);

	tid = can_dm_init(CAN_NODE, CAN_BITRATE, 1);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("can_dm_init failed\r\n");
		ulmk_thread_exit();
	}
	if (board_devices_register_can(CAN_NODE, can_dm_ep(CAN_NODE), tid)
	    != ULMK_OK) {
		board_console_puts("register /dev/can0 failed\r\n");
		ulmk_thread_exit();
	}

	if (ulmk_open("/dev/can0", &can) != ULMK_OK) {
		board_console_puts("ulmk_open(/dev/can0) failed\r\n");
		ulmk_thread_exit();
	}

	board_console_puts("\r\n");
	board_console_puts(
		"ulmk: TC275 Lite CAN loopback DM (N0→N1 LBM, P20.8/7)\r\n");
	board_console_puts(
		"500 kbit/s, TLE9251 #NEN low, TX/RX echo @ 5 Hz\r\n");

	seq = 0u;
	tx.id = CAN_ID;
	tx.dlc = 8u;
	tx._pad[0] = 0u;
	tx._pad[1] = 0u;
	tx._pad[2] = 0u;
	for (;;) {
		tx.data[0] = (uint8_t)(seq & 0xFFu);
		tx.data[1] = (uint8_t)((seq >> 8) & 0xFFu);
		tx.data[2] = (uint8_t)((seq >> 16) & 0xFFu);
		tx.data[3] = (uint8_t)((seq >> 24) & 0xFFu);
		tx.data[4] = 0xCAu;
		tx.data[5] = 0xFEu;
		tx.data[6] = 0xBAu;
		tx.data[7] = 0xBEu;

		rc = ulmk_can_send(&can, &tx);
		if (rc != ULMK_OK) {
			board_console_printf("can_send failed rc=%d\r\n", rc);
		} else {
			print_frame("tx", &tx);
			rc = ulmk_can_recv(&can, &rx);
			if (rc < 0)
				board_console_printf(
					"can_recv failed %d\r\n", rc);
			else
				print_frame("rx", &rx);
		}
		seq++;
		board_timer_sleep_us(PERIOD_US);
	}
}
