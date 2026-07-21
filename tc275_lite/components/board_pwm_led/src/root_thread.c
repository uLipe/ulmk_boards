/* SPDX-License-Identifier: MIT */
/*
 * board_pwm_led — fade LED1/LED2 with GTM TOM PWM (active-low).
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <pwm.h>
#include "board_config.h"
#include "board_console.h"

void board_services_init(const ulmk_boot_info_t *info);
void board_timer_sleep_us(uint32_t us);

#define PWM_MOD			0u
#define PWM_CH_LED1		0u
#define PWM_CH_LED2		1u
#define PWM_FREQ_HZ		1000u
#define FADE_STEPS		80u
#define FADE_STEP_US		50000u

static void fade_pair(uint32_t from, uint32_t to)
{
	uint32_t step;
	uint32_t i;
	uint32_t duty1;
	uint32_t duty2;

	for (i = 0u; i <= FADE_STEPS; i++) {
		step = (i * 1000u) / FADE_STEPS;
		if (to >= from)
			duty1 = from + ((to - from) * step) / 1000u;
		else
			duty1 = from - ((from - to) * step) / 1000u;
		duty2 = 1000u - duty1;
		(void)pwm_set_duty(PWM_CH_LED1, duty1);
		(void)pwm_set_duty(PWM_CH_LED2, duty2);
		board_timer_sleep_us(FADE_STEP_US);
	}
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_tid_t tid;
	uint32_t round;

	board_services_init(info);

	tid = pwm_init(PWM_MOD);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("pwm_init failed\r\n");
		ulmk_thread_exit();
	}

	if (pwm_config(PWM_CH_LED1, PWM_FREQ_HZ, 0u) != ULMK_OK) {
		board_console_puts("pwm_config LED1 failed\r\n");
		ulmk_thread_exit();
	}
	if (pwm_config(PWM_CH_LED2, PWM_FREQ_HZ, 1000u) != ULMK_OK) {
		board_console_puts("pwm_config LED2 failed\r\n");
		ulmk_thread_exit();
	}

	(void)pwm_enable(PWM_CH_LED1, 1);
	(void)pwm_enable(PWM_CH_LED2, 1);

	board_console_puts("\r\n");
	board_console_puts("ulmk: TC275 Lite PWM LED fade (TOM0 CH12/13)\r\n");
	board_console_puts("LED1/LED2 cross-fade @ 1 kHz PWM, ~4 s ramps\r\n");

	round = 0u;
	for (;;) {
		board_console_printf("fade round %u\r\n", round);
		fade_pair(0u, 1000u);
		fade_pair(1000u, 0u);
		round++;
	}
}
