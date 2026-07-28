/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <i2c.h>
#include <touch.h>
#include "board_console.h"
#include "board_services.h"
#include "board_config.h"

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	uint16_t x;
	uint16_t y;
	int ret;

	board_services_init(info);
	board_console_puts("\r\ntouch xy\r\n");

	if (i2c_init(0u, ULMK_BOARD_I2C_BITRATE_HZ) == ULMK_TID_INVALID) {
		board_console_puts("i2c init failed\r\n");
		ulmk_thread_exit();
	}
	if (touch_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("touch init failed\r\n");
		ulmk_thread_exit();
	}

	ret = i2c_probe(0u, ULMK_BOARD_TOUCH_ADDR7);
	board_console_printf("probe ret=%d\r\n", ret);

	ret = touch_read_xy(0u, &x, &y);
	board_console_printf("read_xy ret=%d x=%u y=%u\r\n", ret, x, y);
	board_console_puts("waiting for touch...\r\n");

	for (;;) {
		ret = touch_wait(0u, 1000u);
		if (ret != ULMK_OK)
			continue;
		ret = touch_read_xy(0u, &x, &y);
		if (ret == ULMK_OK)
			board_console_printf("touch x=%u y=%u\r\n", x, y);
		else
			board_console_printf("touch read err=%d\r\n", ret);
	}
}
