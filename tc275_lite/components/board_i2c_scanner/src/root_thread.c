/* SPDX-License-Identifier: MIT */
/*
 * board_i2c_scanner — Zephyr-style 7-bit I2C address scan on I2C0.
 *
 * Probes 0x01..0x7F with an address-only write (ACK = device present).
 * Transfer deadline is policy here (board_timer), not in the I2C driver:
 * a worker thread runs i2c_probe; the scanner races it against a timeout.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <i2c.h>
#include "board_config.h"

void board_services_init(const ulmk_boot_info_t *info);
void board_console_puts(const char *s);
void board_console_putc(char c);
void board_timer_sleep_us(uint32_t us);

#define I2C_NODE		0u
#define PROBE_TIMEOUT_US	5000u
#define PROBE_POLL_US		200u
#define WORKER_STACK		1024u

static volatile uint8_t g_req_addr __attribute__((section(".user_bss")));
static volatile int g_req_pending __attribute__((section(".user_bss")));
static volatile int g_req_done __attribute__((section(".user_bss")));
static volatile int g_req_rc __attribute__((section(".user_bss")));
static ulmk_notif_t g_req_notif __attribute__((section(".user_bss")));

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

static void probe_worker(void *arg)
{
	uint32_t bits;

	(void)arg;
	for (;;) {
		bits = 0u;
		if (ulmk_notif_wait(g_req_notif, 1u, &bits) != ULMK_OK)
			continue;
		g_req_rc = i2c_probe(I2C_NODE, g_req_addr);
		g_req_pending = 0;
		g_req_done = 1;
	}
}

static int probe_with_timeout(uint8_t addr)
{
	uint32_t waited;

	g_req_done = 0;
	g_req_pending = 1;
	g_req_addr = addr;
	(void)ulmk_notif_signal(g_req_notif, 1u);

	waited = 0u;
	while (!g_req_done && waited < PROBE_TIMEOUT_US) {
		board_timer_sleep_us(PROBE_POLL_US);
		waited += PROBE_POLL_US;
	}
	if (!g_req_done)
		return ULMK_ETIMEOUT;
	return g_req_rc;
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;
	uint8_t addr;
	uint8_t col;
	uint32_t found;
	int rc;
	int bus_dead;

	board_services_init(info);

	board_console_puts("\r\n");
	board_console_puts("ulmk: TC275 Lite I2C scanner (I2C0 P15.4/P15.5)\r\n");

	tid = i2c_init(I2C_NODE, ULMK_BOARD_I2C0_BITRATE_HZ);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("i2c_init failed\r\n");
		ulmk_thread_exit();
	}

	g_req_notif = ulmk_notif_create();
	if (g_req_notif == ULMK_NOTIF_INVALID) {
		board_console_puts("notif_create failed\r\n");
		ulmk_thread_exit();
	}

	attr.name       = "i2c_probe";
	attr.entry      = probe_worker;
	attr.arg        = NULL;
	attr.priority   = 4u;
	attr.stack_size = WORKER_STACK;
	attr.privilege  = ULMK_PRIV_USER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("probe worker failed\r\n");
		ulmk_thread_exit();
	}

	board_console_puts("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");

	found = 0u;
	bus_dead = 0;
	for (addr = 0u; addr < 0x80u; addr++) {
		col = (uint8_t)(addr & 0xFu);
		if (col == 0u) {
			put_hex2(addr);
			board_console_puts(": ");
		}

		if (addr == 0u) {
			board_console_puts("   ");
		} else if (bus_dead) {
			board_console_puts("TO ");
		} else {
			rc = probe_with_timeout(addr);
			if (rc == 0) {
				put_hex2(addr);
				board_console_putc(' ');
				found++;
			} else if (rc == ULMK_ETIMEOUT) {
				board_console_puts("TO ");
				/*
				 * Worker is blocked in the I2C server; further
				 * probes cannot complete until the bus wakes.
				 */
				bus_dead = 1;
			} else {
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
