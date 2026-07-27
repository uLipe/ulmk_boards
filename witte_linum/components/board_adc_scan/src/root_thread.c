/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <adc.h>
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	uint16_t values[ADC_CH_MAX];
	uint8_t n;

	board_services_init(info);
	if (adc_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("ADC scan init failed\r\n");
		ulmk_thread_exit();
	}
	board_console_puts("ADC scan\r\n");
	for (;;) {
		if (adc_scan_all(values) == ULMK_OK) {
			for (n = 0u; n < ADC_CH_MAX; n++)
				board_console_printf("ch%u=%u\r\n", n, values[n]);
		}
		board_timer_sleep_us(500000u);
	}
}
