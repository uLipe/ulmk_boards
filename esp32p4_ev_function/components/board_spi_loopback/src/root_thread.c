/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <spi.h>
#include <gdma_axi.h>
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"

static int test_instance(uint8_t n)
{
	uint8_t tx[4] = { 0xA5u, 0x5Au, 0xC3u, 0x3Cu };
	uint8_t rx[4] = { 0 };
	int rc;
	int i;
	int ok;

	board_console_printf("SPI%u loopback\r\n", n + 2u);
	rc = spi_loopback(n, tx, rx, 4u);
	ok = (rc == ULMK_OK);
	for (i = 0; ok && i < 4; i++) {
		if (rx[i] != tx[i])
			ok = 0;
	}
	if (ok)
		board_console_printf(
			"SPI%u: PASS loopback %02x %02x %02x %02x\r\n",
			n + 2u, rx[0], rx[1], rx[2], rx[3]);
	else {
		board_console_printf(
			"SPI%u: FAIL rc=%d rx=%02x %02x %02x %02x\r\n",
			n + 2u, rc, rx[0], rx[1], rx[2], rx[3]);
	}
	return ok;
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	int ok2;
	int ok3;

	board_services_init(info);
	if (gdma_axi_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("SPI: FAIL gdma_axi init\r\n");
		for (;;)
			board_timer_sleep_us(1000000u);
	}
	if (spi_init(0u) == ULMK_TID_INVALID ||
	    spi_init(1u) == ULMK_TID_INVALID) {
		board_console_puts("SPI: FAIL init\r\n");
		for (;;)
			board_timer_sleep_us(1000000u);
	}

	ok2 = test_instance(0u);
	ok3 = test_instance(1u);
	if (ok2 && ok3)
		board_console_puts("SPI: PASS both instances\r\n");
	else
		board_console_puts("SPI: FAIL instances\r\n");

	for (;;)
		board_timer_sleep_us(1000000u);
}
