/* SPDX-License-Identifier: MIT */
/*
 * board_i2c_scanner — Zephyr-style 7-bit I2C address scan on I2C0.
 *
 * Probes 0x01..0x7F with an address-only write (ACK = device present).
 * Lite Kit pins: P15.4 SCL / P15.5 SDA (X1); needs bus pull-ups.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <i2c.h>
#include "board_config.h"

void board_services_init(const ulmk_boot_info_t *info);
void board_console_puts(const char *s);
void board_console_putc(char c);

#define I2C_NODE	0u

static void put_hex2(uint8_t v)
{
	uint8_t n;

	n = (uint8_t)(v >> 4);
	board_console_putc((char)(n < 10u ? ('0' + n) : ('a' + (n - 10u))));
	n = (uint8_t)(v & 0xFu);
	board_console_putc((char)(n < 10u ? ('0' + n) : ('a' + (n - 10u))));
}

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

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_tid_t tid;
	i2c_pins_t pins;
	uint8_t addr;
	uint8_t col;
	uint32_t found;
	int rc;

	board_services_init(info);

	pins.scl_port = ULMK_BOARD_I2C0_SCL_PORT;
	pins.scl_pin = ULMK_BOARD_I2C0_SCL_PIN;
	pins.scl_alt = ULMK_BOARD_I2C0_SCL_ALT;
	pins.sda_port = ULMK_BOARD_I2C0_SDA_PORT;
	pins.sda_pin = ULMK_BOARD_I2C0_SDA_PIN;
	pins.sda_alt = ULMK_BOARD_I2C0_SDA_ALT;
	pins.pisel = ULMK_BOARD_I2C0_PISEL;

	board_console_puts("\r\n");
	board_console_puts("ulmk: TC275 Lite I2C scanner (I2C0 P15.4/P15.5)\r\n");

	tid = i2c_init(I2C_NODE, &pins, ULMK_BOARD_I2C0_BITRATE_HZ);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("i2c_init failed\r\n");
		ulmk_thread_exit();
	}

	board_console_puts("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");

	found = 0u;
	for (addr = 0u; addr < 0x80u; addr++) {
		col = (uint8_t)(addr & 0xFu);
		if (col == 0u) {
			put_hex2(addr);
			board_console_puts(": ");
		}

		if (addr == 0u) {
			/* General call — skip */
			board_console_puts("   ");
		} else {
			rc = i2c_probe(I2C_NODE, addr);
			if (rc == 0) {
				put_hex2(addr);
				board_console_putc(' ');
				found++;
			} else if (rc == ULMK_ETIMEOUT) {
				board_console_puts("TO ");
			} else {
				/* NACK / no device */
				board_console_puts("-- ");
			}
		}

		if (col == 0xFu)
			board_console_puts("\r\n");
	}

	board_console_puts("found ");
	put_u32(found);
	board_console_puts(" device(s)\r\n");

	for (;;)
		ulmk_thread_yield();
}
