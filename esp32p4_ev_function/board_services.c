/* SPDX-License-Identifier: MIT */
/*
 * Board services — console, timer, pinmux, gpio.
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_services.h"
#include "board_timer.h"
#include "board_internal.h"
#include "board_psram.h"
#include "board_cpu_clk.h"
#include "board_config.h"
#include <uart.h>
#include <pinmux.h>
#include <gpio.h>

#define CONSOLE_MSG_PUTC		1u
#define CONSOLE_MSG_WRITE		2u
#define CONSOLE_WRITE_MAX		256u

#define UART0_FIFO		(ULMK_BOARD_UART0_BASE + 0x00u)
#define UART0_STATUS		(ULMK_BOARD_UART0_BASE + 0x1cu)
#define UART0_TXFIFO_SHIFT	16
#define UART0_TXFIFO_MASK	0xFFu
#define UART0_TXFIFO_DEPTH	128u
#define UART0_TX_SPIN_LIMIT	200000u

static ulmk_ep_t g_ep __attribute__((section(".user_bss")));

ulmk_ep_t board_service_ep(void)
{
	return g_ep;
}

/*
 * Polled TX for the console server only.  Must not ep_call the UART driver:
 * root → console → uart → TXFIFO notif deadlocks once the FIFO fills.
 * Same MMIO recipe as board_printk.c, kept local so DRIVER code does not
 * call the kernel printk hook by name.
 */
static void console_putc_hw(char c)
{
	uint32_t spins;

	if (c == '\n')
		console_putc_hw('\r');

	spins = 0u;
	while (((*(volatile uint32_t *)(uintptr_t)UART0_STATUS >>
		 UART0_TXFIFO_SHIFT) & UART0_TXFIFO_MASK) >= UART0_TXFIFO_DEPTH) {
		if (++spins >= UART0_TX_SPIN_LIMIT)
			return;
	}
	*(volatile uint32_t *)(uintptr_t)UART0_FIFO = (uint32_t)(uint8_t)c;
}

static void board_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_msg_t next;
	ulmk_tid_t sender;
	ulmk_tid_t next_sender;
	int rc;

	(void)arg;
	reply.label    = 0u;
	reply.words[0] = 0u;

	/*
	 * reply_recv keeps the server on the recv path in the same syscall as
	 * the reply.  A separate reply+recv lets a higher-prio caller (root)
	 * run in the gap and block on an empty recv_queue — then the console
	 * chain wedges after SPI / any burst of prints.
	 */
	rc = ulmk_ep_recv(g_ep, &msg, &sender);
	if (rc != ULMK_OK)
		return;

	for (;;) {
		if (msg.label == CONSOLE_MSG_PUTC) {
			console_putc_hw((char)(uint8_t)msg.words[0]);
		} else if (msg.label == CONSOLE_MSG_WRITE) {
			const char *buf =
				(const char *)(uintptr_t)msg.words[0];
			uint32_t len = msg.words[1];
			uint32_t i;

			if (buf && len > 0u) {
				if (len > CONSOLE_WRITE_MAX)
					len = CONSOLE_WRITE_MAX;
				for (i = 0u; i < len; i++)
					console_putc_hw(buf[i]);
			}
		}
		rc = ulmk_ep_reply_recv(g_ep, sender, &reply, &next,
					&next_sender);
		if (rc != ULMK_OK)
			continue;
		msg = next;
		sender = next_sender;
	}
}

void board_services_init(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t         tid;

	(void)info;

	/*
	 * Bootloader leaves ~90 MHz.  Raise CPLL→400 MHz before anything
	 * CPU-bound (PSRAM probe, LVGL SW render) runs.
	 */
	(void)board_cpu_clk_set_400m();

	g_ep = ulmk_ep_create();

	attr.name       = "bsvc";
	attr.entry      = board_server;
	attr.priority   = 1u;
	attr.stack_size = 4096u;
	attr.privilege  = ULMK_PRIV_DRIVER;

	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID)
		return;

	(void)board_timer_start(info);
	(void)pinmux_init(0u);
	(void)gpio_init(0u);
	/*
	 * PSRAM after pinmux/gpio — MSPI/AXI only here (not GPSPI).
	 * Display demos that need steady DPI may skip if axi glitches;
	 * baseline board_services always attempt bring-up.
	 */
	(void)board_psram_init();

	/*
	 * Still bring up the UART0 driver for apps that use uart_* directly.
	 * Console TX itself stays polled MMIO (see console_putc_hw) so it
	 * never nests into that server.
	 */
	(void)uart_init(0u, NULL, 0u, 0u);
	/* Do not touch LED1 here — it aliases the backlight PWM pad. */
}
