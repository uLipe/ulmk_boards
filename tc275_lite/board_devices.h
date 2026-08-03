/* SPDX-License-Identifier: MIT */
#ifndef BOARD_DEVICES_H
#define BOARD_DEVICES_H

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk_device.h>
#include <ulmk_device_can.h>
#include <ulmk_device_pwm.h>
#include <ulmk_device_adc.h>
#include <ulmk_device_gpio.h>

/*
 * Register TC275 Lite peripheral adapters with the device manager.
 * Call ulmk_dev_manager_init() once (these helpers do it if needed),
 * then the matching *_dm_init(), then the register helper — or use the
 * convenience wrappers below after the adapter is up.
 *
 * Requires the ulmk_device_manager component to be enabled.
 */

int board_devices_mgr_init(void);

int board_devices_register_can(uint8_t n, ulmk_ep_t ep, ulmk_tid_t tid);
int board_devices_register_pwm(ulmk_ep_t ep, ulmk_tid_t tid);
int board_devices_register_adc(ulmk_ep_t ep, ulmk_tid_t tid);
int board_devices_register_gpio(uint8_t n, ulmk_ep_t ep, ulmk_tid_t tid);

#endif /* BOARD_DEVICES_H */
