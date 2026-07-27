/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <pwm.h>
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	uint32_t duty;

	board_services_init(info);
	if (pwm_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("PWM backlight init failed\r\n");
		ulmk_thread_exit();
	}
	(void)pwm_config(0u, 1000u, 0u);
	(void)pwm_enable(0u, 1);
	board_console_puts("PWM backlight\r\n");
	for (;;) {
		for (duty = 0u; duty <= 1000u; duty += 50u) {
			(void)pwm_set_duty(0u, duty);
			board_timer_sleep_us(20000u);
		}
		for (duty = 1000u; duty > 0u; duty -= 50u) {
			(void)pwm_set_duty(0u, duty);
			board_timer_sleep_us(20000u);
		}
	}
}
