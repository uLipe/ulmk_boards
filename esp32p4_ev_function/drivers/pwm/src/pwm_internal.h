/* SPDX-License-Identifier: MIT */
#ifndef PWM_INTERNAL_H
#define PWM_INTERNAL_H
#include <ulmk/microkernel.h>
#include <pwm.h>
#define PWM_MSG_CONFIG	1u
#define PWM_MSG_DUTY	2u
#define PWM_MSG_ENABLE	3u
extern ulmk_ep_t g_pwm_ep;

int pwm_hw_init(void);
int pwm_hw_config(uint8_t ch, uint8_t gpio, uint32_t freq_hz,
		  uint32_t duty_permille);
int pwm_hw_set_duty(uint8_t ch, uint32_t duty_permille, int on);
int pwm_hw_enable(uint8_t ch, uint32_t duty_permille, int on);
/* DC level via LEDC idle_lv (no PWM edges). */
int pwm_hw_set_idle_level(uint8_t ch, uint8_t gpio, int level_high);
#endif
