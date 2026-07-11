/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/board_services.h — portable board service entry points.
 */

#ifndef BOARD_SERVICES_H
#define BOARD_SERVICES_H

#include <ulmk/microkernel.h>

/* Cert path: console + timer + gpio + leds. */
void board_services_init(const ulmk_boot_info_t *info);

/* Full BSP: also I2C, ADC, CAN, PWM servers. */
void board_services_init_full(const ulmk_boot_info_t *info);

#endif /* BOARD_SERVICES_H */
