/* SPDX-License-Identifier: MIT */
#ifndef PWM_H
#define PWM_H

#include <ulmk/microkernel.h>
#include <stdint.h>

#define PWM_MAX_CH	4u

typedef struct {
	uint8_t port;
	uint8_t pin;
	uint8_t alt;		/* GTM ATOM/TOM alternate */
	uint8_t atom_channel;	/* logical channel id */
} pwm_pin_t;

ulmk_tid_t pwm_init(void);
int pwm_config(uint8_t ch, const pwm_pin_t *pin, uint32_t freq_hz,
	       uint32_t duty_permille);
int pwm_set_duty(uint8_t ch, uint32_t duty_permille);
int pwm_enable(uint8_t ch, int on);

#endif /* PWM_H */
