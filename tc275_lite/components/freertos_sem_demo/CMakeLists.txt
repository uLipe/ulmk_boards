# SPDX-License-Identifier: MIT
# FreeRTOS shim demo: two periodic tasks + binary semaphore sync.

ulmk_component_register(
	NAME         freertos_sem_demo
	ENABLED      OFF
	REQUIRES     freertos
	SOURCES
		src/root_thread.c
	ROOT_THREAD
)
