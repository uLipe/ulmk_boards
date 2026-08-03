/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <i2c.h>
#include "touch.h"
#include "touch_internal.h"
#include "board_console.h"

#define GT911_ADDR_A	0x5Du
#define GT911_ADDR_B	0x14u

ulmk_ep_t g_touch_eps[TOUCH_MAX_INST];

static int g_have_gt911;
static uint8_t g_gt911_addr;

static int gt911_read(int *x, int *y, int *pressed);

static void touch_server(void *arg)
{
	ulmk_ep_t ep = g_touch_eps[0];
	ulmk_msg_t msg, reply;
	ulmk_tid_t sender;
	int x, y, pressed;
	int rc;

	(void)arg;
	for (;;) {
		if (ulmk_ep_recv(ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		reply.words[1] = 0u;
		reply.words[2] = 0u;
		reply.words[3] = 0u;
		reply.words[4] = 0u;
		reply.words[5] = 0u;

		switch (msg.label) {
		case TOUCH_MSG_READ:
			rc = gt911_read(&x, &y, &pressed);
			reply.words[0] = (uint32_t)rc;
			reply.words[1] = (uint32_t)x;
			reply.words[2] = (uint32_t)y;
			reply.words[3] = (uint32_t)pressed;
			break;
		default:
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_ep_t touch_ep(void)
{
	return g_touch_eps[0];
}

ulmk_tid_t touch_init(uint8_t n)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	(void)n;
	if (i2c_init(0u) == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;

	g_gt911_addr = GT911_ADDR_A;
	if (i2c_probe(GT911_ADDR_A) == ULMK_OK) {
		g_have_gt911 = 1;
	} else if (i2c_probe(GT911_ADDR_B) == ULMK_OK) {
		g_gt911_addr = GT911_ADDR_B;
		g_have_gt911 = 1;
	} else {
		g_have_gt911 = 0;
	}

	if (g_have_gt911)
		board_console_printf("touch gt911 ok @0x%02x\r\n",
				     g_gt911_addr);
	else
		board_console_puts("touch gt911 absent\r\n");

	g_touch_eps[0] = ulmk_ep_create();
	if (g_touch_eps[0] == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	attr.name = "touch";
	attr.entry = touch_server;
	attr.priority = 2u;
	attr.stack_size = 2048u;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	return tid;
}

static int gt911_read(int *x, int *y, int *pressed)
{
	uint8_t reg[2];
	uint8_t buf[8];
	int rc;
	uint8_t st;
	uint8_t n;

	if (x)
		*x = 0;
	if (y)
		*y = 0;
	if (pressed)
		*pressed = 0;
	if (!g_have_gt911)
		return ULMK_OK;

	reg[0] = 0x81u;
	reg[1] = 0x4Eu;
	rc = i2c_write(g_gt911_addr, reg, 2u);
	if (rc != ULMK_OK)
		return rc;
	rc = i2c_read(g_gt911_addr, buf, 8u);
	if (rc != ULMK_OK)
		return rc;

	st = buf[0];
	n = st & 0x0Fu;
	if (pressed)
		*pressed = (n != 0u) ? 1 : 0;

	if (n != 0u) {
		if (x)
			*x = (int)buf[2] | ((int)buf[3] << 8);
		if (y)
			*y = (int)buf[4] | ((int)buf[5] << 8);
	}

	{
		uint8_t clr[3] = { 0x81u, 0x4Eu, 0u };

		(void)i2c_write(g_gt911_addr, clr, 3u);
	}

	return ULMK_OK;
}
