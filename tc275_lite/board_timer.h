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

#endif /* BOARD_TIMER_H */
