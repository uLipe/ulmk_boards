/* SPDX-License-Identifier: MIT */
/*
 * board_console.c — IPC console server over ASCLIN0 + RAM log for HIL.
 *
 * Clients format locally then ep_call WRITE(ptr,len).  The server is the
 * sole ASCLIN consumer for console traffic, so SMP lines stay atomic.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_console.h"
#include <asclin.h>

#define CONSOLE_LOG_SIZE	2048u
#define CONSOLE_ASCLIN		0u
#define CONSOLE_MSG_PUTC	1u
#define CONSOLE_MSG_WRITE	2u
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
	(void)asclin_tx_byte(CONSOLE_ASCLIN, (uint8_t)c);
}

static void console_emit_buf(const char *buf, uint32_t len)
{
	uint32_t i;

	if (!buf)
		return;
	if (len > CONSOLE_WRITE_MAX)
		len = CONSOLE_WRITE_MAX;
	for (i = 0u; i < len; i++)
		console_emit_byte(buf[i]);
}

static void console_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	const char *buf;
	uint32_t len;

	(void)arg;
	reply.label = 0u;
	reply.words[0] = 0u;

	for (;;) {
		if (ulmk_ep_recv(g_ep, &msg, &sender) != ULMK_OK)
			continue;

		if (msg.label == CONSOLE_MSG_PUTC) {
			console_emit_byte((char)(uint8_t)msg.words[0]);
		} else if (msg.label == CONSOLE_MSG_WRITE) {
			buf = (const char *)(uintptr_t)msg.words[0];
			len = msg.words[1];
			console_emit_buf(buf, len);
		}

		(void)ulmk_ep_reply(sender, &reply);
	}
}

/* ---- client ------------------------------------------------------------- */

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

	if (g_ep == ULMK_EP_INVALID)
		return;
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

/*
 * Format into a stack buffer then one WRITE — supports
 * %c %s %d %i %u %x %X %p %lu %lx %zu %%
 */
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

static void fmt_ulong(char *out, uint32_t *pos, uint32_t cap, unsigned long v)
{
	char tmp[20];
	uint32_t n = 0u;

	if (v == 0ul) {
		if (*pos < cap)
			out[(*pos)++] = '0';
		return;
	}
	while (v > 0ul && n < sizeof(tmp)) {
		tmp[n++] = (char)('0' + (int)(v % 10ul));
		v /= 10ul;
	}
	while (n > 0u) {
		if (*pos >= cap)
			return;
		out[(*pos)++] = tmp[--n];
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
				fmt_ulong(buf, &pos, CONSOLE_FMT_BUF,
					  va_arg(ap, unsigned long));
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

int board_console_getc(char *out)
{
	uint8_t b;
	int rc;

	rc = asclin_rx_byte_nb(CONSOLE_ASCLIN, &b);
	if (rc != ULMK_OK)
		return rc;
	if (out)
		*out = (char)b;
	return ULMK_OK;
}

ulmk_tid_t board_console_start(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t         asclin_tid;
	ulmk_tid_t         tid;

	(void)info;
	g_ulmk_console_log_len = 0u;
	g_ep = ULMK_EP_INVALID;

	asclin_tid = asclin_init(CONSOLE_ASCLIN, NULL,
				 ULMK_BOARD_CONSOLE_BAUD, ULMK_BOARD_FA_HZ);
	if (asclin_tid == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;

	g_ep = ulmk_ep_create();
	if (g_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	attr.name       = "bcon";
	attr.entry      = console_server;
	attr.arg        = NULL;
	attr.priority   = 1u;
	attr.stack_size = CONSOLE_STACK;
	attr.privilege  = ULMK_PRIV_DRIVER;

	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		g_ep = ULMK_EP_INVALID;
		return ULMK_TID_INVALID;
	}

	return tid;
}
