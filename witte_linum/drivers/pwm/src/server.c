/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_ll.h"
#include "pinmux_internal.h"
#include "pwm_internal.h"

#define PWM_STACK_SIZE	1536u
#define TIM_MAP_SIZE	0x400u

struct pwm_channel {
	TIM_TypeDef *tim;
	uint8_t port;
	uint8_t pin;
	uint8_t af;
	uint8_t channel;
	uint32_t period;
	uint32_t duty;
	int configured;
	int enabled;
};

ulmk_ep_t g_pwm_ep;
static struct pwm_channel g_channels[PWM_MAX_CH]
	__attribute__((section(".user_bss")));

static const uintptr_t g_tim_base[PWM_MAX_CH] = {
	TIM12_BASE, TIM4_BASE,
};

static void pwm_pinmux(const struct pwm_channel *ch)
{
	pinmux_cfg_t cfg;

	cfg.port = ch->port;
	cfg.pin = ch->pin;
	cfg.dir = PINMUX_DIR_OUT;
	cfg.pull = PINMUX_PULL_NONE;
	cfg.alt = ch->af;
	cfg.flags = 0u;
	(void)pinmux_apply(&cfg);
}

static void pwm_apply_duty(struct pwm_channel *ch)
{
	uint32_t ccr;

	ccr = (ch->period * ch->duty) / 1000u;
	if (!ch->enabled)
		ccr = 0u;
	if (ch->channel == 1u)
		LL_TIM_OC_SetCompareCH1(ch->tim, ccr);
	else
		LL_TIM_OC_SetCompareCH2(ch->tim, ccr);
}

static int pwm_configure(uint8_t n, uint32_t freq_hz, uint32_t duty)
{
	struct pwm_channel *ch = &g_channels[n];
	uint32_t clk = ULMK_BOARD_PWM_TIM_CLK_HZ;
	uint32_t ticks;
	uint32_t psc;
	uint32_t arr;
	uint32_t ch_ll;

	if (freq_hz == 0u || !ch->tim)
		return ULMK_EINVAL;

	/*
	 * TIM4/TIM12 are 16-bit.  At 240 MHz, 1 kHz needs PSC>0 or ARR
	 * truncates and the PWM glitches.
	 */
	ticks = clk / freq_hz;
	if (ticks < 2u)
		return ULMK_EINVAL;
	psc = (ticks + 65535u) / 65536u;
	if (psc == 0u)
		psc = 1u;
	psc--;
	arr = (ticks / (psc + 1u)) - 1u;
	if (arr == 0u || arr > 0xFFFFu || psc > 0xFFFFu)
		return ULMK_EINVAL;

	ch->period = arr;
	ch->duty = duty;
	ch->configured = 1;
	pwm_pinmux(ch);

	ch_ll = (ch->channel == 1u) ? LL_TIM_CHANNEL_CH1 : LL_TIM_CHANNEL_CH2;

	LL_TIM_DisableCounter(ch->tim);
	LL_TIM_SetPrescaler(ch->tim, psc);
	LL_TIM_SetAutoReload(ch->tim, arr);
	LL_TIM_SetCounterMode(ch->tim, LL_TIM_COUNTERMODE_UP);
	LL_TIM_OC_SetMode(ch->tim, ch_ll, LL_TIM_OCMODE_PWM1);
	LL_TIM_OC_SetPolarity(ch->tim, ch_ll, LL_TIM_OCPOLARITY_HIGH);
	LL_TIM_OC_EnablePreload(ch->tim, ch_ll);
	LL_TIM_EnableARRPreload(ch->tim);
	pwm_apply_duty(ch);
	LL_TIM_GenerateEvent_UPDATE(ch->tim);
	return ULMK_OK;
}

static int pwm_set_enable(uint8_t n, int enabled)
{
	struct pwm_channel *ch = &g_channels[n];

	if (!ch->configured)
		return ULMK_EINVAL;
	ch->enabled = enabled;
	pwm_apply_duty(ch);
	if (ch->channel == 1u) {
		if (enabled)
			LL_TIM_CC_EnableChannel(ch->tim, LL_TIM_CHANNEL_CH1);
		else
			LL_TIM_CC_DisableChannel(ch->tim, LL_TIM_CHANNEL_CH1);
	} else if (enabled) {
		LL_TIM_CC_EnableChannel(ch->tim, LL_TIM_CHANNEL_CH2);
	} else {
		LL_TIM_CC_DisableChannel(ch->tim, LL_TIM_CHANNEL_CH2);
	}
	if (enabled)
		LL_TIM_EnableCounter(ch->tim);
	return ULMK_OK;
}

static void pwm_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	uint8_t n;

	(void)arg;

	for (n = 0u; n < PWM_MAX_CH; n++) {
		g_channels[n].tim = ulmk_mem_map((void *)g_tim_base[n], TIM_MAP_SIZE,
						 ULMK_PERM_READ | ULMK_PERM_WRITE,
						 ULMK_MMAP_PERIPH);
		if (!g_channels[n].tim)
			return;
	}
	g_channels[0].port = 7u;	/* GPIOH */
	g_channels[0].pin = 6u;
	g_channels[0].af = 2u;
	g_channels[0].channel = 1u;
	g_channels[1].port = 1u;	/* GPIOB */
	g_channels[1].pin = 7u;
	g_channels[1].af = 2u;
	g_channels[1].channel = 2u;

	for (;;) {
		if (ulmk_ep_recv(g_pwm_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		n = (uint8_t)msg.words[0];
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		if (n < PWM_MAX_CH && msg.label == PWM_MSG_CONFIG)
			reply.words[0] = (uint32_t)pwm_configure(n, msg.words[1],
							      msg.words[2]);
		else if (n < PWM_MAX_CH && msg.label == PWM_MSG_DUTY) {
			if (msg.words[1] <= 1000u && g_channels[n].configured) {
				g_channels[n].duty = msg.words[1];
				pwm_apply_duty(&g_channels[n]);
				reply.words[0] = (uint32_t)ULMK_OK;
			}
		} else if (n < PWM_MAX_CH && msg.label == PWM_MSG_ENABLE) {
			reply.words[0] = (uint32_t)pwm_set_enable(n,
							   msg.words[1] != 0u);
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t pwm_init(uint8_t mod)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	if (mod != 0u || g_pwm_ep != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	g_pwm_ep = ulmk_ep_create();
	if (g_pwm_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	attr.name = "pwm";
	attr.entry = pwm_server;
	attr.priority = 2u;
	attr.stack_size = PWM_STACK_SIZE;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	return tid;
}
