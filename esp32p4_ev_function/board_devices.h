/* SPDX-License-Identifier: MIT */
#ifndef BOARD_DEVICES_H
#define BOARD_DEVICES_H

#include <ulmk_device.h>
#include <ulmk_device_display.h>
#include <ulmk_device_input.h>

/*
 * Initialise display and touch drivers, then register them with the
 * ulmk device manager under the standard pathnames.
 *
 * Call once from the root thread, after board_services_init().
 * Requires the ulmk_device_manager component to be enabled.
 *
 * Returns ULMK_OK on success, or a negative error code.
 */
int board_devices_register(void);

/*
 * Map framebuffer / paint window into the calling thread.
 * On this board PSRAM is globally MMU-mapped — no-op, always ULMK_OK.
 */
int board_devices_map_fb(void);

#endif /* BOARD_DEVICES_H */
