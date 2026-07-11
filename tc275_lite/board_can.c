/* SPDX-License-Identifier: MIT */
/*
 * board_can.c — MultiCAN node0 IPC server (TLE9251 on P20.8 TX / P20.7 RX).
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_can.h"
#include "drivers/port/port_regs.h"

#define CAN_MSG_CONFIG		1u
#define CAN_MSG_SEND		2u
#define CAN_MSG_RECV		3u
#define CAN_MSG_SUBSCRIBE	4u
#define CAN_MAP_SIZE		0x4000u
#define CAN_RX_SLOTS		4u

struct can_frame {
	uint32_t id;
	uint8_t  len;
	uint8_t  data[8];
	int      valid;
};

static ulmk_ep_t g_ep __attribute__((section(".user_bss")));
static ulmk_notif_t g_evt __attribute__((section(".user_bss")));
static uint32_t g_evt_bit __attribute__((section(".user_bss")));
static uint32_t g_bitrate __attribute__((section(".user_bss")));
static struct can_frame g_rx[CAN_RX_SLOTS]
	__attribute__((section(".user_bss")));
static uint32_t g_rx_w __attribute__((section(".user_bss")));
static uint32_t g_rx_r __attribute__((section(".user_bss")));

static void can_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	void *can;
	void *p20;
	uint32_t i;

	(void)arg;
	can = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_CAN_BASE, CAN_MAP_SIZE,
			   ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_PERIPH);
	p20 = ulmk_mem_map((void *)(uintptr_t)PORT20_BASE, PORT_MAP_SIZE,
			   ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_PERIPH);
	if (p20) {
		port_set_iocr(p20, 8u, 0x90u); /* TX alt */
		port_set_iocr(p20, 7u, 0x10u); /* RX input */
		/* STB/#NEN low if wired on a GPIO — kit-specific; skip if N/A */
	}
	(void)can;
	g_bitrate = 500000u;

	for (;;) {
		if (ulmk_ep_recv(g_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_OK;
		reply.words[1] = 0u;
		reply.words[2] = 0u;
		reply.words[3] = 0u;
		switch (msg.label) {
		case CAN_MSG_CONFIG:
			g_bitrate = msg.words[0] ? msg.words[0] : 500000u;
			break;
		case CAN_MSG_SEND:
			/* Loopback into RX queue for bring-up without bus. */
			i = g_rx_w % CAN_RX_SLOTS;
			g_rx[i].id = msg.words[0];
			g_rx[i].len = (uint8_t)(msg.words[1] & 0xFu);
			g_rx[i].data[0] = (uint8_t)msg.words[2];
			g_rx[i].data[1] = (uint8_t)(msg.words[2] >> 8);
			g_rx[i].data[2] = (uint8_t)(msg.words[2] >> 16);
			g_rx[i].data[3] = (uint8_t)(msg.words[2] >> 24);
			g_rx[i].valid = 1;
			g_rx_w++;
			if (g_evt != ULMK_NOTIF_INVALID)
				ulmk_notif_signal(g_evt, 1u << g_evt_bit);
			break;
		case CAN_MSG_RECV:
			if (g_rx_r == g_rx_w) {
				reply.words[0] = (uint32_t)(int32_t)ULMK_ETIMEOUT;
				break;
			}
			i = g_rx_r % CAN_RX_SLOTS;
			reply.words[1] = g_rx[i].id;
			reply.words[2] = g_rx[i].len;
			reply.words[3] = (uint32_t)g_rx[i].data[0] |
					 ((uint32_t)g_rx[i].data[1] << 8) |
					 ((uint32_t)g_rx[i].data[2] << 16) |
					 ((uint32_t)g_rx[i].data[3] << 24);
			g_rx[i].valid = 0;
			g_rx_r++;
			break;
		case CAN_MSG_SUBSCRIBE:
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

int board_can_config(uint32_t bitrate)
{
	ulmk_msg_t m;

	m.label = CAN_MSG_CONFIG;
	m.words[0] = bitrate;
	if (ulmk_ep_call(g_ep, &m) != ULMK_OK)
		return ULMK_EINVAL;
	return (int)(int32_t)m.words[0];
}

int board_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
	ulmk_msg_t m;
	uint32_t pack = 0u;
	uint8_t i;

	m.label = CAN_MSG_SEND;
	m.words[0] = id;
	m.words[1] = len;
	if (data) {
		for (i = 0u; i < len && i < 4u; i++)
			pack |= ((uint32_t)data[i]) << (i * 8u);
	}
	m.words[2] = pack;
	if (ulmk_ep_call(g_ep, &m) != ULMK_OK)
		return ULMK_EINVAL;
	return (int)(int32_t)m.words[0];
}

int board_can_recv(uint32_t *id, uint8_t *data, uint8_t *len)
{
	ulmk_msg_t m;
	uint8_t i;

	m.label = CAN_MSG_RECV;
	if (ulmk_ep_call(g_ep, &m) != ULMK_OK)
		return ULMK_EINVAL;
	if ((int)(int32_t)m.words[0] != ULMK_OK)
		return (int)(int32_t)m.words[0];
	if (id)
		*id = m.words[1];
	if (len)
		*len = (uint8_t)m.words[2];
	if (data) {
		for (i = 0u; i < 4u; i++)
			data[i] = (uint8_t)(m.words[3] >> (i * 8u));
	}
	return ULMK_OK;
}

int board_can_subscribe(ulmk_notif_t n, uint32_t bit)
{
	ulmk_msg_t m;

	m.label = CAN_MSG_SUBSCRIBE;
	m.words[0] = (uint32_t)n;
	m.words[1] = bit;
	if (ulmk_ep_call(g_ep, &m) != ULMK_OK)
		return ULMK_EINVAL;
	return (int)(int32_t)m.words[0];
}

ulmk_tid_t board_can_start(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	(void)info;
	g_ep = ulmk_ep_create();
	g_evt = ULMK_NOTIF_INVALID;
	attr.name = "bcan";
	attr.entry = can_server;
	attr.priority = 3u;
	attr.stack_size = 1536u;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid != ULMK_TID_INVALID)
		ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	return tid;
}
