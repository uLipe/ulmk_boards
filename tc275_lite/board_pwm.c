/* SPDX-License-Identifier: MIT */
/*
 * board_pwm.c — GTM PWM IPC server (channel 0 → P20.12 Arduino PWM pin).
 *
 * Maps GTM; stores freq/duty and toggles the pin in a soft PWM fashion when
 * enabled (bring-up).  ATOM/TOM register programming can replace soft PWM
 * without changing the client API.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_pwm.h"
#include "drivers/port/port_regs.h"

#define PWM_MSG_CONFIG		1u
#define PWM_MSG_SET		2u
#define PWM_MSG_GET		3u
#define PWM_MSG_ENABLE		4u
#define PWM_MSG_SUBSCRIBE	5u
#define PWM_MAP_SIZE		0x10000u
#define PWM_CHANNELS		2u

struct pwm_ch {
	uint32_t freq_hz;
	uint32_t duty_pct;
	int      enabled;
};

static ulmk_ep_t g_ep __attribute__((section(".user_bss")));
static ulmk_notif_t g_evt __attribute__((section(".user_bss")));
static uint32_t g_evt_bit __attribute__((section(".user_bss")));
static struct pwm_ch g_ch[PWM_CHANNELS] __attribute__((section(".user_bss")));
static void *g_p20 __attribute__((section(".user_bss")));

static void pwm_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	void *gtm;
	uint32_t ch;

	(void)arg;
	gtm = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_GTM_BASE, PWM_MAP_SIZE,
			   ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_PERIPH);
	g_p20 = ulmk_mem_map((void *)(uintptr_t)PORT20_BASE, PORT_MAP_SIZE,
			     ULMK_PERM_READ | ULMK_PERM_WRITE,
			     ULMK_MMAP_PERIPH);
	if (g_p20)
		port_set_iocr(g_p20, 12u, PORT_PC_OUT_PP);
	(void)gtm;
	g_ch[0].freq_hz = 1000u;
	g_ch[0].duty_pct = 0u;

	for (;;) {
		if (ulmk_ep_recv(g_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_OK;
		reply.words[1] = 0u;
		ch = msg.words[0];
		switch (msg.label) {
		case PWM_MSG_CONFIG:
			if (ch >= PWM_CHANNELS) {
				reply.words[0] = (uint32_t)(int32_t)ULMK_EINVAL;
				break;
			}
			g_ch[ch].freq_hz = msg.words[1] ? msg.words[1] : 1000u;
			g_ch[ch].duty_pct = msg.words[2] > 100u ? 100u
								: msg.words[2];
			break;
		case PWM_MSG_SET:
			if (ch >= PWM_CHANNELS) {
				reply.words[0] = (uint32_t)(int32_t)ULMK_EINVAL;
				break;
			}
			g_ch[ch].duty_pct = msg.words[1] > 100u ? 100u
								: msg.words[1];
			if (g_p20 && g_ch[ch].enabled)
				port_write_pin(g_p20, 12u,
					       g_ch[ch].duty_pct > 50u);
			break;
		case PWM_MSG_GET:
			if (ch >= PWM_CHANNELS) {
				reply.words[0] = (uint32_t)(int32_t)ULMK_EINVAL;
				break;
			}
			reply.words[1] = g_ch[ch].duty_pct;
			break;
		case PWM_MSG_ENABLE:
			if (ch >= PWM_CHANNELS) {
				reply.words[0] = (uint32_t)(int32_t)ULMK_EINVAL;
				break;
			}
			g_ch[ch].enabled = msg.words[1] ? 1 : 0;
			if (g_p20)
				port_write_pin(g_p20, 12u,
					       g_ch[ch].enabled &&
					       g_ch[ch].duty_pct > 0u);
			if (g_evt != ULMK_NOTIF_INVALID)
				ulmk_notif_signal(g_evt, 1u << g_evt_bit);
			break;
		case PWM_MSG_SUBSCRIBE:
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

int board_pwm_config(uint32_t channel, uint32_t freq_hz, uint32_t duty_pct)
{
	ulmk_msg_t m;

	m.label = PWM_MSG_CONFIG;
	m.words[0] = channel;
	m.words[1] = freq_hz;
	m.words[2] = duty_pct;
	if (ulmk_ep_call(g_ep, &m) != ULMK_OK)
		return ULMK_EINVAL;
	return (int)(int32_t)m.words[0];
}

int board_pwm_set(uint32_t channel, uint32_t duty_pct)
{
	ulmk_msg_t m;

	m.label = PWM_MSG_SET;
	m.words[0] = channel;
	m.words[1] = duty_pct;
	if (ulmk_ep_call(g_ep, &m) != ULMK_OK)
		return ULMK_EINVAL;
	return (int)(int32_t)m.words[0];
}

int board_pwm_get(uint32_t channel, uint32_t *duty_pct)
{
	ulmk_msg_t m;

	m.label = PWM_MSG_GET;
	m.words[0] = channel;
	if (ulmk_ep_call(g_ep, &m) != ULMK_OK)
		return ULMK_EINVAL;
	if (duty_pct && (int)(int32_t)m.words[0] == ULMK_OK)
		*duty_pct = m.words[1];
	return (int)(int32_t)m.words[0];
}

int board_pwm_enable(uint32_t channel, int on)
{
	ulmk_msg_t m;

	m.label = PWM_MSG_ENABLE;
	m.words[0] = channel;
	m.words[1] = (uint32_t)on;
	if (ulmk_ep_call(g_ep, &m) != ULMK_OK)
		return ULMK_EINVAL;
	return (int)(int32_t)m.words[0];
}

int board_pwm_subscribe(ulmk_notif_t n, uint32_t bit)
{
	ulmk_msg_t m;

	m.label = PWM_MSG_SUBSCRIBE;
	m.words[0] = (uint32_t)n;
	m.words[1] = bit;
	if (ulmk_ep_call(g_ep, &m) != ULMK_OK)
		return ULMK_EINVAL;
	return (int)(int32_t)m.words[0];
}

ulmk_tid_t board_pwm_start(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	(void)info;
	g_ep = ulmk_ep_create();
	g_evt = ULMK_NOTIF_INVALID;
	attr.name = "bpwm";
	attr.entry = pwm_server;
	attr.priority = 3u;
	attr.stack_size = 1536u;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid != ULMK_TID_INVALID)
		ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	return tid;
}
