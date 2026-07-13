/* SPDX-License-Identifier: MIT */
/*
 * pwm server — GTM TOM PWM (no busy-wait).
 *
 * Hardware generates the waveform; duty updates are register writes.
 * Fade timing belongs in the app via board_timer_sleep_us().
 *
 * Lite Kit LEDs (active-low): P00.5 → TOM0 CH12 / TOUT14,
 * P00.6 → TOM0 CH13 / TOUT15 (ToutSel_a, port alt1).
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "pwm_internal.h"
#include "board_config.h"
#include "pinmux_internal.h"
#include "IfxGtm_reg.h"
#include "Gtm/Std/IfxGtm_Tom.h"

#define PWM_STACK		2048u
#define PWM_TOM_MAP_SIZE	0x1000u
#define PWM_CMU_MAP_SIZE	0x100u
#define PWM_TOUT_MAP_SIZE	0x40u
#define PWM_FXCLK_DIV		16u	/* CMU FXCLK1 */

struct pwm_ch {
	uint8_t  port;
	uint8_t  pin;
	uint8_t  alt;
	uint8_t  tom_ch;
	uint8_t  tout_n;
	uint8_t  tout_sel;
	uint16_t period;
	uint32_t duty;
	int      on;
	int      used;
};

struct pwm_board_ch {
	uint8_t port;
	uint8_t pin;
	uint8_t alt;
	uint8_t tom_ch;
	uint8_t tout_n;
};

static const struct pwm_board_ch g_board_ch[PWM_MAX_CH] = {
	{
		ULMK_BOARD_PWM0_PORT, ULMK_BOARD_PWM0_PIN,
		ULMK_BOARD_PWM0_ALT, ULMK_BOARD_PWM0_TOM_CH,
		ULMK_BOARD_PWM0_TOUT,
	},
	{
		ULMK_BOARD_PWM1_PORT, ULMK_BOARD_PWM1_PIN,
		ULMK_BOARD_PWM1_ALT, ULMK_BOARD_PWM1_TOM_CH,
		ULMK_BOARD_PWM1_TOUT,
	},
};

ulmk_ep_t g_pwm_ep;
static struct pwm_ch g_ch[PWM_MAX_CH] __attribute__((section(".user_bss")));
static Ifx_GTM_TOM *g_tom __attribute__((section(".user_bss")));
static volatile uint32_t *g_cmu_clk_en __attribute__((section(".user_bss")));
static volatile uint32_t *g_cmu_gclk_num __attribute__((section(".user_bss")));
static volatile uint32_t *g_cmu_gclk_den __attribute__((section(".user_bss")));
static volatile uint32_t *g_toutsel __attribute__((section(".user_bss")));
static uint8_t g_cmu_ready __attribute__((section(".user_bss")));

static void rmw32(volatile void *reg, uint32_t mask, uint32_t val)
{
	volatile unsigned int *r = reg;

	*r = (*r & ~mask) | (val & mask);
}

static int resolve_route(uint8_t n, uint8_t *tom_ch, uint8_t *tout_n,
			 uint8_t *tout_sel)
{
	if (n >= PWM_MAX_CH)
		return -1;
	*tom_ch = g_board_ch[n].tom_ch;
	*tout_n = g_board_ch[n].tout_n;
	*tout_sel = 0u; /* ToutSel_a */
	return 0;
}

static void cmu_ensure(void)
{
	if (g_cmu_ready)
		return;
	/* GCLK = cluster clock (NUM/DEN = 1). */
	*g_cmu_gclk_num = 1u;
	*g_cmu_gclk_den = 1u;
	/* Enable FXCLK (TOM). CLK_EN uses enable-bit pairs; see IFXGTM_CMU_*. */
	*g_cmu_clk_en = 0x00800000u; /* IFXGTM_CMU_CLKEN_FXCLK */
	g_cmu_ready = 1u;
}

static void tout_route(uint8_t tout_n, uint8_t tout_sel)
{
	uint32_t reg_i;
	uint32_t shift;
	uint32_t mask;
	uint32_t val;

	reg_i = (uint32_t)tout_n >> 4;
	shift = ((uint32_t)tout_n & 0xFu) * 2u;
	mask = 3u << shift;
	val = (uint32_t)tout_sel << shift;
	rmw32(&g_toutsel[reg_i], mask, val);
}

static void pin_tom_alt(uint8_t port, uint8_t pin, uint8_t alt)
{
	pinmux_cfg_t cfg;

	cfg.port  = port;
	cfg.pin   = pin;
	cfg.dir   = PINMUX_DIR_OUT;
	cfg.pull  = PINMUX_PULL_NONE;
	cfg.alt   = alt ? alt : 1u;
	cfg.flags = 0u;
	(void)pinmux_apply(&cfg);
}

static uint16_t freq_to_period(uint32_t freq_hz)
{
	uint32_t ticks;

	if (freq_hz == 0u)
		freq_hz = 1000u;
	ticks = ULMK_BOARD_FGTM_HZ / (PWM_FXCLK_DIV * freq_hz);
	if (ticks < 2u)
		ticks = 2u;
	if (ticks > 0xFFFFu)
		ticks = 0xFFFFu;
	return (uint16_t)ticks;
}

static uint16_t duty_to_cm1(uint16_t period, uint32_t duty_permille)
{
	uint32_t cm1;

	if (duty_permille > 1000u)
		duty_permille = 1000u;
	cm1 = ((uint32_t)period * duty_permille) / 1000u;
	if (cm1 > period)
		cm1 = period;
	return (uint16_t)cm1;
}

static void ch_apply_duty(uint8_t ch)
{
	IfxGtm_Tom_Ch tch;
	uint16_t cm1;

	if (!g_ch[ch].used || !g_tom)
		return;
	tch = (IfxGtm_Tom_Ch)g_ch[ch].tom_ch;
	if (!g_ch[ch].on)
		cm1 = 0u;
	else
		cm1 = duty_to_cm1(g_ch[ch].period, g_ch[ch].duty);
	/*
	 * While the channel is running, CM1 is reloaded from SR1 on each
	 * period reset when UPEN is set. Write shadows (and CM for the
	 * current period) so fade steps actually take effect.
	 */
	IfxGtm_Tom_Ch_setCompareShadow(g_tom, tch, g_ch[ch].period, cm1);
	IfxGtm_Tom_Ch_setCompareOne(g_tom, tch, cm1);
}

static void tgc_set_ch(Ifx_GTM_TOM_TGC *tgc, uint8_t ch, int on)
{
	uint32_t shift = ((uint32_t)(ch & 7u) << 1);
	uint32_t mask = 3u << shift;
	uint32_t val = (on ? 2u : 1u) << shift; /* enable=2, disable=1 */

	rmw32(&tgc->ENDIS_CTRL.U, mask, val);
	rmw32(&tgc->ENDIS_STAT.U, mask, val);
	rmw32(&tgc->OUTEN_CTRL.U, mask, val);
	rmw32(&tgc->OUTEN_STAT.U, mask, val);
}

/* UPEN_CTRLx lives in GLB_CTRL[16+]; 00=no change, 10=enable (bit-pair). */
static void tgc_enable_upen(Ifx_GTM_TOM_TGC *tgc, uint8_t ch)
{
	uint32_t shift = 16u + ((uint32_t)(ch & 7u) << 1);

	tgc->GLB_CTRL.U = 2u << shift;
}

/* FUPD + RSTCN0 so shadows/counters load on HOST_TRIG. */
static void tgc_force_update(Ifx_GTM_TOM_TGC *tgc, uint8_t ch)
{
	uint32_t shift = ((uint32_t)(ch & 7u) << 1);

	tgc->FUPD_CTRL.U = (2u << shift) | (2u << (16u + shift));
}

static void tgc_trigger(Ifx_GTM_TOM_TGC *tgc)
{
	tgc->GLB_CTRL.U = 1u; /* HOST_TRIG (UPEN bit-pairs = 00 → no change) */
}

static int ch_hw_start(uint8_t ch)
{
	IfxGtm_Tom_Ch tch;
	Ifx_GTM_TOM_TGC *tgc;
	uint16_t cm1;

	cmu_ensure();
	tch = (IfxGtm_Tom_Ch)g_ch[ch].tom_ch;
	tgc = IfxGtm_Tom_Ch_getTgcPointer(g_tom, 1u); /* CH8..15 → TGC1 */

	tout_route(g_ch[ch].tout_n, g_ch[ch].tout_sel);
	pin_tom_alt(g_ch[ch].port, g_ch[ch].pin, g_ch[ch].alt);

	IfxGtm_Tom_Ch_setClockSource(g_tom, tch, IfxGtm_Tom_Ch_ClkSrc_cmuFxclk1);
	IfxGtm_Tom_Ch_setResetSource(g_tom, tch, IfxGtm_Tom_Ch_ResetEvent_onCm0);
	/* Active-low LEDs: SL=0 → low during duty (LED on). */
	IfxGtm_Tom_Ch_setSignalLevel(g_tom, tch, Ifx_ActiveState_low);

	/* UPEN: SR0/SR1 → CM0/CM1 on each CN0 reset (required for live duty). */
	tgc_enable_upen(tgc, (uint8_t)tch);
	tgc_force_update(tgc, (uint8_t)tch);

	cm1 = duty_to_cm1(g_ch[ch].period, g_ch[ch].on ? g_ch[ch].duty : 0u);
	IfxGtm_Tom_Ch_setCompareZero(g_tom, tch, g_ch[ch].period);
	IfxGtm_Tom_Ch_setCompareOne(g_tom, tch, cm1);
	IfxGtm_Tom_Ch_setCompareShadow(g_tom, tch, g_ch[ch].period, cm1);
	IfxGtm_Tom_Ch_setCounterValue(g_tom, tch, 0u);

	tgc_set_ch(tgc, (uint8_t)tch, 1);
	tgc_trigger(tgc);
	return ULMK_OK;
}

static void ch_hw_stop(uint8_t ch)
{
	IfxGtm_Tom_Ch tch;
	Ifx_GTM_TOM_TGC *tgc;

	if (!g_ch[ch].used || !g_tom)
		return;
	tch = (IfxGtm_Tom_Ch)g_ch[ch].tom_ch;
	tgc = IfxGtm_Tom_Ch_getTgcPointer(g_tom, 1u);
	tgc_set_ch(tgc, (uint8_t)tch, 0);
	tgc_trigger(tgc);
}

static void pwm_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	uint8_t ch;
	uint8_t tom_ch;
	uint8_t tout_n;
	uint8_t tout_sel;
	void *mapped;

	(void)arg;

	mapped = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_GTM_TOM0_BASE,
			      PWM_TOM_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE,
			      ULMK_MMAP_PERIPH);
	if (!mapped)
		for (;;)
			;
	g_tom = (Ifx_GTM_TOM *)mapped;

	mapped = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_GTM_CMU_BASE,
			      PWM_CMU_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE,
			      ULMK_MMAP_PERIPH);
	if (!mapped)
		for (;;)
			;
	g_cmu_clk_en = (volatile uint32_t *)mapped;		/* +0x00 CLK_EN */
	g_cmu_gclk_num = g_cmu_clk_en + 1;			/* +0x04 */
	g_cmu_gclk_den = g_cmu_clk_en + 2;			/* +0x08 */

	mapped = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_GTM_TOUTSEL_BASE,
			      PWM_TOUT_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE,
			      ULMK_MMAP_PERIPH);
	if (!mapped)
		for (;;)
			;
	g_toutsel = (volatile uint32_t *)mapped;

	for (;;) {
		if (ulmk_ep_recv(g_pwm_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_OK;
		ch = (uint8_t)msg.words[0];
		if (ch >= PWM_MAX_CH) {
			reply.words[0] = (uint32_t)ULMK_EINVAL;
		} else if (msg.label == PWM_MSG_CONFIG) {
			if (resolve_route(ch, &tom_ch, &tout_n, &tout_sel) != 0) {
				reply.words[0] = (uint32_t)ULMK_EINVAL;
			} else {
				g_ch[ch].port = g_board_ch[ch].port;
				g_ch[ch].pin = g_board_ch[ch].pin;
				g_ch[ch].alt = g_board_ch[ch].alt;
				g_ch[ch].tom_ch = tom_ch;
				g_ch[ch].tout_n = tout_n;
				g_ch[ch].tout_sel = tout_sel;
				g_ch[ch].period = freq_to_period(msg.words[1]);
				g_ch[ch].duty = msg.words[2];
				g_ch[ch].used = 1;
				g_ch[ch].on = 0;
				reply.words[0] = (uint32_t)ch_hw_start(ch);
			}
		} else if (msg.label == PWM_MSG_DUTY) {
			if (!g_ch[ch].used) {
				reply.words[0] = (uint32_t)ULMK_EINVAL;
			} else {
				g_ch[ch].duty = msg.words[1];
				ch_apply_duty(ch);
			}
		} else if (msg.label == PWM_MSG_ENABLE) {
			if (!g_ch[ch].used) {
				reply.words[0] = (uint32_t)ULMK_EINVAL;
			} else {
				g_ch[ch].on = (int)msg.words[1];
				if (g_ch[ch].on)
					reply.words[0] = (uint32_t)ch_hw_start(ch);
				else
					ch_hw_stop(ch);
			}
		} else {
			reply.words[0] = (uint32_t)ULMK_EINVAL;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t pwm_init(uint8_t mod)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	if (mod != 0u)
		return ULMK_TID_INVALID;
	if (g_pwm_ep != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	g_pwm_ep = ulmk_ep_create();
	if (g_pwm_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	attr.name = "pwm";
	attr.entry = pwm_server;
	attr.priority = 3u;
	attr.stack_size = PWM_STACK;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(g_pwm_ep);
		g_pwm_ep = ULMK_EP_INVALID;
		return ULMK_TID_INVALID;
	}
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	return tid;
}
