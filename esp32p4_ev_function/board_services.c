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
#include <uart.h>
#include <pinmux.h>
#include <gpio.h>

#define CONSOLE_MSG_PUTC		1u
#define CONSOLE_MSG_WRITE		2u
#define CONSOLE_WRITE_MAX		256u

extern void ulmk_printk_char_out(char c);

static ulmk_ep_t g_ep __attribute__((section(".user_bss")));
static uint8_t g_uart0_ok __attribute__((section(".user_bss")));

ulmk_ep_t board_service_ep(void)
{
	return g_ep;
}

static void console_putc_hw(char c)
{
	if (g_uart0_ok) {
		if (c == '\n')
			(void)uart_tx_byte(0u, (uint8_t)'\r');
		(void)uart_tx_byte(0u, (uint8_t)c);
		return;
	}
	/*
	 * Driver not up yet: fall back to the polled FIFO writer rather than
	 * the ROM console, which is not reentrant across threads.
	 */
	ulmk_printk_char_out(c);
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
	 * Console handover last: driver servers must be spawned from the root
	 * thread, and PSRAM bring-up calls ROM routines that fault if this
	 * runs before it.  pins/baud left at 0 adopts the mux and divider the
	 * bootloader set up, so the link does not change mid-boot; if
	 * bring-up fails the ROM path stays rather than leaving the board
	 * mute.
	 */
	g_uart0_ok = (uart_init(0u, NULL, 0u, 0u) != ULMK_TID_INVALID) ?
		     1u : 0u;
	/* Do not touch LED1 here — it aliases the backlight PWM pad. */
}
