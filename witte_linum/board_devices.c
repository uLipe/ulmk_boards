/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "ulmk_device.h"
#include "board_devices.h"
#include "board_config.h"
#include <display.h>
#include <display_dm.h>
#include <touch.h>
#include <touch_dm.h>
#include <i2c.h>
#include <pwm.h>

int board_devices_register(void)
{
	ulmk_tid_t tid;
	ulmk_ep_t ep;
	int rc;

	rc = ulmk_dev_manager_init();
	if (rc != ULMK_OK)
		return rc;

	tid = display_init(0u);
	if (tid == ULMK_TID_INVALID)
		return ULMK_EINVAL;
	/*
	 * Map SDRAM into the registering thread (root) so display_hello and
	 * similar root-only clients can paint.  Worker threads must also call
	 * board_devices_map_fb() — maps are per-thread.
	 */
	if (!ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_SDRAM_BASE,
			  ULMK_BOARD_SDRAM_SIZE,
			  ULMK_PERM_READ | ULMK_PERM_WRITE,
			  ULMK_MMAP_SHARED))
		return ULMK_ENOMEM;
	tid = display_dm_init();
	if (tid == ULMK_TID_INVALID)
		return ULMK_EINVAL;
	ep = display_dm_ep();
	rc = ulmk_dev_register("/dev/disp0", ep, tid,
				ULMK_DEV_CLASS_DISPLAY, 0u);
	if (rc != ULMK_OK)
		return rc;

	tid = pwm_init(0u);
	if (tid != ULMK_TID_INVALID) {
		(void)pwm_config(ULMK_BOARD_PWM_BACKLIGHT, 1000u, 800u);
		(void)pwm_enable(ULMK_BOARD_PWM_BACKLIGHT, 1);
	}

	/*
	 * I2C must be up before touch_init() which issues I2C transactions
	 * from its server thread for FT5446 communication.
	 */
	(void)i2c_init(0u, ULMK_BOARD_I2C_BITRATE_HZ);

	tid = touch_init(0u);
	if (tid == ULMK_TID_INVALID)
		return ULMK_EINVAL;
	tid = touch_dm_init();
	if (tid == ULMK_TID_INVALID)
		return ULMK_EINVAL;
	ep = touch_dm_ep();
	rc = ulmk_dev_register("/dev/input0", ep, tid,
				ULMK_DEV_CLASS_INPUT, 0u);
	return rc;
}

int board_devices_map_fb(void)
{
	if (!ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_SDRAM_BASE,
			  ULMK_BOARD_SDRAM_SIZE,
			  ULMK_PERM_READ | ULMK_PERM_WRITE,
			  ULMK_MMAP_SHARED))
		return ULMK_ENOMEM;
	return ULMK_OK;
}
