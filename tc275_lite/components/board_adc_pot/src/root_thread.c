/* SPDX-License-Identifier: MIT */
/*
 * board_adc_pot — read the Lite Kit potentiometer (AN0) and print voltage.
 *
 * VAREF = 3.3 V, 12-bit VADC G0CH0.  Requires board console + timer + adc.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <adc.h>
#include "board_config.h"

void board_services_init(const ulmk_boot_info_t *info);
void board_console_putc(char c);
void board_console_puts(const char *s);
void board_timer_sleep_us(uint32_t us);

#define POT_GROUP	ULMK_BOARD_ADC_POT_GROUP
#define POT_CHANNEL	ULMK_BOARD_ADC_POT_CHANNEL
#define VAREF_MV	ULMK_BOARD_VADC_VAREF_MV
#define ADC_MAX		4095u
#define SAMPLE_US	200000u

static ULMK_PRIVATE adc_channel_t g_pot;

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

static void print_sample(uint16_t raw)
{
	uint32_t mv;

	mv = ((uint32_t)raw * VAREF_MV) / ADC_MAX;
	board_console_puts("pot raw=");
	put_u32(raw);
	board_console_puts("  V=");
	put_u32(mv / 1000u);
	board_console_putc('.');
	{
		uint32_t frac = mv % 1000u;

		board_console_putc((char)('0' + (frac / 100u)));
		board_console_putc((char)('0' + ((frac / 10u) % 10u)));
		board_console_putc((char)('0' + (frac % 10u)));
	}
	board_console_puts(" V\r\n");
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_tid_t tid;
	uint16_t raw;
	int rc;

	board_services_init(info);

	tid = adc_init();
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("adc_init failed\r\n");
		ulmk_thread_exit();
	}

	g_pot.group = POT_GROUP;
	g_pot.channel = POT_CHANNEL;
	rc = adc_config(&g_pot);
	if (rc != ULMK_OK) {
		board_console_puts("adc_config failed\r\n");
		ulmk_thread_exit();
	}

	board_console_puts("\r\n");
	board_console_puts("ulmk: TC275 Lite ADC pot (AN0 / G0CH0)\r\n");
	board_console_puts("Turn the potentiometer — samples @ 5 Hz\r\n");

	for (;;) {
		rc = adc_read(&g_pot, &raw);
		if (rc == ULMK_OK)
			print_sample(raw);
		else
			board_console_puts("adc_read timeout\r\n");
		board_timer_sleep_us(SAMPLE_US);
	}
}
