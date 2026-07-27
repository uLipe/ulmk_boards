/* SPDX-License-Identifier: MIT */
/*
 * LED4 (GPIO19) / LED5 (GPIO62) — active-low on LAUNCHXL-F29H85X.
 */

#include <stdint.h>
#include <stdbool.h>
#include <board_config.h>

#define MMIO32(a)	(*(volatile uint32_t *)(uintptr_t)(a))

#define GPIOCTRL_BASE	ULMK_BOARD_GPIOCTRL_BASE
#define GPIODATA_BASE	ULMK_BOARD_GPIODATA_BASE
#define GPIO_MUX_TO_GMUX	0x34u

/* pin_map.h values for GPIO-as-GPIO. */
#define GPIO_19_GPIO19	0x00100600u
#define GPIO_62_GPIO62	0x00901C00u

/*
 * GPIODATA port stride: GPBDAT-GPADAT = 0x14 → 5× uint32
 * layout: DAT, SET, CLEAR, TOGGLE, DIR
 */
#define GPIO_DATA_REGS_STEP	5u
#define GPIO_GPxSET_INDEX	1u
#define GPIO_GPxCLEAR_INDEX	2u
#define GPIO_GPxTOGGLE_INDEX	3u
#define GPIO_GPxDIR_INDEX	4u

static bool g_leds_ready;
static uint8_t g_led_on[2];

static void gpio_set_pin_config(uint32_t pin_config)
{
	uint32_t mux_addr = GPIOCTRL_BASE + (pin_config >> 16);
	uint32_t shift = (pin_config >> 8) & 0xFFu;
	uint32_t mask = 3u << shift;

	MMIO32(mux_addr) &= ~mask;
	MMIO32(mux_addr + GPIO_MUX_TO_GMUX) =
		(MMIO32(mux_addr + GPIO_MUX_TO_GMUX) & ~mask) |
		(((pin_config >> 2) & 3u) << shift);
	MMIO32(mux_addr) |= (pin_config & 3u) << shift;
}

static volatile uint32_t *gpio_data_reg(uint32_t pin)
{
	return (volatile uint32_t *)(uintptr_t)GPIODATA_BASE +
	       ((pin / 32u) * GPIO_DATA_REGS_STEP);
}

static void gpio_dir_out(uint32_t pin)
{
	volatile uint32_t *reg = gpio_data_reg(pin);
	uint32_t mask = 1u << (pin % 32u);

	reg[GPIO_GPxDIR_INDEX] |= mask;
}

static void gpio_write(uint32_t pin, uint32_t level)
{
	volatile uint32_t *reg = gpio_data_reg(pin);
	uint32_t mask = 1u << (pin % 32u);

	if (level == 0u)
		reg[GPIO_GPxCLEAR_INDEX] = mask;
	else
		reg[GPIO_GPxSET_INDEX] = mask;
}

static void gpio_toggle(uint32_t pin)
{
	volatile uint32_t *reg = gpio_data_reg(pin);

	reg[GPIO_GPxTOGGLE_INDEX] = 1u << (pin % 32u);
}

static uint32_t led_to_gpio(uint32_t led)
{
	if (led == 0u)
		return ULMK_BOARD_LED4_GPIO;
	if (led == 1u)
		return ULMK_BOARD_LED5_GPIO;
	return ULMK_BOARD_LED4_GPIO;
}

void board_leds_init(void)
{
	if (g_leds_ready)
		return;

	gpio_set_pin_config(GPIO_19_GPIO19);
	gpio_set_pin_config(GPIO_62_GPIO62);
	gpio_dir_out(ULMK_BOARD_LED4_GPIO);
	gpio_dir_out(ULMK_BOARD_LED5_GPIO);

	/* Active-low: drive high = LED off. */
	gpio_write(ULMK_BOARD_LED4_GPIO, 1u);
	gpio_write(ULMK_BOARD_LED5_GPIO, 1u);
	g_led_on[0] = 0u;
	g_led_on[1] = 0u;
	g_leds_ready = true;
}

void board_led_set(uint32_t led, int on)
{
	uint32_t pin;

	if (led > 1u)
		return;
	board_leds_init();
	pin = led_to_gpio(led);
	/* Active-low: on → drive 0. */
	gpio_write(pin, on ? 0u : 1u);
	g_led_on[led] = on ? 1u : 0u;
}

void board_led_toggle(uint32_t led)
{
	if (led > 1u)
		return;
	board_leds_init();
	gpio_toggle(led_to_gpio(led));
	g_led_on[led] ^= 1u;
}

int board_leds_set(uint32_t led, int on)
{
	board_led_set(led, on);
	return 0;
}

int board_leds_get(uint32_t led, int *on)
{
	if (led > 1u || !on)
		return -1;
	board_leds_init();
	*on = (int)g_led_on[led];
	return 0;
}
