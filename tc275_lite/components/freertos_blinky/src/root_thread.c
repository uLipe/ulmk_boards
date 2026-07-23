/* SPDX-License-Identifier: MIT */
/*
 * freertos_blinky — FreeRTOS API on ulmk (TC275 Lite).
 *
 * Two tasks drive LED1 / LED2 out of phase @ 100 ms using vTaskDelay.
 */

#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <FreeRTOS.h>
#include <task.h>

#include "board_console.h"

void board_services_init(const ulmk_boot_info_t *info);
int board_leds_set(uint32_t led, int on);

#define BOARD_LED_1	0u
#define BOARD_LED_2	1u

#define TASK_STACK	512
#define TASK_PRIO	3
#define BLINK_MS	100u

static void led1_task(void *arg)
{
	(void)arg;

	for (;;) {
		(void)board_leds_set(BOARD_LED_1, 1);
		vTaskDelay(pdMS_TO_TICKS(BLINK_MS));
		(void)board_leds_set(BOARD_LED_1, 0);
		vTaskDelay(pdMS_TO_TICKS(BLINK_MS));
	}
}

static void led2_task(void *arg)
{
	(void)arg;

	/* Opposite phase to LED1. */
	vTaskDelay(pdMS_TO_TICKS(BLINK_MS));
	for (;;) {
		(void)board_leds_set(BOARD_LED_2, 1);
		vTaskDelay(pdMS_TO_TICKS(BLINK_MS));
		(void)board_leds_set(BOARD_LED_2, 0);
		vTaskDelay(pdMS_TO_TICKS(BLINK_MS));
	}
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	board_services_init(info);
	freertos_ulmk_init();

	board_console_puts("freertos_blinky: LEDs alternate @ 100 ms\n");

	if (xTaskCreate(led1_task, "led1", TASK_STACK, NULL, TASK_PRIO,
			NULL) != pdPASS) {
		board_console_puts("freertos_blinky: led1 task failed\n");
		ulmk_thread_exit();
	}
	if (xTaskCreate(led2_task, "led2", TASK_STACK, NULL, TASK_PRIO,
			NULL) != pdPASS) {
		board_console_puts("freertos_blinky: led2 task failed\n");
		ulmk_thread_exit();
	}

	vTaskStartScheduler();
	ulmk_thread_exit();
}
