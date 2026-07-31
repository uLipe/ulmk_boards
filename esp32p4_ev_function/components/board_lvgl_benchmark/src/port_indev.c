/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <lvgl.h>
#include <touch.h>
#include "port_indev.h"

static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
	int x;
	int y;
	int pressed;

	(void)indev;
	if (touch_read(&x, &y, &pressed) == ULMK_OK && pressed) {
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
