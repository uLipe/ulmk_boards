/* SPDX-License-Identifier: MIT */
/*
 * board_can_loopback — MultiCAN node 0 internal loopback @ 5 Hz.
 *
 * Uses the Lite Kit TLE9251 pins (P20.8/P20.7) + #NEN (P20.6) so the
 * same wiring works on a real bus after clearing loopback.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <can.h>
#include "board_config.h"

void board_services_init(const ulmk_boot_info_t *info);
void board_console_puts(const char *s);
void board_console_putc(char c);
void board_timer_sleep_us(uint32_t us);

#define CAN_NODE		0u
#define CAN_BITRATE		500000u
#define CAN_ID		0x321u
#define PERIOD_US		200000u	/* 5 Hz */

static void put_u32(uint32_t v)
{
	char buf[10];
	uint32_t i = 0u;
	uint32_t n = v;

	if (n == 0u) {
		board_console_putc('0');
		return;
	}
	while (n > 0u && i < sizeof(buf)) {
		buf[i++] = (char)('0' + (n % 10u));
		n /= 10u;
	}
	while (i > 0u)
		board_console_putc(buf[--i]);
}

static void put_hex(uint32_t v, uint8_t nibbles)
{
	uint8_t i;
	uint8_t n;

	for (i = nibbles; i > 0u; i--) {
		n = (uint8_t)((v >> ((i - 1u) * 4u)) & 0xFu);
		board_console_putc((char)(n < 10u ? ('0' + n) : ('A' + (n - 10u))));
	}
}

static void print_frame(const char *tag, const can_frame_t *f)
{
	uint8_t i;

	board_console_puts(tag);
	board_console_puts(" id=0x");
	put_hex(f->id, 3u);
	board_console_puts(" dlc=");
	put_u32(f->dlc);
	board_console_puts(" data=");
	for (i = 0u; i < f->dlc && i < 8u; i++) {
		put_hex(f->data[i], 2u);
		if (i + 1u < f->dlc)
			board_console_putc(' ');
	}
	board_console_puts("\r\n");
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
	board_console_puts("ulmk: TC275 Lite CAN loopback (N0→N1 LBM, P20.8/7)\r\n");
	board_console_puts("500 kbit/s, TLE9251 #NEN low, TX/RX echo @ 5 Hz\r\n");

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
			board_console_puts("can_send failed rc=");
			put_u32((uint32_t)(int32_t)rc);
			board_console_puts(" diag=0x");
			put_hex(can_last_diag(), 8u);
			board_console_puts("\r\n");
		} else {
			print_frame("tx", &tx);
			rc = can_recv(CAN_NODE, &rx);
			if (rc != ULMK_OK) {
				board_console_puts("can_recv failed ");
				put_u32((uint32_t)(int32_t)rc);
				board_console_puts("\r\n");
			} else {
				print_frame("rx", &rx);
			}
		}
		seq++;
		board_timer_sleep_us(PERIOD_US);
	}
}
