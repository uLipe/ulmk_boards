/* SPDX-License-Identifier: MIT */
/*
 * board_pwm_led — fade LED1/LED2 with GTM TOM PWM (active-low).
 *
 * Hardware PWM at 1 kHz; fade steps sleep via board_timer (IRQ+notif).
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <pwm.h>
#include "board_config.h"

void board_services_init(const ulmk_boot_info_t *info);
void board_console_puts(const char *s);
void board_console_putc(char c);
void board_timer_sleep_us(uint32_t us);

#define PWM_CH_LED1		0u
#define PWM_CH_LED2		1u
#define PWM_FREQ_HZ		1000u
#define FADE_STEPS		50u
#define FADE_STEP_US		20000u	/* 50 × 20 ms = 1 s per ramp */

static void put_u32(uint32_t v)
{
	char buf[10];
	uint32_t i = 0u;
	uint32_t n = v;

	if (n == 0u) {
		board_console_putc('0');
		return;
	}
	while (n > 0u && i < sizeof(buf)) {
		buf[i++] = (char)('0' + (n % 10u));
		n /= 10u;
	}
	while (i > 0u)
		board_console_putc(buf[--i]);
}

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
	pwm_pin_t pin;
	uint32_t round;

	board_services_init(info);

	tid = pwm_init();
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("pwm_init failed\r\n");
		ulmk_thread_exit();
	}

	pin.port = ULMK_BOARD_LED1_PORT;
	pin.pin = ULMK_BOARD_LED1_PIN;
	pin.alt = 1u;
	pin.tom_ch = 12u;
	if (pwm_config(PWM_CH_LED1, &pin, PWM_FREQ_HZ, 0u) != ULMK_OK) {
		board_console_puts("pwm_config LED1 failed\r\n");
		ulmk_thread_exit();
	}

	pin.port = ULMK_BOARD_LED2_PORT;
	pin.pin = ULMK_BOARD_LED2_PIN;
	pin.alt = 1u;
	pin.tom_ch = 13u;
	if (pwm_config(PWM_CH_LED2, &pin, PWM_FREQ_HZ, 1000u) != ULMK_OK) {
		board_console_puts("pwm_config LED2 failed\r\n");
		ulmk_thread_exit();
	}

	(void)pwm_enable(PWM_CH_LED1, 1);
	(void)pwm_enable(PWM_CH_LED2, 1);

	board_console_puts("\r\n");
	board_console_puts("ulmk: TC275 Lite PWM LED fade (TOM0 CH12/13)\r\n");
	board_console_puts("LED1/LED2 cross-fade @ 1 kHz PWM, 1 s ramps\r\n");

	round = 0u;
	for (;;) {
		board_console_puts("fade round ");
		put_u32(round);
		board_console_puts("\r\n");
		fade_pair(0u, 1000u);
		fade_pair(1000u, 0u);
		round++;
	}
}
