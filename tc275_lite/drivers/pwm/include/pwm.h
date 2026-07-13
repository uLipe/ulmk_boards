/* SPDX-License-Identifier: MIT */
#ifndef PWM_H
#define PWM_H

#include <ulmk/microkernel.h>
#include <stdint.h>
#include "board_config.h"

#define PWM_MAX_CH	ULMK_BOARD_PWM_MAX

/*
 * pwm_init(@p mod) — start GTM TOM server (Lite: mod 0 = TOM0).
 * pwm_config/set/enable(@p n) — @p n is the PWM channel (board pin/TOM map).
 */
ulmk_tid_t pwm_init(uint8_t mod);
int pwm_config(uint8_t n, uint32_t freq_hz, uint32_t duty_permille);
int pwm_set_duty(uint8_t n, uint32_t duty_permille);
int pwm_enable(uint8_t n, int on);

#endif /* PWM_H */
