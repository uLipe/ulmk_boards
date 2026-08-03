/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "ulmk_device.h"
#include "board_devices.h"
#include <display.h>
#include <display_dm.h>
#include <touch.h>
#include <touch_dm.h>

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
	tid = display_dm_init();
	if (tid == ULMK_TID_INVALID)
		return ULMK_EINVAL;
	ep = display_dm_ep();
	rc = ulmk_dev_register("/dev/disp0", ep, tid,
				ULMK_DEV_CLASS_DISPLAY, 0u);
	if (rc != ULMK_OK)
		return rc;

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
	/* PSRAM @ ULMK_BOARD_PSRAM_BASE is identity-mapped by board_psram. */
	return ULMK_OK;
}
