/* SPDX-License-Identifier: MIT */
#ifndef BOARD_LCD_H
#define BOARD_LCD_H

#include <stdint.h>

/* LCD panel — reset (GPIO27) + backlight DC on GPIO26 (GPIO OE, not LEDC). */
void board_lcd_reset(void);
void board_lcd_backlight_gpio(int on);

#endif /* BOARD_LCD_H */
