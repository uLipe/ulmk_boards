/* SPDX-License-Identifier: MIT */
/*
 * board_adc_pot_dm — potentiometer via /dev/adc0.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <ulmk_device.h>
#include <ulmk_device_adc.h>
#include <adc_dm.h>
#include "board_config.h"
#include "board_console.h"
#include "board_devices.h"

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
	ulmk_dev_t adc;
	uint16_t raw;
	uint32_t mv;
	uint32_t frac;
	int rc;

	board_services_init(info);

	tid = adc_dm_init(ADC_MOD);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("adc_dm_init failed\r\n");
		ulmk_thread_exit();
	}
	if (board_devices_register_adc(adc_dm_ep(), tid) != ULMK_OK) {
		board_console_puts("register /dev/adc0 failed\r\n");
		ulmk_thread_exit();
	}

	if (ulmk_open("/dev/adc0", &adc) != ULMK_OK) {
		board_console_puts("ulmk_open(/dev/adc0) failed\r\n");
		ulmk_thread_exit();
	}

	rc = ulmk_adc_config(&adc, ADC_CH);
	if (rc != ULMK_OK) {
		board_console_puts("adc_config failed\r\n");
		ulmk_thread_exit();
	}
	(void)ulmk_adc_select(&adc, ADC_CH);

	board_console_puts("\r\n");
	board_console_puts("ulmk: TC275 Lite ADC pot DM (AN0 / G0CH0)\r\n");
	board_console_puts("Turn the potentiometer — samples @ 5 Hz\r\n");

	for (;;) {
		rc = ulmk_adc_read(&adc, &raw);
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
