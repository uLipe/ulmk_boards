/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <spi.h>
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	uint8_t tx[4] = { 0xA5u, 0x5Au, 0xC3u, 0x3Cu };
	uint8_t rx[4] = { 0 };
	int rc;
	int i;
	int ok;

	board_services_init(info);
	if (spi_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("SPI: FAIL init\r\n");
		for (;;)
			board_timer_sleep_us(1000000u);
	}

	board_console_puts("SPI GPSPI2 loopback\r\n");
	rc = spi_loopback(0u, tx, rx, 4u);
	ok = (rc == ULMK_OK);
	for (i = 0; ok && i < 4; i++) {
		if (rx[i] != tx[i])
			ok = 0;
	}
	if (ok)
		board_console_printf(
			"SPI: PASS loopback %02x %02x %02x %02x\r\n",
			rx[0], rx[1], rx[2], rx[3]);
	else
		board_console_printf(
			"SPI: FAIL rc=%d rx=%02x %02x %02x %02x\r\n",
			rc, rx[0], rx[1], rx[2], rx[3]);

	for (;;)
		board_timer_sleep_us(1000000u);
}
