/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <pwm.h>
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	board_services_init(info);
	if (pwm_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("PWM buzzer init failed\r\n");
		ulmk_thread_exit();
	}
	(void)pwm_config(1u, 2000u, 500u);
	(void)pwm_enable(1u, 1);
	board_console_puts("PWM buzzer\r\n");
	for (;;) {
		board_timer_sleep_us(250000u);
		(void)pwm_set_duty(1u, 0u);
		board_timer_sleep_us(250000u);
		(void)pwm_set_duty(1u, 500u);
	}
}
