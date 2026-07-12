/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "pwm_internal.h"
#include "drivers/common/illd_port.h"
#include "IfxGtm_reg.h"

#define PWM_STACK	1536u
#define PWM_MAP_SIZE	0x400u

struct pwm_ch {
	uint8_t  port;
	uint8_t  pin;
	uint8_t  alt;
	uint32_t duty;
	int      on;
	int      used;
};

ulmk_ep_t g_pwm_ep;
static struct pwm_ch g_ch[PWM_MAX_CH] __attribute__((section(".user_bss")));

static void apply_soft(uint8_t ch)
{
	Ifx_P *port;
	void *m;

	if (ch >= PWM_MAX_CH || !g_ch[ch].used)
		return;
	port = illd_port_module(g_ch[ch].port);
	if (!port)
		return;
	m = ulmk_mem_map((void *)port, ILLD_PORT_MAP_SIZE,
			 ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_PERIPH);
	if (!m)
		return;
	illd_port_set_mode((Ifx_P *)m, g_ch[ch].pin,
			   IfxPort_Mode_outputPushPullGeneral);
	if (g_ch[ch].on && g_ch[ch].duty >= 500u)
		IfxPort_setPinHigh((Ifx_P *)m, g_ch[ch].pin);
	else
		IfxPort_setPinLow((Ifx_P *)m, g_ch[ch].pin);
}

static void pwm_server(void *arg)
{
	void *mapped;
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	uint8_t ch;

	(void)arg;
	mapped = ulmk_mem_map((void *)&MODULE_GTM, PWM_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE,
			      ULMK_MMAP_PERIPH);
	if (!mapped)
		for (;;)
			;
	(void)mapped;

	for (;;) {
		if (ulmk_ep_recv(g_pwm_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_OK;
		ch = (uint8_t)msg.words[0];
		if (ch >= PWM_MAX_CH) {
			reply.words[0] = (uint32_t)ULMK_EINVAL;
		} else if (msg.label == PWM_MSG_CONFIG) {
			g_ch[ch].port = (uint8_t)(msg.words[1] >> 16);
			g_ch[ch].pin  = (uint8_t)((msg.words[1] >> 8) & 0xFFu);
			g_ch[ch].alt  = (uint8_t)(msg.words[1] & 0xFFu);
			g_ch[ch].duty = msg.words[3];
			g_ch[ch].used = 1;
			g_ch[ch].on = 0;
			apply_soft(ch);
		} else if (msg.label == PWM_MSG_DUTY) {
			g_ch[ch].duty = msg.words[1];
			apply_soft(ch);
		} else if (msg.label == PWM_MSG_ENABLE) {
			g_ch[ch].on = (int)msg.words[1];
			apply_soft(ch);
		} else {
			reply.words[0] = (uint32_t)ULMK_EINVAL;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t pwm_init(void)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	if (g_pwm_ep != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	g_pwm_ep = ulmk_ep_create();
	if (g_pwm_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	attr.name = "pwm";
	attr.entry = pwm_server;
	attr.priority = 3u;
	attr.stack_size = PWM_STACK;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(g_pwm_ep);
		g_pwm_ep = ULMK_EP_INVALID;
		return ULMK_TID_INVALID;
	}
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	return tid;
}
