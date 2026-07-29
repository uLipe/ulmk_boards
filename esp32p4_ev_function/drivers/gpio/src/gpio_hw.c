/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "gpio_internal.h"
#include "pinmux_internal.h"

#define GPIO_BASE		ULMK_BOARD_GPIO_BASE

#define GPIO_OUT_W1TS		(*(volatile uint32_t *)(GPIO_BASE + 0x08u))
#define GPIO_OUT_W1TC		(*(volatile uint32_t *)(GPIO_BASE + 0x0cu))
#define GPIO_OUT1_W1TS		(*(volatile uint32_t *)(GPIO_BASE + 0x14u))
#define GPIO_OUT1_W1TC		(*(volatile uint32_t *)(GPIO_BASE + 0x18u))
#define GPIO_IN			(*(volatile uint32_t *)(GPIO_BASE + 0x3cu))
#define GPIO_IN1		(*(volatile uint32_t *)(GPIO_BASE + 0x40u))

static int gpio_ok(uint8_t gpio)
{
	return gpio <= 56u;
}

int gpio_hw_config(uint8_t gpio, uint32_t dir, uint32_t pull)
{
	pinmux_cfg_t cfg = {0};

	if (!gpio_ok(gpio))
		return ULMK_EINVAL;

	cfg.pin = gpio;
	cfg.dir = (dir == GPIO_DIR_OUT) ? PINMUX_DIR_OUT : PINMUX_DIR_IN;
	if (pull == GPIO_PULL_UP)
		cfg.pull = PINMUX_PULL_UP;
	else if (pull == GPIO_PULL_DOWN)
		cfg.pull = PINMUX_PULL_DOWN;
	else
		cfg.pull = PINMUX_PULL_NONE;
	cfg.alt = PINMUX_ALT_GPIO;
	/* Keep IE for outputs so GPIO_IN reflects the pad. */
	cfg.flags = PINMUX_F_IE;
	return pinmux_apply(&cfg);
}

int gpio_hw_set(uint8_t gpio, int value)
{
	uint32_t bit;

	if (!gpio_ok(gpio))
		return ULMK_EINVAL;
	bit = 1u << (gpio & 31u);
	if (gpio < 32u) {
		if (value)
			GPIO_OUT_W1TS = bit;
		else
			GPIO_OUT_W1TC = bit;
	} else {
		if (value)
			GPIO_OUT1_W1TS = bit;
		else
			GPIO_OUT1_W1TC = bit;
	}
	return ULMK_OK;
}

int gpio_hw_get(uint8_t gpio, int *value)
{
	uint32_t bit;
	uint32_t v;

	if (!gpio_ok(gpio) || !value)
		return ULMK_EINVAL;
	bit = 1u << (gpio & 31u);
	v = (gpio < 32u) ? GPIO_IN : GPIO_IN1;
	*value = (v & bit) ? 1 : 0;
	return ULMK_OK;
}
