/* SPDX-License-Identifier: MIT */
#ifndef BOARD_DEVICES_H
#define BOARD_DEVICES_H

#include <ulmk_device.h>
#include <ulmk_device_display.h>
#include <ulmk_device_input.h>

/*
 * Initialise display (+ pwm/i2c bring-up) and touch, then register them
 * with the ulmk device manager under the standard pathnames.
 *
 * Call once from the root thread, after board_services_init().
 * Requires the ulmk_device_manager component to be enabled.
 *
 * Returns ULMK_OK on success, or a negative error code.
 */
int board_devices_register(void);

/*
 * Map the board framebuffer window (and LVGL heap, when colocated) into
 * the *calling* thread.  mem_map is per-thread — a map done in root does
 * not cover a worker that paints via /dev/disp0.
 *
 * Call from every thread that will ulmk_disp_get_fb() / present or use a
 * heap placed in that window (e.g. lvgl_bench).
 */
int board_devices_map_fb(void);

#endif /* BOARD_DEVICES_H */
