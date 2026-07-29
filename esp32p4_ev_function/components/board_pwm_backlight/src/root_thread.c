/* SPDX-License-Identifier: MIT */
#include <stdint.h>
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
		board_console_puts("PWM init failed\r\n");
		ulmk_thread_exit();
	}
	board_console_puts("PWM backlight fade (GPIO26)\r\n");
	(void)pwm_config(0u, 1000u, 0u);
	(void)pwm_enable(0u, 1);
	duty = 0u;
	for (;;) {
		(void)pwm_set_duty(0u, duty);
		board_console_printf("pwm duty=%u\r\n", duty);
		duty = (duty + 100u) % 1100u;
		board_timer_sleep_us(200000u);
	}
}
