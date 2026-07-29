/* SPDX-License-Identifier: MIT */
/*
 * LEDC PWM via MMIO (no IDF driver / FreeRTOS atomic wrappers).
 * Clock: XTAL 40 MHz (HP peri_clk_ctrl22 src=0). Channels → GPIO matrix.
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "pwm_internal.h"
#include "pinmux_internal.h"

#define HP_CLKRST		0x500E6000u
#define HP_SOC_CLK_CTRL3	(HP_CLKRST + 0x20u)
#define HP_PERI_CLK_CTRL22	(HP_CLKRST + 0x9cu)
#define HP_RST_EN1		(HP_CLKRST + 0xc4u)

#define LEDC_BASE		ULMK_BOARD_LEDC_BASE
#define LEDC_CH_STRIDE		0x14u
#define LEDC_CH_CONF0(n)	(LEDC_BASE + (n) * LEDC_CH_STRIDE + 0x00u)
#define LEDC_CH_HPOINT(n)	(LEDC_BASE + (n) * LEDC_CH_STRIDE + 0x04u)
#define LEDC_CH_DUTY(n)		(LEDC_BASE + (n) * LEDC_CH_STRIDE + 0x08u)
#define LEDC_CH_CONF1(n)	(LEDC_BASE + (n) * LEDC_CH_STRIDE + 0x0cu)
#define LEDC_TIMER0_CONF	(LEDC_BASE + 0xa0u)
#define LEDC_CONF		(LEDC_BASE + 0x170u)

#define LEDC_SIG_OUT0		126u
#define DUTY_RES_BITS		10u
#define DUTY_PERIOD		(1u << DUTY_RES_BITS) /* 1024 ticks */
#define DUTY_FULL		(DUTY_PERIOD - 1u) /* 1023 — ESP LEDC max */
#define XTAL_HZ			40000000u

static inline void wr(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
}

static inline uint32_t rd(uint32_t a)
{
	return *(volatile uint32_t *)(uintptr_t)a;
}

static void ledc_clk_enable(void)
{
	uint32_t v;

	v = rd(HP_SOC_CLK_CTRL3);
	wr(HP_SOC_CLK_CTRL3, v | (1u << 0)); /* ledc_apb_clk_en */

	v = rd(HP_RST_EN1);
	wr(HP_RST_EN1, v | (1u << 29)); /* rst_en_ledc */
	wr(HP_RST_EN1, v & ~(1u << 29));

	/* peri: src=XTAL (0), clk_en=1 */
	v = rd(HP_PERI_CLK_CTRL22);
	v &= ~0x7u;
	v |= (1u << 2);
	wr(HP_PERI_CLK_CTRL22, v);

	/* Force LEDC register clock gate open */
	wr(LEDC_CONF, rd(LEDC_CONF) | (1u << 31));
}

static void gpio_matrix_ledc(uint8_t gpio, uint8_t ch)
{
	pinmux_cfg_t cfg = {0};

	cfg.pin = gpio;
	cfg.dir = PINMUX_DIR_OUT;
	cfg.pull = PINMUX_PULL_NONE;
	cfg.alt = PINMUX_ALT_MATRIX;
	cfg.flags = PINMUX_F_PERIPH_OE;
	cfg.matrix_out = (uint16_t)(LEDC_SIG_OUT0 + ch);
	(void)pinmux_apply(&cfg);
}

static void timer0_set_freq(uint32_t freq_hz)
{
	uint32_t div;
	uint32_t conf;

	if (freq_hz == 0u)
		freq_hz = 5000u;

	div = (XTAL_HZ << 8) / (freq_hz * DUTY_PERIOD);
	if (div < 256u)
		div = 256u;

	conf = (DUTY_RES_BITS & 0x1fu) | ((div & 0x3ffffu) << 5);
	wr(LEDC_TIMER0_CONF, conf | (1u << 24)); /* rst */
	wr(LEDC_TIMER0_CONF, conf); /* run */
	wr(LEDC_TIMER0_CONF, conf | (1u << 26)); /* para_up */
}

static void ch_apply_duty(uint8_t ch, uint32_t duty_permille, int on)
{
	uint32_t duty_val;
	uint32_t conf0;

	if (duty_permille > 1000u)
		duty_permille = 1000u;
	/* BSP: duty 0..1023 for 10-bit; 1024 is out of range and can stick low */
	duty_val = (DUTY_FULL * duty_permille) / 1000u;
	if (!on)
		duty_val = 0u;

	wr(LEDC_CH_HPOINT(ch), 0u);
	wr(LEDC_CH_DUTY(ch), duty_val << 4);
	wr(LEDC_CH_CONF1(ch), 1u << 31); /* duty_start */

	conf0 = 0u; /* timer0 */
	if (on)
		conf0 |= (1u << 2); /* sig_out_en */
	conf0 |= (1u << 4); /* para_up */
	wr(LEDC_CH_CONF0(ch), conf0);
}

/*
 * Steady level without PWM edges: idle_lv + sig_out_en=0.
 * Use for backlight "on" when the pad must stay DC-high.
 */
int pwm_hw_set_idle_level(uint8_t ch, uint8_t gpio, int level_high)
{
	uint32_t conf0;

	if (ch >= PWM_MAX_CH)
		return ULMK_EINVAL;

	ledc_clk_enable();
	gpio_matrix_ledc(gpio, ch);

	/* timer still needed for channel to be valid; keep a sane freq */
	timer0_set_freq(5000u);

	wr(LEDC_CH_HPOINT(ch), 0u);
	wr(LEDC_CH_DUTY(ch), 0u);
	wr(LEDC_CH_CONF1(ch), 1u << 31);

	conf0 = 0u; /* timer0, sig_out_en=0 */
	if (level_high)
		conf0 |= (1u << 3); /* idle_lv = 1 */
	conf0 |= (1u << 4); /* para_up */
	wr(LEDC_CH_CONF0(ch), conf0);
	return ULMK_OK;
}

int pwm_hw_init(void)
{
	ledc_clk_enable();
	timer0_set_freq(5000u);
	return ULMK_OK;
}

int pwm_hw_config(uint8_t ch, uint8_t gpio, uint32_t freq_hz,
		  uint32_t duty_permille)
{
	if (ch >= PWM_MAX_CH)
		return ULMK_EINVAL;
	timer0_set_freq(freq_hz);
	gpio_matrix_ledc(gpio, ch);
	ch_apply_duty(ch, duty_permille, 0);
	return ULMK_OK;
}

int pwm_hw_set_duty(uint8_t ch, uint32_t duty_permille, int on)
{
	if (ch >= PWM_MAX_CH)
		return ULMK_EINVAL;
	ch_apply_duty(ch, duty_permille, on);
	return ULMK_OK;
}

int pwm_hw_enable(uint8_t ch, uint32_t duty_permille, int on)
{
	return pwm_hw_set_duty(ch, duty_permille, on);
}
