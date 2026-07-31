/* SPDX-License-Identifier: MIT */
/*
 * smp_spi_can — SPI client on CPU0, CAN client on CPU1 (cross-CPU IPC).
 *
 * Root proves both drivers once on CPU0, then pins the clients.  Sleep on
 * CPU1 needs the shared-tick wheel (timer_wheel_cpu → 0) on this SoC.
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <can.h>
#include <spi.h>
#include <gdma_axi.h>
#include "board_config.h"
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"

#define PERIOD_US	1000000u

static int spi_once(void)
{
	uint8_t tx[4] = { 0xA5u, 0x5Au, 0xC3u, 0x3Cu };
	uint8_t rx[4] = { 0 };
	int rc;
	int i;
	int ok;

	rc = spi_loopback(0u, tx, rx, 4u);
	ok = (rc == ULMK_OK);
	for (i = 0; ok && i < 4; i++) {
		if (rx[i] != tx[i])
			ok = 0;
	}
	return ok;
}

static int can_once(void)
{
	uint8_t payload[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	uint8_t rx[8] = { 0 };
	uint8_t len = 0u;
	uint32_t id = 0u;
	int rc;
	unsigned i;
	int ok;

	rc = can_send(0x100u, payload, 8u);
	(void)can_recv(&id, rx, &len);
	ok = (rc == 0) && (id == 0x100u) && (len == 8u);
	for (i = 0u; ok && i < 8u; i++)
		ok = (rx[i] == payload[i]);
	return ok;
}

static void spi_worker(void *arg)
{
	uint32_t seq = 0u;

	(void)arg;
	board_console_printf("smp_spi_can: spi on CPU%u\r\n", ulmk_cpu_id());
	/*
	 * Phase-offset vs CAN so the two clients are not mid-xfer at once
	 * (shared IRQ/GDMA paths still serialize poorly under SMP).
	 */
	board_timer_sleep_us(PERIOD_US / 2u);
	for (;;) {
		if (spi_once())
			board_console_printf(
				"smp_spi_can: SPI2: PASS loopback seq=%u\r\n",
				seq);
		else
			board_console_printf(
				"smp_spi_can: SPI2: FAIL seq=%u\r\n", seq);
		seq++;
		board_timer_sleep_us(PERIOD_US);
	}
}

static void can_worker(void *arg)
{
	uint32_t seq = 0u;

	(void)arg;
	board_console_printf("smp_spi_can: can on CPU%u\r\n", ulmk_cpu_id());
	for (;;) {
		if (can_once())
			board_console_printf(
				"smp_spi_can: tx id=0x100 PASS seq=%u\r\n",
				seq);
		else
			board_console_printf(
				"smp_spi_can: tx id=0x100 FAIL seq=%u\r\n",
				seq);
		seq++;
		board_timer_sleep_us(PERIOD_US);
	}
}

static ulmk_tid_t spawn_pinned(const char *name, void (*entry)(void *),
			       uint8_t cpu)
{
	ulmk_thread_attr_t attr = {0};

	attr.name       = name;
	attr.entry      = entry;
	attr.arg        = NULL;
	attr.priority   = 3u;
	attr.stack_size = 3072u;
	attr.privilege  = ULMK_PRIV_DRIVER;
	attr.heap_size  = 0u;
	attr.cpu        = cpu;
	return ulmk_thread_create(&attr);
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	board_services_init(info);

	board_console_puts("\r\n");
	board_console_puts("ulmk: smp_spi_can (SPI CPU0 / CAN CPU1)\r\n");

	if (ulmk_cpu_id() != 0u || (uint32_t)ULMK_ARCH_NUM_CPU < 2u) {
		board_console_puts("smp_spi_can: FAIL cpu setup\r\n");
		ulmk_thread_exit();
	}

	if (gdma_axi_init(0u) == ULMK_TID_INVALID ||
	    spi_init(0u) == ULMK_TID_INVALID ||
	    can_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("smp_spi_can: FAIL driver init\r\n");
		ulmk_thread_exit();
	}

	if (!spi_once()) {
		board_console_puts("smp_spi_can: FAIL spi smoke\r\n");
		ulmk_thread_exit();
	}
	board_console_puts("smp_spi_can: SPI2: PASS smoke\r\n");

	if (!can_once()) {
		board_console_puts("smp_spi_can: FAIL can smoke\r\n");
		ulmk_thread_exit();
	}
	board_console_puts("smp_spi_can: tx id=0x100 PASS smoke\r\n");

	if (spawn_pinned("spi_w", spi_worker, 0u) == ULMK_TID_INVALID ||
	    spawn_pinned("can_w", can_worker, 1u) == ULMK_TID_INVALID) {
		board_console_puts("smp_spi_can: FAIL spawn\r\n");
		ulmk_thread_exit();
	}

	board_console_puts("smp_spi_can: ready\r\n");
	ulmk_thread_exit();
}
