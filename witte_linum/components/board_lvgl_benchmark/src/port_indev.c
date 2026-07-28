/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <lvgl.h>
#include <touch.h>
#include "port_indev.h"

static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
	uint16_t x;
	uint16_t y;
	int ret;

	(void)indev;
	ret = touch_poll(0u, &x, &y);
	if (ret == 1) {
		data->state = LV_INDEV_STATE_PRESSED;
		data->point.x = (int32_t)x;
		data->point.y = (int32_t)y;
	} else {
		data->state = LV_INDEV_STATE_RELEASED;
	}
}

lv_indev_t *port_indev_init(lv_display_t *disp)
{
	lv_indev_t *indev;

	indev = lv_indev_create();
	if (!indev)
		return NULL;
	lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
	lv_indev_set_read_cb(indev, indev_read_cb);
	if (disp)
		lv_indev_set_display(indev, disp);
	return indev;
}
