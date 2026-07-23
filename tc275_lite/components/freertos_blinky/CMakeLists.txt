# SPDX-License-Identifier: MIT
# FreeRTOS blinky: two tasks alternate board LEDs @ 100 ms.

ulmk_component_register(
	NAME         freertos_blinky
	ENABLED      OFF
	REQUIRES     freertos
	SOURCES
		src/root_thread.c
	ROOT_THREAD
)
