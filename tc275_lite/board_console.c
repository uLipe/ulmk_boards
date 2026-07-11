/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/board_console.c — ASCLIN0 console IPC server (userspace).
 *
 * Every character is also appended to g_ulmk_console_log[] so HIL scripts can
 * validate output via JTAG when the DAS USB node is not a CDC VCOM.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_console.h"
#include "drivers/asclin/asclin_uart.h"
#include "drivers/port/port14_asclin0.h"

void ulmk_board_hil_mark(uint32_t n);

#define CONSOLE_MSG_PUTC	1u
#define CONSOLE_MSG_GETC	2u
#define ASCLIN_MAP_SIZE		0x100u
#define CONSOLE_LOG_SIZE	2048u

static ulmk_ep_t g_ep __attribute__((section(".user_bss")));

volatile uint32_t g_ulmk_console_log_len
	__attribute__((section(".user_bss")));
volatile char g_ulmk_console_log[CONSOLE_LOG_SIZE]
	__attribute__((section(".user_bss")));

static void console_log_putc(char c)
{
	uint32_t n;

	n = g_ulmk_console_log_len;
	if (n >= CONSOLE_LOG_SIZE - 1u)
		return;
	g_ulmk_console_log[n] = c;
	g_ulmk_console_log_len = n + 1u;
}

void board_console_putc(char c)
{
	ulmk_msg_t msg;

	msg.label    = CONSOLE_MSG_PUTC;
	msg.words[0] = (uint32_t)(uint8_t)c;
	ulmk_ep_call(g_ep, &msg);
}

void board_console_puts(const char *s)
{
	if (!s)
		return;
	while (*s)
		board_console_putc(*s++);
}

int board_console_getc(char *out)
{
	ulmk_msg_t msg;
	int rc;

	msg.label = CONSOLE_MSG_GETC;
	msg.words[0] = 0u;
	if (ulmk_ep_call(g_ep, &msg) != ULMK_OK)
		return ULMK_EINVAL;
	rc = (int)(int32_t)msg.words[0];
	if (rc == ULMK_OK && out)
		*out = (char)(uint8_t)msg.words[1];
	return rc;
}

static void console_server(void *arg)
{
	void *base;
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	char       c;
	uint8_t    b;
	int        rc;

	(void)arg;

	port14_asclin0_init();

	base = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_ASCLIN0_BASE,
			    ASCLIN_MAP_SIZE,
			    ULMK_PERM_READ | ULMK_PERM_WRITE,
			    ULMK_MMAP_PERIPH);
	if (!base)
		for (;;)
			;

	asclin_uart_init(base, 0u, 0u, ULMK_BOARD_CONSOLE_BAUD,
			 ULMK_BOARD_FA_HZ);
	ulmk_board_hil_mark(4u);

	for (;;) {
		ulmk_ep_recv(g_ep, &msg, &sender);
		reply.label = 0u;
		reply.words[0] = 0u;
		reply.words[1] = 0u;
		if (msg.label == CONSOLE_MSG_PUTC) {
			c = (char)(uint8_t)msg.words[0];
			console_log_putc(c);
			(void)asclin_uart_tx_byte(base, (uint8_t)c);
		} else if (msg.label == CONSOLE_MSG_GETC) {
			rc = asclin_uart_rx_byte_nb(base, &b);
			if (rc == 0) {
				reply.words[0] = (uint32_t)ULMK_OK;
				reply.words[1] = b;
			} else {
				reply.words[0] =
					(uint32_t)(int32_t)ULMK_ETIMEOUT;
			}
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t board_console_start(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t         tid;

	(void)info;

	g_ep = ulmk_ep_create();
	g_ulmk_console_log_len = 0u;

	attr.name       = "bcon";
	attr.entry      = console_server;
	attr.arg        = NULL;
	attr.priority   = 1u;
	attr.stack_size = 2048u;
	attr.privilege  = ULMK_PRIV_DRIVER;

	tid = ulmk_thread_create(&attr);
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	return tid;
}
