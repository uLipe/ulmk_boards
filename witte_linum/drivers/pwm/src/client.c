/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "pwm_internal.h"

static int pwm_call(ulmk_msg_t *msg)
{
	int ret;

	if (g_pwm_ep == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	ret = ulmk_ep_call(g_pwm_ep, msg);
	if (ret != ULMK_OK)
		return ret;
	return (int)(int32_t)msg->words[0];
}

int pwm_config(uint8_t n, uint32_t freq_hz, uint32_t duty_permille)
{
	ulmk_msg_t msg = {0};

	if (n >= PWM_MAX_CH || freq_hz == 0u || duty_permille > 1000u)
		return ULMK_EINVAL;
	msg.label = PWM_MSG_CONFIG;
	msg.words[0] = n;
	msg.words[1] = freq_hz;
	msg.words[2] = duty_permille;
	return pwm_call(&msg);
}

int pwm_set_duty(uint8_t n, uint32_t duty_permille)
{
	ulmk_msg_t msg = {0};

	if (n >= PWM_MAX_CH || duty_permille > 1000u)
		return ULMK_EINVAL;
	msg.label = PWM_MSG_DUTY;
	msg.words[0] = n;
	msg.words[1] = duty_permille;
	return pwm_call(&msg);
}

int pwm_enable(uint8_t n, int on)
{
	ulmk_msg_t msg = {0};

	if (n >= PWM_MAX_CH)
		return ULMK_EINVAL;
	msg.label = PWM_MSG_ENABLE;
	msg.words[0] = n;
	msg.words[1] = on ? 1u : 0u;
	return pwm_call(&msg);
}
