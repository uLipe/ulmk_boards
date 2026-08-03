/* SPDX-License-Identifier: MIT */
#ifndef GPIO_DM_H
#define GPIO_DM_H

#include <ulmk/microkernel.h>
#include <stdint.h>

/*
 * Device-manager adapter over the legacy GPIO client (gpio.h).
 * Path: /dev/gpio0.  board_services_init() usually already called gpio_init;
 * open() is idempotent in that case.
 */
ulmk_tid_t gpio_dm_init(uint8_t n);
ulmk_ep_t gpio_dm_ep(uint8_t n);

#endif /* GPIO_DM_H */
