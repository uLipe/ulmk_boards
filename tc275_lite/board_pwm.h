/* SPDX-License-Identifier: MIT */
#ifndef BOARD_PWM_H
#define BOARD_PWM_H

#include <ulmk/microkernel.h>
#include <stdint.h>

ulmk_tid_t board_pwm_start(const ulmk_boot_info_t *info);
int board_pwm_config(uint32_t channel, uint32_t freq_hz, uint32_t duty_pct);
int board_pwm_set(uint32_t channel, uint32_t duty_pct);
int board_pwm_get(uint32_t channel, uint32_t *duty_pct);
int board_pwm_enable(uint32_t channel, int on);
int board_pwm_subscribe(ulmk_notif_t n, uint32_t bit);

#endif /* BOARD_PWM_H */
