/* SPDX-License-Identifier: MIT */
#ifndef PWM_INTERNAL_H
#define PWM_INTERNAL_H

#include <ulmk/microkernel.h>
#include <pwm.h>

#define PWM_MSG_CONFIG	1u
#define PWM_MSG_DUTY	2u
#define PWM_MSG_ENABLE	3u

extern ulmk_ep_t g_pwm_ep;

#endif /* PWM_INTERNAL_H */
