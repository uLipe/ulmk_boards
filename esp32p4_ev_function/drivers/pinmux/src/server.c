/* SPDX-License-Identifier: MIT */
/*
 * pinmux server — owns IOMUX + GPIO matrix pad routing.
 *
 * Other drivers must use pinmux_apply() instead of writing IOMUX/GPIO
 * matrix registers themselves.  Apps use pinmux_config() over IPC.
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "pinmux_internal.h"
#include "board_config.h"

#define PINMUX_STACK		1024u

#ifndef ULMK_BOARD_PINMUX_MAX
#define ULMK_BOARD_PINMUX_MAX	1u
#endif

#define GPIO_BASE		ULMK_BOARD_GPIO_BASE
#define IOMUX_BASE		ULMK_BOARD_IOMUX_BASE

#define GPIO_ENABLE_W1TS	(*(volatile uint32_t *)(GPIO_BASE + 0x24u))
#define GPIO_ENABLE_W1TC	(*(volatile uint32_t *)(GPIO_BASE + 0x28u))
#define GPIO_ENABLE1_W1TS	(*(volatile uint32_t *)(GPIO_BASE + 0x30u))
#define GPIO_ENABLE1_W1TC	(*(volatile uint32_t *)(GPIO_BASE + 0x34u))
#define GPIO_PIN(n)		(*(volatile uint32_t *)(GPIO_BASE + 0x74u + 4u * (n)))
#define GPIO_FUNC_OUT_SEL(n)	(*(volatile uint32_t *)(GPIO_BASE + 0x558u + 4u * (n)))
#define GPIO_FUNC_IN_SEL(sig)	(*(volatile uint32_t *)(GPIO_BASE + 0x158u + 4u * (sig)))
#define IOMUX_GPIO(n)		(*(volatile uint32_t *)(IOMUX_BASE + 0x04u + 4u * (n)))

#define IOMUX_MCU_SEL_GPIO	1u

ulmk_ep_t g_pinmux_eps[ULMK_BOARD_PINMUX_MAX];

static int gpio_ok(uint8_t gpio)
{
	return gpio <= 56u;
}

static void set_oe(uint8_t gpio, int enable)
{
	uint32_t bit = 1u << (gpio & 31u);

	if (gpio < 32u) {
		if (enable)
			GPIO_ENABLE_W1TS = bit;
		else
			GPIO_ENABLE_W1TC = bit;
	} else {
		if (enable)
			GPIO_ENABLE1_W1TS = bit;
		else
			GPIO_ENABLE1_W1TC = bit;
	}
}

int pinmux_apply(const pinmux_cfg_t *cfg)
{
	uint32_t mux;
	uint32_t out_sel;
	uint8_t gpio;

	if (!cfg)
		return ULMK_EINVAL;
	gpio = cfg->pin;
	if (!gpio_ok(gpio))
		return ULMK_EINVAL;

	mux = IOMUX_GPIO(gpio);
	mux &= ~(0x7u << 12);
	mux |= (IOMUX_MCU_SEL_GPIO << 12);
	mux &= ~((1u << 9) | (1u << 8) | (1u << 7));
	if (cfg->pull == PINMUX_PULL_UP)
		mux |= (1u << 8);
	else if (cfg->pull == PINMUX_PULL_DOWN)
		mux |= (1u << 7);
	if (cfg->dir == PINMUX_DIR_IN || (cfg->flags & PINMUX_F_IE) != 0u)
		mux |= (1u << 9);
	IOMUX_GPIO(gpio) = mux;

	if ((cfg->flags & PINMUX_F_OPENDRAIN) != 0u)
		GPIO_PIN(gpio) |= (1u << 2);
	else
		GPIO_PIN(gpio) &= ~(1u << 2);

	if (cfg->alt == PINMUX_ALT_MATRIX) {
		if (cfg->matrix_out != 0u || cfg->dir == PINMUX_DIR_OUT) {
			out_sel = (uint32_t)cfg->matrix_out & 0x1FFu;
			if ((cfg->flags & PINMUX_F_PERIPH_OE) == 0u)
				out_sel |= (1u << 10);
			GPIO_FUNC_OUT_SEL(gpio) = out_sel;
		}
		if (cfg->matrix_in != 0u) {
			GPIO_FUNC_IN_SEL(cfg->matrix_in) =
				(gpio & 0x3Fu) | (1u << 7);
		}
	} else {
		/* GPIO mode: SIG_GPIO_OUT + oen from GPIO_ENABLE */
		GPIO_FUNC_OUT_SEL(gpio) = PINMUX_SIG_GPIO_OUT | (1u << 10);
	}

	if (cfg->dir == PINMUX_DIR_OUT)
		set_oe(gpio, 1);
	else
		set_oe(gpio, 0);

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
			cfg.port       = (uint8_t)(msg.words[0] >> 8);
			cfg.pin        = (uint8_t)(msg.words[0] & 0xFFu);
			cfg.dir        = (uint8_t)msg.words[1];
			cfg.pull       = (uint8_t)msg.words[2];
			cfg.alt        = (uint8_t)(msg.words[3] & 0xFFu);
			cfg.flags      = (uint8_t)((msg.words[3] >> 8) & 0xFFu);
			cfg.matrix_out = (uint16_t)(msg.words[4] & 0xFFFFu);
			cfg.matrix_in  = (uint16_t)((msg.words[4] >> 16) & 0xFFFFu);
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

	if (n >= ULMK_BOARD_PINMUX_MAX)
		return ULMK_TID_INVALID;
	if (g_pinmux_eps[n] != ULMK_EP_INVALID && g_pinmux_eps[n] != 0)
		return ULMK_TID_INVALID;

	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

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
