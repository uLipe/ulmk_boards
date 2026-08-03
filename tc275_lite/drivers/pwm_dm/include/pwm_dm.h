/* SPDX-License-Identifier: MIT */
#ifndef PWM_DM_H
#define PWM_DM_H

#include <ulmk/microkernel.h>
#include <stdint.h>

/*
 * Device-manager adapter over the legacy GTM TOM PWM client (pwm.h).
 * Path: /dev/pwm0 (mod 0).
 */
ulmk_tid_t pwm_dm_init(uint8_t mod);
ulmk_ep_t pwm_dm_ep(void);
/* Start legacy pwm server — call from root after ulmk_open(). */
ulmk_tid_t pwm_dm_bind_hw(void);

#endif /* PWM_DM_H */
