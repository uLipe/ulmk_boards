/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "can_internal.h"
#include "drivers/common/illd_port.h"
#include "IfxCan_reg.h"

#define CAN_STACK	1536u
#define CAN_MAP_SIZE	0x400u

typedef struct {
	uint8_t   n;
	can_pins_t pins;
	uint32_t  bitrate;
	ulmk_ep_t ep;
} can_args_t;

ulmk_ep_t g_can_eps[CAN_MAX];
static can_args_t g_args[CAN_MAX] __attribute__((section(".user_bss")));
static can_frame_t g_rx __attribute__((section(".user_bss")));
static int g_rx_valid __attribute__((section(".user_bss")));

static void pinmux(const can_pins_t *p)
{
	Ifx_P *port;
	void *m;

	port = illd_port_module(p->tx_port);
	if (port) {
		m = ulmk_mem_map((void *)port, ILLD_PORT_MAP_SIZE,
				 ULMK_PERM_READ | ULMK_PERM_WRITE,
				 ULMK_MMAP_PERIPH);
		if (m)
			illd_port_set_mode((Ifx_P *)m, p->tx_pin,
					   IfxPort_Mode_outputPushPullAlt5);
	}
	port = illd_port_module(p->rx_port);
	if (port) {
		m = ulmk_mem_map((void *)port, ILLD_PORT_MAP_SIZE,
				 ULMK_PERM_READ | ULMK_PERM_WRITE,
				 ULMK_MMAP_PERIPH);
		if (m)
			illd_port_set_mode((Ifx_P *)m, p->rx_pin,
					   IfxPort_Mode_inputPullUp);
	}
}

static void can_server(void *arg)
{
	can_args_t *a = arg;
	void *mapped;
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;

	mapped = ulmk_mem_map((void *)&MODULE_CAN, CAN_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE,
			      ULMK_MMAP_PERIPH);
	if (!mapped)
		for (;;)
			;
	pinmux(&a->pins);
	(void)a->bitrate;

	for (;;) {
		if (ulmk_ep_recv(a->ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_OK;
		reply.words[1] = 0u;
		reply.words[2] = 0u;
		reply.words[3] = 0u;
		if (msg.label == CAN_MSG_SEND) {
			/* Loopback into local RX queue for bring-up. */
			g_rx.id = msg.words[0];
			g_rx.dlc = (uint8_t)msg.words[1];
			g_rx.data[0] = (uint8_t)(msg.words[2] & 0xFFu);
			g_rx.data[1] = (uint8_t)((msg.words[2] >> 8) & 0xFFu);
			g_rx.data[2] = (uint8_t)((msg.words[2] >> 16) & 0xFFu);
			g_rx.data[3] = (uint8_t)((msg.words[2] >> 24) & 0xFFu);
			g_rx.data[4] = (uint8_t)(msg.words[3] & 0xFFu);
			g_rx.data[5] = (uint8_t)((msg.words[3] >> 8) & 0xFFu);
			g_rx.data[6] = (uint8_t)((msg.words[3] >> 16) & 0xFFu);
			g_rx.data[7] = (uint8_t)((msg.words[3] >> 24) & 0xFFu);
			g_rx_valid = 1;
		} else if (msg.label == CAN_MSG_RECV) {
			if (!g_rx_valid) {
				reply.words[0] = (uint32_t)ULMK_ETIMEOUT;
			} else {
				reply.words[1] = g_rx.id;
				reply.words[2] = g_rx.dlc;
				reply.words[3] = (uint32_t)g_rx.data[0] |
					((uint32_t)g_rx.data[1] << 8) |
					((uint32_t)g_rx.data[2] << 16) |
					((uint32_t)g_rx.data[3] << 24);
				g_rx_valid = 0;
			}
		} else {
			reply.words[0] = (uint32_t)ULMK_EINVAL;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t can_init(uint8_t n, const can_pins_t *pins, uint32_t bitrate)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_ep_t ep;
	ulmk_tid_t tid;

	if (n >= CAN_MAX || !pins || g_can_eps[n] != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	g_args[n].n = n;
	g_args[n].pins = *pins;
	g_args[n].bitrate = bitrate;
	g_args[n].ep = ep;
	attr.name = "can";
	attr.entry = can_server;
	attr.arg = &g_args[n];
	attr.priority = 3u;
	attr.stack_size = CAN_STACK;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(ep);
		return ULMK_TID_INVALID;
	}
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	g_can_eps[n] = ep;
	return tid;
}
