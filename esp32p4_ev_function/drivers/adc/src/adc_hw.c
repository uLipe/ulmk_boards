/* SPDX-License-Identifier: MIT */
/*
 * SAR ADC1 through the digital controller, results delivered by AHB-PDMA.
 *
 * The RTC one-shot path has no completion interrupt, so it can only be read
 * by spinning on a done flag.  The digital controller scans a pattern table
 * on its own timer and pushes every conversion into the DMA, which raises
 * end-of-frame once the programmed number of results has landed — one wait,
 * no polling, and all channels sampled in a single pass.
 *
 * Channels 0..3 → GPIO16..19 (BL/RST on GPIO26/27).
 */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_cache.h"
#include "adc_internal.h"
#include "dma.h"

#define LPPERI_BASE		0x50120000u
#define LPPERI_CLK_EN		(LPPERI_BASE + 0x00u)
#define LPPERI_RST_EN		(LPPERI_BASE + 0x04u)
#define LP_ADC_BASE		0x50127000u
#define READER1_CTRL		(LP_ADC_BASE + 0x00u)
#define MEAS1_CTRL2		(LP_ADC_BASE + 0x0cu)
#define MEAS1_MUX		(LP_ADC_BASE + 0x10u)
#define ATTEN1			(LP_ADC_BASE + 0x14u)
#define FORCE_WPD		(LP_ADC_BASE + 0x3cu)

#define CK_EN_LP_ADC		(1u << 23)
#define RST_EN_LP_ADC		(1u << 23)
#define CK_EN_LP_I2CMST		(1u << 27)
#define XPD_SAR_PU		3u

/*
 * Analog register bus.  The SAR's bias and sampling parameters are not
 * memory mapped; they are reached through the analog I2C master, which runs a
 * whole transaction per access and reports completion in a busy bit.  Without
 * programming them the comparator sits at a rail whatever the input does.
 */
#define ANA_MST_BASE		0x50124000u
#define ANA_MST_I2C0_CTRL	(ANA_MST_BASE + 0x00u)
#define ANA_MST_ANA_CONF1	(ANA_MST_BASE + 0x1cu)
#define ANA_MST_ANA_CONF2	(ANA_MST_BASE + 0x20u)
#define ANA_MST_CLK160M		(ANA_MST_BASE + 0x34u)
#define ANA_MST_SEL_160M	(1u << 0)
#define ANA_MST_SAR_SEL		(1u << 7)

#define I2C_CTRL_BUSY		(1u << 25)
#define I2C_CTRL_WRITE		(1u << 24)
#define I2C_CTRL_DATA_S		16
#define I2C_CTRL_ADDR_S		8

#define REGI2C_SAR_BLOCK	0x69u
/* Register 2 holds the sampling cycle count in [2:0] and DREF in [6:4]. */
#define SAR1_CFG_ADDR		0x02u
#define SAR1_SAMPLE_CYCLE	2u
#define SAR1_DREF		4u

#define START_FORCE		(1u << 18)
#define EN_PAD_FORCE		(1u << 31)
#define SAR1_DIG_FORCE		(1u << 31)

/* Digital controller. */
#define ADC_BASE		0x500DE000u
#define ADC_CTRL		(ADC_BASE + 0x00u)
#define ADC_CTRL2		(ADC_BASE + 0x04u)
#define ADC_FSM_WAIT		(ADC_BASE + 0x0cu)
#define ADC_PATT_TAB(i)		(ADC_BASE + 0x18u + (i) * 4u)
#define ADC_DMA_CONF		(ADC_BASE + 0x60u)

#define CTRL_SAR_CLK_GATED	(1u << 5)
#define CTRL_SAR_CLK_DIV_S	6
#define CTRL_SAR1_PATT_LEN_S	14
#define CTRL_SAR1_PATT_P_CLEAR	(1u << 22)
#define CTRL_DATA_SAR_SEL	(1u << 24)
#define CTRL_XPD_SAR1_FORCE_S	26

#define CTRL2_TIMER_SEL		(1u << 11)
#define CTRL2_TIMER_TARGET_S	12
#define CTRL2_TIMER_EN		(1u << 24)

#define DMA_CONF_RESET_FSM	(1u << 30)
#define DMA_CONF_TRANS		(1u << 31)

/* Clock and reset for the digital controller live in the HP domain. */
#define HP_CLKRST		0x500E6000u
#define HP_SOC_CLK_CTRL2	(HP_CLKRST + 0x1cu)
#define HP_PERI_CLK_CTRL22	(HP_CLKRST + 0x9cu)
#define HP_PERI_CLK_CTRL23	(HP_CLKRST + 0xa0u)
#define HP_RST_EN2		(HP_CLKRST + 0xc8u)
#define ADC_APB_CLK_EN		(1u << 5)
#define ADC_CLK_EN		(1u << 0)
#define RST_EN_ADC		(1u << 10)

/*
 * Source is the 40 MHz crystal, always running.  The divider matches what the
 * reference driver uses so the controller sees 2.5 MHz, and the trigger
 * interval is expressed in those ticks: interval = clk / 2 / sample rate.
 */
#define ADC_SRC_HZ		40000000u
#define ADC_CLK_DIV_NUM		15u
#define ADC_CLK_DIV_A		0u
#define ADC_CLK_DIV_B		1u
#define ADC_SAMPLE_HZ		20000u
#define ADC_ATTEN		3u	/* full scale, ~0..3.1 V */

/*
 * One frame covers the pattern several times; averaging the passes takes the
 * jitter out of a potentiometer reading.  16 results is also exactly one
 * cache line, so the invalidate after the transfer touches nothing else.
 */
#define ADC_PASSES		4u
#define ADC_FRAME_CONV		(ADC_CH_MAX * ADC_PASSES)
#define ADC_FRAME_BYTES		(ADC_FRAME_CONV * 4u)

#define RES_DATA(w)		((w) & 0xfffu)
#define RES_CHAN(w)		(((w) >> 13) & 0xfu)
#define RES_UNIT(w)		(((w) >> 17) & 0x1u)

#define IOMUX_BASE		ULMK_BOARD_IOMUX_BASE
#define IOMUX_GPIO(n)		(*(volatile uint32_t *)(IOMUX_BASE + 0x04u + 4u * (n)))

static const uint8_t g_ch_gpio[ADC_CH_MAX] = { 16u, 17u, 18u, 19u };

static uint32_t g_frame[ADC_FRAME_CONV] __attribute__((aligned(64)));

static inline void wr(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
}

static inline uint32_t rd(uint32_t a)
{
	return *(volatile uint32_t *)(uintptr_t)a;
}

/*
 * One transaction on the analog bus.  The busy bit drops after a handful of
 * bus cycles, so this is a hardware settle spin, not a wait for an event.
 */
#define REGI2C_SETTLE	1000u

static void regi2c_idle(void)
{
	uint32_t i;

	for (i = 0u; i < REGI2C_SETTLE; i++) {
		if ((rd(ANA_MST_I2C0_CTRL) & I2C_CTRL_BUSY) == 0u)
			return;
	}
}

static uint8_t regi2c_xfer(uint8_t reg, int write, uint8_t data)
{
	uint32_t cmd;

	wr(ANA_MST_ANA_CONF2, 0u);
	wr(ANA_MST_ANA_CONF1, 0u);
	wr(ANA_MST_ANA_CONF2, ANA_MST_SAR_SEL);

	regi2c_idle();
	cmd = REGI2C_SAR_BLOCK | ((uint32_t)reg << I2C_CTRL_ADDR_S);
	if (write)
		cmd |= I2C_CTRL_WRITE | ((uint32_t)data << I2C_CTRL_DATA_S);
	wr(ANA_MST_I2C0_CTRL, cmd);
	regi2c_idle();

	return (uint8_t)((rd(ANA_MST_I2C0_CTRL) >> I2C_CTRL_DATA_S) & 0xFFu);
}

static void regi2c_write_field(uint8_t reg, uint8_t msb, uint8_t lsb,
			       uint8_t val)
{
	uint32_t mask = (0xFFu >> (7u - (msb - lsb))) << lsb;
	uint8_t cur = regi2c_xfer(reg, 0, 0u);
	uint8_t out;

	out = (uint8_t)((cur & ~mask) | (((uint32_t)val << lsb) & mask));
	(void)regi2c_xfer(reg, 1, out);
}

static void adc_analog_init(void)
{
	uint32_t v;

	v = rd(LPPERI_CLK_EN);
	wr(LPPERI_CLK_EN, v | CK_EN_LP_I2CMST);
	wr(ANA_MST_CLK160M, rd(ANA_MST_CLK160M) | ANA_MST_SEL_160M);

	regi2c_write_field(SAR1_CFG_ADDR, 2u, 0u, SAR1_SAMPLE_CYCLE);
	regi2c_write_field(SAR1_CFG_ADDR, 6u, 4u, SAR1_DREF);
}

static void adc_pin_analog(uint8_t gpio)
{
	uint32_t mux;

	/*
	 * MCU_SEL=0 (GPIO matrix / analog pad path on P4 IOMUX): clear
	 * digital IE and pulls so the SAR pad is not driven.
	 */
	mux = IOMUX_GPIO(gpio);
	mux &= ~(0x7u << 12);
	mux &= ~((1u << 9) | (1u << 8) | (1u << 7));
	IOMUX_GPIO(gpio) = mux;
}

/*
 * Entry k of the table sits in word k/4, and within a word the entries run
 * from the top of bits [23:0] downwards, six bits each.
 */
static void adc_pattern_set(uint32_t idx, uint8_t ch, uint8_t atten)
{
	uint32_t word = idx / 4u;
	uint32_t shift = (idx % 4u) * 6u;
	uint32_t entry = (atten & 0x3u) | ((uint32_t)(ch & 0xfu) << 2);
	uint32_t tab;

	tab = rd(ADC_PATT_TAB(word));
	tab &= ~(0xFC0000u >> shift);
	tab |= ((entry & 0x3Fu) << 18) >> shift;
	wr(ADC_PATT_TAB(word), tab);
}

static void adc_clk_init(void)
{
	uint32_t v;

	v = rd(HP_SOC_CLK_CTRL2);
	wr(HP_SOC_CLK_CTRL2, v | ADC_APB_CLK_EN);

	v = rd(HP_RST_EN2);
	wr(HP_RST_EN2, v | RST_EN_ADC);
	wr(HP_RST_EN2, v & ~RST_EN_ADC);

	v = rd(HP_PERI_CLK_CTRL23);
	v &= ~((0xFFu << 1) | (0xFFu << 9) | (0xFFu << 17));
	v |= (ADC_CLK_DIV_NUM << 1) | (ADC_CLK_DIV_A << 9) |
	     (ADC_CLK_DIV_B << 17) | ADC_CLK_EN;
	wr(HP_PERI_CLK_CTRL23, v);

	v = rd(HP_PERI_CLK_CTRL22);
	v &= ~(0x3u << 30);	/* source select 0 = XTAL */
	wr(HP_PERI_CLK_CTRL22, v);
}

static uint32_t adc_trigger_interval(void)
{
	uint32_t div = ADC_CLK_DIV_NUM + 1u; /* fractional part is zero here */

	return ADC_SRC_HZ / div / 2u / ADC_SAMPLE_HZ;
}

int adc_hw_init(void)
{
	uint32_t v;
	uint32_t i;

	v = rd(LPPERI_CLK_EN);
	wr(LPPERI_CLK_EN, v | CK_EN_LP_ADC);

	v = rd(LPPERI_RST_EN);
	wr(LPPERI_RST_EN, v | RST_EN_LP_ADC);
	wr(LPPERI_RST_EN, v & ~RST_EN_LP_ADC);

	wr(FORCE_WPD, XPD_SAR_PU);
	wr(ATTEN1, 0xFFFFFFFFu);
	wr(READER1_CTRL, (2u << 0) | (1u << 28));

	/* Hand the SAR over to the digital controller. */
	wr(MEAS1_MUX, SAR1_DIG_FORCE);
	wr(MEAS1_CTRL2, START_FORCE | EN_PAD_FORCE);

	for (i = 0u; i < ADC_CH_MAX; i++)
		adc_pin_analog(g_ch_gpio[i]);

	adc_analog_init();
	adc_clk_init();

	wr(ADC_FSM_WAIT, (100u << 16) | (8u << 8) | 5u);

	/*
	 * work_mode 0 / sar_sel 0 selects ADC1 only; data_sar_sel makes the
	 * result carry its channel so a frame can be demultiplexed.
	 */
	wr(ADC_CTRL, CTRL_SAR_CLK_GATED | (1u << CTRL_SAR_CLK_DIV_S) |
		     CTRL_DATA_SAR_SEL |
		     ((ADC_CH_MAX - 1u) << CTRL_SAR1_PATT_LEN_S) |
		     (XPD_SAR_PU << CTRL_XPD_SAR1_FORCE_S));

	for (i = 0u; i < 4u; i++)
		wr(ADC_PATT_TAB(i), 0xFFFFFFu);
	for (i = 0u; i < ADC_CH_MAX; i++)
		adc_pattern_set(i, (uint8_t)i, ADC_ATTEN);

	wr(ADC_CTRL2, CTRL2_TIMER_SEL |
		      (adc_trigger_interval() << CTRL2_TIMER_TARGET_S));
	wr(ADC_DMA_CONF, ADC_FRAME_CONV);

	return dma_channel_open(DMA_SLOT_PERI, DMA_PERIPH_ADC0, 0u);
}

static void adc_run(int on)
{
	uint32_t ctrl2 = rd(ADC_CTRL2);

	if (on) {
		wr(ADC_DMA_CONF, ADC_FRAME_CONV | DMA_CONF_TRANS);
		wr(ADC_CTRL2, ctrl2 | CTRL2_TIMER_EN);
	} else {
		wr(ADC_CTRL2, ctrl2 & ~CTRL2_TIMER_EN);
		wr(ADC_DMA_CONF, ADC_FRAME_CONV);
	}
}

int adc_hw_scan(uint16_t *out)
{
	uint32_t sum[ADC_CH_MAX] = {0};
	uint32_t cnt[ADC_CH_MAX] = {0};
	uint32_t ctrl;
	uint32_t i;
	int got;

	if (!out)
		return ULMK_EINVAL;

	adc_run(0);

	/* Restart the FSM and the pattern pointer so a frame begins at ch0. */
	wr(ADC_DMA_CONF, ADC_FRAME_CONV | DMA_CONF_RESET_FSM);
	wr(ADC_DMA_CONF, ADC_FRAME_CONV);
	ctrl = rd(ADC_CTRL);
	wr(ADC_CTRL, ctrl | CTRL_SAR1_PATT_P_CLEAR);
	wr(ADC_CTRL, ctrl);

	got = dma_rx_arm(DMA_SLOT_PERI, g_frame, ADC_FRAME_BYTES);
	if (got != ULMK_OK)
		return got;

	adc_run(1);
	got = dma_rx_wait(DMA_SLOT_PERI);
	adc_run(0);

	if (got < 0)
		return got;

	for (i = 0u; i < ((uint32_t)got / 4u) && i < ADC_FRAME_CONV; i++) {
		uint32_t w = g_frame[i];
		uint32_t ch = RES_CHAN(w);

		if (RES_UNIT(w) != 0u || ch >= ADC_CH_MAX)
			continue;
		sum[ch] += RES_DATA(w);
		cnt[ch]++;
	}

	for (i = 0u; i < ADC_CH_MAX; i++) {
		if (cnt[i] == 0u)
			return ULMK_ETIMEOUT;
		out[i] = (uint16_t)(sum[i] / cnt[i]);
	}
	return ULMK_OK;
}

int adc_hw_read(uint8_t ch, uint16_t *out)
{
	uint16_t all[ADC_CH_MAX];
	int rc;

	if (!out || ch >= ADC_CH_MAX)
		return ULMK_EINVAL;

	rc = adc_hw_scan(all);
	if (rc != ULMK_OK)
		return rc;
	*out = all[ch];
	return ULMK_OK;
}
