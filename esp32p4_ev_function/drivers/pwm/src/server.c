/* SPDX-License-Identifier: MIT */
/*
 * LEDC PWM server — ch0 = LCD backlight (GPIO26 on this kit).
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "pwm_internal.h"
#include "board_config.h"

#define PWM_STACK	1536u

ulmk_ep_t g_pwm_ep;
static uint32_t g_duty[PWM_MAX_CH];
static uint8_t  g_on[PWM_MAX_CH];
static uint8_t  g_hw_ok;
static const uint8_t g_pin[PWM_MAX_CH] = {
	ULMK_BOARD_BL_GPIO,
};

static int do_config(uint8_t n, uint32_t freq, uint32_t duty)
{
	int rc;

	if (n >= PWM_MAX_CH)
		return ULMK_EINVAL;
	g_duty[n] = duty > 1000u ? 1000u : duty;
	if (!g_hw_ok)
		return ULMK_ENOTSUP;
	rc = pwm_hw_config(n, g_pin[n], freq ? freq : 1000u, g_duty[n]);
	return rc;
}

static int do_duty(uint8_t n, uint32_t duty)
{
	if (n >= PWM_MAX_CH)
		return ULMK_EINVAL;
	g_duty[n] = duty > 1000u ? 1000u : duty;
	if (!g_hw_ok)
		return ULMK_ENOTSUP;
	return pwm_hw_set_duty(n, g_duty[n], (int)g_on[n]);
}

static int do_enable(uint8_t n, int on)
{
	if (n >= PWM_MAX_CH)
		return ULMK_EINVAL;
	g_on[n] = on ? 1u : 0u;
	if (!g_hw_ok)
		return ULMK_ENOTSUP;
	return pwm_hw_enable(n, g_duty[n], on);
}

static void pwm_server(void *arg)
{
	ulmk_msg_t msg, reply;
	ulmk_tid_t sender;

	(void)arg;
	/* PMP_NUM=0: bootloader map leaves HP peri accessible to drivers. */
	g_hw_ok = (pwm_hw_init() == ULMK_OK) ? 1u : 0u;

	for (;;) {
		if (ulmk_ep_recv(g_pwm_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		switch (msg.label) {
		case PWM_MSG_CONFIG:
			reply.words[0] = (uint32_t)do_config(
				(uint8_t)msg.words[0], msg.words[1],
				msg.words[2]);
			break;
		case PWM_MSG_DUTY:
			reply.words[0] = (uint32_t)do_duty(
				(uint8_t)msg.words[0], msg.words[1]);
			break;
		case PWM_MSG_ENABLE:
			reply.words[0] = (uint32_t)do_enable(
				(uint8_t)msg.words[0], (int)msg.words[1]);
			break;
		default:
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t pwm_init(uint8_t mod)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	(void)mod;
	if (g_pwm_ep != ULMK_EP_INVALID && g_pwm_ep != 0)
		return ULMK_TID_INVALID;
	g_pwm_ep = ulmk_ep_create();
	if (g_pwm_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	attr.name = "pwm";
	attr.entry = pwm_server;
	attr.priority = 1u;
	attr.stack_size = PWM_STACK;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	return tid;
}
