/* SPDX-License-Identifier: MIT */
/*
 * freertos_sem_demo — FreeRTOS API on ulmk (TC275 Lite).
 *
 * Task A: every 500 ms prints and gives a binary semaphore.
 * Task B: takes the semaphore, prints, repeats.
 */

#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>

#include "board_console.h"

void board_services_init(const ulmk_boot_info_t *info);

#define TASK_STACK		512
#define TASK_PRIO		3

static ULMK_PRIVATE SemaphoreHandle_t g_sem;

static void task_a(void *arg)
{
	(void)arg;

	for (;;) {
		board_console_puts("freertos: Task A\n");
		(void)xSemaphoreGive(g_sem);
		vTaskDelay(pdMS_TO_TICKS(500));
	}
}

static void task_b(void *arg)
{
	(void)arg;

	for (;;) {
		if (xSemaphoreTake(g_sem, portMAX_DELAY) == pdTRUE)
			board_console_puts("freertos: Task B got sem\n");
	}
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	board_services_init(info);
	freertos_ulmk_init();

	board_console_puts("freertos_sem_demo: start\n");

	g_sem = xSemaphoreCreateBinary();
	if (!g_sem) {
		board_console_puts("freertos_sem_demo: sem create failed\n");
		ulmk_thread_exit();
	}

	if (xTaskCreate(task_a, "task_a", TASK_STACK, NULL, TASK_PRIO, NULL) !=
	    pdPASS) {
		board_console_puts("freertos_sem_demo: task_a failed\n");
		ulmk_thread_exit();
	}
	if (xTaskCreate(task_b, "task_b", TASK_STACK, NULL, TASK_PRIO, NULL) !=
	    pdPASS) {
		board_console_puts("freertos_sem_demo: task_b failed\n");
		ulmk_thread_exit();
	}

	vTaskStartScheduler();
	ulmk_thread_exit();
}
