/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include "pwm_internal.h"

static int pwm_call(ulmk_msg_t *msg)
{
	if (g_pwm_ep == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	return ulmk_ep_call(g_pwm_ep, msg);
}

int pwm_config(uint8_t n, uint32_t freq_hz, uint32_t duty_permille)
{
	ulmk_msg_t msg;
	int rc;
	msg.label = PWM_MSG_CONFIG;
	msg.words[0] = n;
	msg.words[1] = freq_hz;
	msg.words[2] = duty_permille;
	rc = pwm_call(&msg);
	return rc != ULMK_OK ? rc : (int)(int32_t)msg.words[0];
}

int pwm_set_duty(uint8_t n, uint32_t duty_permille)
{
	ulmk_msg_t msg;
	int rc;
	msg.label = PWM_MSG_DUTY;
	msg.words[0] = n;
	msg.words[1] = duty_permille;
	rc = pwm_call(&msg);
	return rc != ULMK_OK ? rc : (int)(int32_t)msg.words[0];
}

int pwm_enable(uint8_t n, int on)
{
	ulmk_msg_t msg;
	int rc;
	msg.label = PWM_MSG_ENABLE;
	msg.words[0] = n;
	msg.words[1] = (uint32_t)on;
	rc = pwm_call(&msg);
	return rc != ULMK_OK ? rc : (int)(int32_t)msg.words[0];
}
