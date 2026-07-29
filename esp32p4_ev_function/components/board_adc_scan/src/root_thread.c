/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <adc.h>
#include <dma.h>
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	uint16_t values[ADC_CH_MAX];

	board_services_init(info);
	/* Results reach memory over the DMA, so its server must exist first. */
	if (dma_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("ADC scan: DMA init failed\r\n");
		ulmk_thread_exit();
	}
	if (adc_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("ADC scan init failed\r\n");
		ulmk_thread_exit();
	}
	board_console_puts("ADC scan\r\n");
	for (;;) {
		if (adc_scan_all(values) == ULMK_OK) {
			board_console_printf(
				"ch0=%u ch1=%u ch2=%u ch3=%u\r\n",
				values[0], values[1], values[2], values[3]);
		} else {
			board_console_puts("ADC scan fail\r\n");
		}
		board_timer_sleep_us(500000u);
	}
}
