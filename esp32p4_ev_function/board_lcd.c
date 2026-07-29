/* SPDX-License-Identifier: MIT */
/*
 * LCD panel — reset GPIO27; backlight GPIO26 as plain GPIO (DC).
 * LEDC PWM is available for demos; display bring-up uses GPIO OE so the
 * DuPont jumper stays driven without remux thrash.
 */
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_lcd.h"
#include "gpio_internal.h"

void board_lcd_reset(void)
{
	(void)gpio_hw_config((uint8_t)ULMK_BOARD_LCD_RST_GPIO,
			     GPIO_DIR_OUT, GPIO_PULL_NONE);
	(void)gpio_hw_set((uint8_t)ULMK_BOARD_LCD_RST_GPIO, 0);
	(void)ulmk_sleep_ms(20u);
	(void)gpio_hw_set((uint8_t)ULMK_BOARD_LCD_RST_GPIO, 1);
	(void)ulmk_sleep_ms(50u);
}

void board_lcd_backlight_gpio(int on)
{
	(void)gpio_hw_config((uint8_t)ULMK_BOARD_BL_GPIO,
			     GPIO_DIR_OUT, GPIO_PULL_NONE);
	(void)gpio_hw_set((uint8_t)ULMK_BOARD_BL_GPIO, on ? 1 : 0);
}
