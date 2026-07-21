/* SPDX-License-Identifier: MIT */
/*
 * board_adc_pot — read the Lite Kit potentiometer (AN0) and print voltage.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <adc.h>
#include "board_config.h"
#include "board_console.h"

void board_services_init(const ulmk_boot_info_t *info);
void board_timer_sleep_us(uint32_t us);

#define ADC_MOD		0u
#define ADC_CH		0u
#define VAREF_MV	ULMK_BOARD_VADC_VAREF_MV
#define ADC_FS		4095u
#define SAMPLE_US	200000u

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_tid_t tid;
	uint16_t raw;
	uint32_t mv;
	uint32_t frac;
	int rc;

	board_services_init(info);

	tid = adc_init(ADC_MOD);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("adc_init failed\r\n");
		ulmk_thread_exit();
	}

	rc = adc_config(ADC_CH);
	if (rc != ULMK_OK) {
		board_console_puts("adc_config failed\r\n");
		ulmk_thread_exit();
	}

	board_console_puts("\r\n");
	board_console_puts("ulmk: TC275 Lite ADC pot (AN0 / G0CH0)\r\n");
	board_console_puts("Turn the potentiometer — samples @ 5 Hz\r\n");

	for (;;) {
		rc = adc_read(ADC_CH, &raw);
		if (rc == ULMK_OK) {
			mv = ((uint32_t)raw * VAREF_MV) / ADC_FS;
			frac = mv % 1000u;
			board_console_printf(
				"pot raw=%u  V=%u.%u%u%u V\r\n",
				(uint32_t)raw, mv / 1000u,
				frac / 100u, (frac / 10u) % 10u, frac % 10u);
		} else {
			board_console_puts("adc_read timeout\r\n");
		}
		board_timer_sleep_us(SAMPLE_US);
	}
}
