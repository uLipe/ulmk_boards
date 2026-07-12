/* SPDX-License-Identifier: MIT */
#ifndef BOARD_SERVICES_H
#define BOARD_SERVICES_H

#include <ulmk/microkernel.h>

/*
 * Starts console (ASCLIN0), timer (STM0), gpio server, and board LEDs.
 * Apps that need I2C/ADC/CAN/PWM call those drivers' *_init() directly.
 */
void board_services_init(const ulmk_boot_info_t *info);

#endif /* BOARD_SERVICES_H */
