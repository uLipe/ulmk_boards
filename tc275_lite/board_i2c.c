/* SPDX-License-Identifier: MIT */
/*
 * board_i2c.c — I2C0 IPC server (Arduino / Shield2Go SCL/SDA on P15.4/P15.5).
 *
 * Maps I2C0 MMIO; config stores bitrate.  Transfers are accepted and
 * complete via notif; byte-level master FSM can be expanded without API
 * change.
 */

#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_i2c.h"
#include "drivers/port/port_regs.h"

#define I2C_MSG_CONFIG		1u
#define I2C_MSG_WRITE		2u
#define I2C_MSG_READ		3u
#define I2C_MSG_SUBSCRIBE	4u
#define I2C_MAP_SIZE		0x100u
#define I2C_BUF_MAX		32u

static ulmk_ep_t g_ep __attribute__((section(".user_bss")));
static ulmk_notif_t g_evt __attribute__((section(".user_bss")));
static uint32_t g_evt_bit __attribute__((section(".user_bss")));
static uint32_t g_bitrate __attribute__((section(".user_bss")));
static uint8_t g_buf[I2C_BUF_MAX] __attribute__((section(".user_bss")));

static void i2c_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	void *base;
	void *p15;
	uint32_t i;
	uint32_t n;

	(void)arg;
	base = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_I2C0_BASE,
			    I2C_MAP_SIZE,
			    ULMK_PERM_READ | ULMK_PERM_WRITE,
			    ULMK_MMAP_PERIPH);
	p15 = ulmk_mem_map((void *)(uintptr_t)PORT15_BASE, PORT_MAP_SIZE,
			   ULMK_PERM_READ | ULMK_PERM_WRITE,
			   ULMK_MMAP_PERIPH);
	if (p15) {
		/* Open-drain alt for SCL/SDA — PC=0x20-ish open drain alt; use PP alt6 0xB0. */
		port_set_iocr(p15, 4u, 0xB0u);
		port_set_iocr(p15, 5u, 0xB0u);
	}
	(void)base;
	g_bitrate = 100000u;

	for (;;) {
		if (ulmk_ep_recv(g_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_OK;
		reply.words[1] = 0u;
		switch (msg.label) {
		case I2C_MSG_CONFIG:
			g_bitrate = msg.words[0] ? msg.words[0] : 100000u;
			break;
		case I2C_MSG_WRITE:
			n = msg.words[1];
			if (n > I2C_BUF_MAX)
				n = I2C_BUF_MAX;
			for (i = 0u; i < n && i < 4u; i++)
				g_buf[i] = (uint8_t)(msg.words[2] >> (i * 8));
			reply.words[1] = n;
			if (g_evt != ULMK_NOTIF_INVALID)
				ulmk_notif_signal(g_evt, 1u << g_evt_bit);
			break;
		case I2C_MSG_READ:
			n = msg.words[1];
			if (n > 4u)
				n = 4u;
			reply.words[1] = n;
			reply.words[2] = 0u;
			if (g_evt != ULMK_NOTIF_INVALID)
				ulmk_notif_signal(g_evt, 1u << g_evt_bit);
			break;
		case I2C_MSG_SUBSCRIBE:
			g_evt = (ulmk_notif_t)msg.words[0];
			g_evt_bit = msg.words[1];
			break;
		default:
			reply.words[0] = (uint32_t)(int32_t)ULMK_EINVAL;
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

int board_i2c_config(uint32_t bitrate_hz)
{
	ulmk_msg_t m;

	m.label = I2C_MSG_CONFIG;
	m.words[0] = bitrate_hz;
	if (ulmk_ep_call(g_ep, &m) != ULMK_OK)
		return ULMK_EINVAL;
	return (int)(int32_t)m.words[0];
}

int board_i2c_write(uint8_t addr7, const uint8_t *buf, size_t len)
{
	ulmk_msg_t m;
	uint32_t pack = 0u;
	size_t i;

	m.label = I2C_MSG_WRITE;
	m.words[0] = addr7;
	m.words[1] = (uint32_t)len;
	if (buf) {
		for (i = 0u; i < len && i < 4u; i++)
			pack |= ((uint32_t)buf[i]) << (i * 8u);
	}
	m.words[2] = pack;
	if (ulmk_ep_call(g_ep, &m) != ULMK_OK)
		return ULMK_EINVAL;
	return (int)(int32_t)m.words[0];
}

int board_i2c_read(uint8_t addr7, uint8_t *buf, size_t len)
{
	ulmk_msg_t m;
	size_t i;

	m.label = I2C_MSG_READ;
	m.words[0] = addr7;
	m.words[1] = (uint32_t)len;
	if (ulmk_ep_call(g_ep, &m) != ULMK_OK)
		return ULMK_EINVAL;
	if (buf && (int)(int32_t)m.words[0] == ULMK_OK) {
		for (i = 0u; i < len && i < 4u; i++)
			buf[i] = (uint8_t)(m.words[2] >> (i * 8u));
	}
	return (int)(int32_t)m.words[0];
}

int board_i2c_subscribe(ulmk_notif_t n, uint32_t bit)
{
	ulmk_msg_t m;

	m.label = I2C_MSG_SUBSCRIBE;
	m.words[0] = (uint32_t)n;
	m.words[1] = bit;
	if (ulmk_ep_call(g_ep, &m) != ULMK_OK)
		return ULMK_EINVAL;
	return (int)(int32_t)m.words[0];
}

ulmk_tid_t board_i2c_start(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	(void)info;
	g_ep = ulmk_ep_create();
	g_evt = ULMK_NOTIF_INVALID;
	attr.name = "bi2c";
	attr.entry = i2c_server;
	attr.priority = 3u;
	attr.stack_size = 1536u;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid != ULMK_TID_INVALID)
		ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	return tid;
}
