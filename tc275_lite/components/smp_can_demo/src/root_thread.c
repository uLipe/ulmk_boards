/* SPDX-License-Identifier: MIT */
/*
 * smp_can_demo — SMP CAN loopback + dual LED traffic markers (TC275 Lite).
 *
 * CPU0 TX thread: send frames, toggle LED1, print affinity.
 * CPU1 RX thread: recv/decode frames, toggle LED2, print affinity.
 *
 * The CAN server processes one ep_call at a time and blocks inside RECV on
 * ulmk_notif_wait.  Crossing a concurrent SEND creates a deadlock, so the
 * demo posts/consumes one frame at a time via shared domain counters.
 *
 * Build: --enable-smp --component smp_can_demo
 * UART:  often /dev/ttyUSB1 on kits that also expose an ESP adapter on USB0.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <can.h>
#include "board_config.h"
#include "board_console.h"
#include "board_leds.h"

void board_services_init(const ulmk_boot_info_t *info);
void board_timer_sleep_us(uint32_t us);

#define CAN_NODE		0u
#define CAN_BITRATE		500000u
#define CAN_ID			0x321u
#define PERIOD_US		200000u	/* 5 Hz */

static ULMK_PRIVATE volatile uint32_t g_posted;
static ULMK_PRIVATE volatile uint32_t g_taken;

static void thr_tx_cpu0(void *arg)
{
	can_frame_t tx;
	uint32_t seq;
	int rc;

	(void)arg;

	board_console_printf("smp_can_demo: TX thread on CPU%u (LED1)\r\n",
			     ulmk_cpu_id());

	seq = 0u;
	tx.id = CAN_ID;
	tx.dlc = 8u;
	for (;;) {
		while (g_taken != g_posted)
			ulmk_thread_yield();

		tx.data[0] = (uint8_t)(seq & 0xFFu);
		tx.data[1] = (uint8_t)((seq >> 8) & 0xFFu);
		tx.data[2] = (uint8_t)((seq >> 16) & 0xFFu);
		tx.data[3] = (uint8_t)((seq >> 24) & 0xFFu);
		tx.data[4] = 0xC0u;
		tx.data[5] = 0xCAu;
		tx.data[6] = 0xFEu;
		tx.data[7] = (uint8_t)ulmk_cpu_id();

		rc = can_send(CAN_NODE, &tx);
		if (rc != ULMK_OK) {
			board_console_printf(
				"smp_can_demo: can_send rc=%d\r\n", rc);
		} else {
			g_posted++;
			board_console_printf(
				"smp_can_demo: CPU0 TX id=0x%X dlc=%u data=%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
				tx.id, (uint32_t)tx.dlc,
				(uint32_t)tx.data[0], (uint32_t)tx.data[1],
				(uint32_t)tx.data[2], (uint32_t)tx.data[3],
				(uint32_t)tx.data[4], (uint32_t)tx.data[5],
				(uint32_t)tx.data[6], (uint32_t)tx.data[7]);
			(void)board_leds_toggle(BOARD_LED_1);
		}
		seq++;
		board_timer_sleep_us(PERIOD_US);
	}
}

static void thr_rx_cpu1(void *arg)
{
	can_frame_t rx;
	uint32_t expect;
	uint32_t seq;
	int rc;

	(void)arg;

	board_console_printf("smp_can_demo: RX thread on CPU%u (LED2)\r\n",
			     ulmk_cpu_id());

	expect = 0u;
	for (;;) {
		while (g_posted == expect)
			ulmk_thread_yield();

		rc = can_recv(CAN_NODE, &rx);
		if (rc != ULMK_OK) {
			board_console_printf(
				"smp_can_demo: can_recv rc=%d\r\n", rc);
			continue;
		}

		expect++;
		g_taken = expect;

		board_console_printf(
			"smp_can_demo: CPU1 RX id=0x%X dlc=%u data=%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
			rx.id, (uint32_t)rx.dlc,
			(uint32_t)rx.data[0], (uint32_t)rx.data[1],
			(uint32_t)rx.data[2], (uint32_t)rx.data[3],
			(uint32_t)rx.data[4], (uint32_t)rx.data[5],
			(uint32_t)rx.data[6], (uint32_t)rx.data[7]);

		if (rx.id == CAN_ID && rx.dlc >= 8u) {
			seq = (uint32_t)rx.data[0] |
			      ((uint32_t)rx.data[1] << 8) |
			      ((uint32_t)rx.data[2] << 16) |
			      ((uint32_t)rx.data[3] << 24);
			board_console_printf(
				"smp_can_demo: CPU1 decoded seq=%u src_cpu=%u\r\n",
				seq, (uint32_t)rx.data[7]);
		}
		(void)board_leds_toggle(BOARD_LED_2);
	}
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr;
	ulmk_tid_t tid;

	board_services_init(info);

	board_console_puts("\r\n");
	board_console_puts("ulmk: smp_can_demo (CPU0 TX / CPU1 RX, LBM)\r\n");

	if (ulmk_cpu_id() != 0u) {
		board_console_puts("smp_can_demo: FAIL root not on CPU0\r\n");
		ulmk_thread_exit();
	}

	tid = can_init(CAN_NODE, CAN_BITRATE, 1);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("smp_can_demo: can_init failed\r\n");
		ulmk_thread_exit();
	}

	(void)board_leds_set(BOARD_LED_1, 0);
	(void)board_leds_set(BOARD_LED_2, 0);
	g_posted = 0u;
	g_taken = 0u;

	attr.name       = "can_tx0";
	attr.entry      = thr_tx_cpu0;
	attr.arg        = NULL;
	attr.priority   = 6u;
	attr.stack_size = 2048u;
	attr.privilege  = ULMK_PRIV_DRIVER;
	attr.heap_size  = 0u;
	attr.cpu        = 0u;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("smp_can_demo: FAIL spawn TX\r\n");
		ulmk_thread_exit();
	}

	attr.name       = "can_rx1";
	attr.entry      = thr_rx_cpu1;
	attr.arg        = NULL;
	attr.priority   = 5u;
	attr.stack_size = 2048u;
	attr.privilege  = ULMK_PRIV_DRIVER;
	attr.heap_size  = 0u;
	attr.cpu        = 1u;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("smp_can_demo: FAIL spawn RX\r\n");
		ulmk_thread_exit();
	}

	board_console_puts(
		"smp_can_demo: workers spawned — LED1=TX LED2=RX\r\n");
	ulmk_thread_exit();
}
