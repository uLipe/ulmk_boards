/* SPDX-License-Identifier: MIT */
/*
 * pinmux server — owns PORT mem_map (once per port) and pad mode.
 *
 * Other drivers must use pinmux_port() / pinmux_apply() instead of mapping
 * PORT themselves.  Public apps use pinmux_config() over IPC.
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "pinmux_internal.h"
#include "board_config.h"
#include "drivers/common/illd_port.h"
#include "Port/Std/IfxPort.h"

#define PINMUX_STACK		1024u
#define PINMUX_PORT_SLOTS	16u

#ifndef ULMK_BOARD_PINMUX_MAX
#define ULMK_BOARD_PINMUX_MAX	1u
#endif

struct port_slot {
	uint8_t port;
	Ifx_P  *mod;
	int     used;
};

ulmk_ep_t g_pinmux_eps[ULMK_BOARD_PINMUX_MAX];
static struct port_slot g_ports[PINMUX_PORT_SLOTS]
	__attribute__((section(".user_bss")));

/* Ports the Lite Kit demos / console / buses touch — mapped at init. */
static const uint8_t g_board_ports[] = {
	0u, 2u, 10u, 11u, 14u, 15u, 20u, 33u,
};

static Ifx_P *map_port_once(uint8_t port)
{
	uint32_t i;
	Ifx_P *mod;
	void *mapped;

	for (i = 0u; i < PINMUX_PORT_SLOTS; i++) {
		if (g_ports[i].used && g_ports[i].port == port)
			return g_ports[i].mod;
	}
	mod = illd_port_module(port);
	if (!mod)
		return NULL;
	mapped = ulmk_mem_map((void *)mod, ILLD_PORT_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE,
			      ULMK_MMAP_PERIPH);
	if (!mapped)
		return NULL;
	mod = (Ifx_P *)mapped;
	for (i = 0u; i < PINMUX_PORT_SLOTS; i++) {
		if (!g_ports[i].used) {
			g_ports[i].port = port;
			g_ports[i].mod = mod;
			g_ports[i].used = 1;
			return mod;
		}
	}
	return mod;
}

Ifx_P *pinmux_port(uint8_t port)
{
	return map_port_once(port);
}

static IfxPort_Mode resolve_mode(const pinmux_cfg_t *cfg)
{
	uint8_t alt = cfg->alt;
	int od = (cfg->flags & PINMUX_F_OPENDRAIN) != 0;

	if (cfg->dir == PINMUX_DIR_IN) {
		if (cfg->pull == PINMUX_PULL_UP)
			return IfxPort_Mode_inputPullUp;
		if (cfg->pull == PINMUX_PULL_DOWN)
			return IfxPort_Mode_inputPullDown;
		return IfxPort_Mode_inputNoPullDevice;
	}

	if (alt == PINMUX_ALT_GPIO) {
		return od ? IfxPort_Mode_outputOpenDrainGeneral
			  : IfxPort_Mode_outputPushPullGeneral;
	}
	if (od) {
		switch (alt) {
		case 1u: return IfxPort_Mode_outputOpenDrainAlt1;
		case 2u: return IfxPort_Mode_outputOpenDrainAlt2;
		case 3u: return IfxPort_Mode_outputOpenDrainAlt3;
		case 4u: return IfxPort_Mode_outputOpenDrainAlt4;
		case 5u: return IfxPort_Mode_outputOpenDrainAlt5;
		case 6u: return IfxPort_Mode_outputOpenDrainAlt6;
		default: return IfxPort_Mode_outputOpenDrainAlt7;
		}
	}
	switch (alt) {
	case 1u: return IfxPort_Mode_outputPushPullAlt1;
	case 2u: return IfxPort_Mode_outputPushPullAlt2;
	case 3u: return IfxPort_Mode_outputPushPullAlt3;
	case 4u: return IfxPort_Mode_outputPushPullAlt4;
	case 5u: return IfxPort_Mode_outputPushPullAlt5;
	case 6u: return IfxPort_Mode_outputPushPullAlt6;
	default: return IfxPort_Mode_outputPushPullAlt7;
	}
}

int pinmux_apply(const pinmux_cfg_t *cfg)
{
	Ifx_P *mod;

	if (!cfg)
		return ULMK_EINVAL;
	mod = map_port_once(cfg->port);
	if (!mod)
		return ULMK_EINVAL;
	illd_port_set_mode(mod, cfg->pin, resolve_mode(cfg));
	return ULMK_OK;
}

static void pinmux_server(void *arg)
{
	ulmk_ep_t ep = (ulmk_ep_t)(uintptr_t)arg;
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	pinmux_cfg_t cfg;

	for (;;) {
		if (ulmk_ep_recv(ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		if (msg.label == PINMUX_MSG_CONFIG) {
			cfg.port  = (uint8_t)(msg.words[0] >> 8);
			cfg.pin   = (uint8_t)(msg.words[0] & 0xFFu);
			cfg.dir   = (uint8_t)msg.words[1];
			cfg.pull  = (uint8_t)msg.words[2];
			cfg.alt   = (uint8_t)(msg.words[3] & 0xFFu);
			cfg.flags = (uint8_t)((msg.words[3] >> 8) & 0xFFu);
			reply.words[0] = (uint32_t)pinmux_apply(&cfg);
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t pinmux_init(uint8_t n)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_ep_t ep;
	ulmk_tid_t tid;
	uint32_t i;

	if (n >= ULMK_BOARD_PINMUX_MAX)
		return ULMK_TID_INVALID;
	if (g_pinmux_eps[n] != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	/* Pre-map board ports once (caller needs MAP_PERIPH). */
	for (i = 0u; i < (uint32_t)(sizeof(g_board_ports) / sizeof(g_board_ports[0]));
	     i++) {
		if (!map_port_once(g_board_ports[i])) {
			ulmk_ep_destroy(ep);
			return ULMK_TID_INVALID;
		}
	}

	attr.name       = "pinmux";
	attr.entry      = pinmux_server;
	attr.arg        = (void *)(uintptr_t)ep;
	attr.priority   = 2u;
	attr.stack_size = PINMUX_STACK;
	attr.privilege  = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(ep);
		return ULMK_TID_INVALID;
	}
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	g_pinmux_eps[n] = ep;
	return tid;
}
