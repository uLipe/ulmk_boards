/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/board_timer.h — userspace STM0 sleep/timer service.
 */

#ifndef BOARD_TIMER_H
#define BOARD_TIMER_H

#include <ulmk/microkernel.h>
#include <stdint.h>

ulmk_tid_t board_timer_start(const ulmk_boot_info_t *info);
void board_timer_sleep_us(uint32_t us);

/* Free-running STM0.TIM0 — valid after board_timer_start(). */
uint32_t board_timer_now_ticks(void);
uint32_t board_timer_ticks_to_ns(uint32_t dt);

#endif /* BOARD_TIMER_H */
