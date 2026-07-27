/* SPDX-License-Identifier: MIT */
/*
 * board_console.c — IPC console over SEGGER RTT (ch0) + RAM log.
 * No UART required — use JLinkRTTClient / hil-rtt-capture.sh.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_console.h"
#include "board_rtt.h"

#define CONSOLE_LOG_SIZE	2048u
#define CONSOLE_MSG_PUTC	1u
#define CONSOLE_MSG_WRITE	2u
#define CONSOLE_MSG_GETC	3u
#define CONSOLE_WRITE_MAX	256u
#define CONSOLE_FMT_BUF		160u
#define CONSOLE_STACK		2048u

volatile uint32_t g_ulmk_console_log_len
	__attribute__((section(".user_bss")));
volatile char g_ulmk_console_log[CONSOLE_LOG_SIZE]
	__attribute__((section(".user_bss")));

static ulmk_ep_t g_ep __attribute__((section(".user_bss")));

static void console_log_putc(char c)
{
	uint32_t n;

	n = g_ulmk_console_log_len;
	if (n >= CONSOLE_LOG_SIZE - 1u)
		return;
	g_ulmk_console_log[n] = c;
	g_ulmk_console_log_len = n + 1u;
}

static void console_emit_byte(char c)
{
	console_log_putc(c);
	board_rtt_putc(c);
}

static void console_emit_buf(const char *buf, uint32_t len)
{
	uint32_t i;

	if (!buf || len == 0u)
		return;
	if (len > CONSOLE_WRITE_MAX)
		len = CONSOLE_WRITE_MAX;
	for (i = 0u; i < len; i++)
		console_log_putc(buf[i]);
	board_rtt_write(buf, len);
}

static void console_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	const char *buf;
	uint32_t len;
	char c;

	(void)arg;
	board_rtt_init();
	reply.label = 0u;
	reply.words[0] = 0u;

	for (;;) {
		if (ulmk_ep_recv(g_ep, &msg, &sender) != ULMK_OK)
			continue;

		if (msg.label == CONSOLE_MSG_PUTC) {
			console_emit_byte((char)(uint8_t)msg.words[0]);
			reply.words[0] = (uint32_t)ULMK_OK;
		} else if (msg.label == CONSOLE_MSG_WRITE) {
			buf = (const char *)(uintptr_t)msg.words[0];
			len = msg.words[1];
			console_emit_buf(buf, len);
			reply.words[0] = (uint32_t)ULMK_OK;
		} else if (msg.label == CONSOLE_MSG_GETC) {
			if (board_rtt_getc(&c) == 0) {
				reply.words[0] = (uint32_t)ULMK_OK;
				reply.words[1] = (uint32_t)(uint8_t)c;
			} else {
				reply.words[0] = (uint32_t)ULMK_ETIMEOUT;
				reply.words[1] = 0u;
			}
		} else {
			reply.words[0] = (uint32_t)ULMK_EINVAL;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

static void console_write(const char *buf, uint32_t len)
{
	ulmk_msg_t msg;

	if (!buf || len == 0u || g_ep == ULMK_EP_INVALID)
		return;
	if (len > CONSOLE_WRITE_MAX)
		len = CONSOLE_WRITE_MAX;
	msg.label    = CONSOLE_MSG_WRITE;
	msg.words[0] = (uint32_t)(uintptr_t)buf;
	msg.words[1] = len;
	(void)ulmk_ep_call(g_ep, &msg);
}

void board_console_putc(char c)
{
	ulmk_msg_t msg;

	msg.label    = CONSOLE_MSG_PUTC;
	msg.words[0] = (uint32_t)(uint8_t)c;
	(void)ulmk_ep_call(g_ep, &msg);
}

void board_console_puts(const char *s)
{
	uint32_t len;

	if (!s)
		return;
	len = 0u;
	while (s[len] != '\0' && len < CONSOLE_WRITE_MAX)
		len++;
	console_write(s, len);
}

int board_console_getc(char *out)
{
	ulmk_msg_t msg;
	int rc;

	if (!out || g_ep == ULMK_EP_INVALID)
		return ULMK_EINVAL;
	msg.label = CONSOLE_MSG_GETC;
	rc = ulmk_ep_call(g_ep, &msg);
	if (rc != ULMK_OK)
		return rc;
	if ((int)(int32_t)msg.words[0] != ULMK_OK)
		return (int)(int32_t)msg.words[0];
	*out = (char)(uint8_t)msg.words[1];
	return ULMK_OK;
}

static void fmt_u32(char *out, uint32_t *pos, uint32_t cap, uint32_t v)
{
	char tmp[10];
	uint32_t n = 0u;

	if (v == 0u) {
		if (*pos < cap)
			out[(*pos)++] = '0';
		return;
	}
	while (v > 0u && n < sizeof(tmp)) {
		tmp[n++] = (char)('0' + (v % 10u));
		v /= 10u;
	}
	while (n > 0u) {
		if (*pos >= cap)
			return;
		out[(*pos)++] = tmp[--n];
	}
}

static void fmt_i32(char *out, uint32_t *pos, uint32_t cap, int32_t v)
{
	if (v < 0) {
		if (*pos < cap)
			out[(*pos)++] = '-';
		fmt_u32(out, pos, cap, (uint32_t)(-(v + 1)) + 1u);
	} else {
		fmt_u32(out, pos, cap, (uint32_t)v);
	}
}

static void fmt_hex(char *out, uint32_t *pos, uint32_t cap,
		    unsigned long v, int upper)
{
	static const char lo[] = "0123456789abcdef";
	static const char hi[] = "0123456789ABCDEF";
	const char *digits = upper ? hi : lo;
	char tmp[16];
	uint32_t n = 0u;

	if (v == 0ul) {
		if (*pos < cap)
			out[(*pos)++] = '0';
		return;
	}
	while (v > 0ul && n < sizeof(tmp)) {
		tmp[n++] = digits[v & 0xful];
		v >>= 4;
	}
	while (n > 0u) {
		if (*pos >= cap)
			return;
		out[(*pos)++] = tmp[--n];
	}
}

static void fmt_str(char *out, uint32_t *pos, uint32_t cap, const char *s)
{
	if (!s)
		s = "(null)";
	while (*s) {
		if (*pos >= cap)
			return;
		out[(*pos)++] = *s++;
	}
}

void board_console_printf(const char *fmt, ...)
{
	char buf[CONSOLE_FMT_BUF];
	uint32_t pos = 0u;
	va_list ap;
	int is_long;

	if (!fmt)
		return;

	va_start(ap, fmt);
	while (*fmt && pos < CONSOLE_FMT_BUF) {
		if (*fmt != '%') {
			buf[pos++] = *fmt++;
			continue;
		}
		fmt++;
		is_long = 0;
		while (*fmt == '0')
			fmt++;
		while (*fmt >= '1' && *fmt <= '9')
			fmt++;
		if (*fmt == 'l') {
			is_long = 1;
			fmt++;
		} else if (*fmt == 'z') {
			is_long = (sizeof(size_t) > sizeof(uint32_t)) ? 1 : 0;
			fmt++;
		}

		switch (*fmt++) {
		case 'c':
			if (pos < CONSOLE_FMT_BUF)
				buf[pos++] = (char)va_arg(ap, int);
			break;
		case 's':
			fmt_str(buf, &pos, CONSOLE_FMT_BUF,
				va_arg(ap, const char *));
			break;
		case 'd':
		case 'i':
			if (is_long)
				fmt_i32(buf, &pos, CONSOLE_FMT_BUF,
					(int32_t)va_arg(ap, long));
			else
				fmt_i32(buf, &pos, CONSOLE_FMT_BUF,
					va_arg(ap, int32_t));
			break;
		case 'u':
			if (is_long)
				fmt_u32(buf, &pos, CONSOLE_FMT_BUF,
					(uint32_t)va_arg(ap, unsigned long));
			else
				fmt_u32(buf, &pos, CONSOLE_FMT_BUF,
					va_arg(ap, uint32_t));
			break;
		case 'x':
			if (is_long)
				fmt_hex(buf, &pos, CONSOLE_FMT_BUF,
					va_arg(ap, unsigned long), 0);
			else
				fmt_hex(buf, &pos, CONSOLE_FMT_BUF,
					(unsigned long)va_arg(ap, uint32_t), 0);
			break;
		case 'X':
			if (is_long)
				fmt_hex(buf, &pos, CONSOLE_FMT_BUF,
					va_arg(ap, unsigned long), 1);
			else
				fmt_hex(buf, &pos, CONSOLE_FMT_BUF,
					(unsigned long)va_arg(ap, uint32_t), 1);
			break;
		case 'p':
			if (pos < CONSOLE_FMT_BUF)
				buf[pos++] = '0';
			if (pos < CONSOLE_FMT_BUF)
				buf[pos++] = 'x';
			fmt_hex(buf, &pos, CONSOLE_FMT_BUF,
				(unsigned long)(uintptr_t)va_arg(ap, void *),
				0);
			break;
		case '%':
			if (pos < CONSOLE_FMT_BUF)
				buf[pos++] = '%';
			break;
		default:
			if (pos < CONSOLE_FMT_BUF)
				buf[pos++] = '?';
			break;
		}
	}
	va_end(ap);
	console_write(buf, pos);
}

ulmk_tid_t board_console_start(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	(void)info;

	board_rtt_init();

	g_ep = ulmk_ep_create();
	if (g_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	attr.name       = "console";
	attr.entry      = console_server;
	attr.priority   = 1u;
	attr.stack_size = CONSOLE_STACK;
	attr.privilege  = ULMK_PRIV_DRIVER;

	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;
	return tid;
}
