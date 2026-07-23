/* SPDX-License-Identifier: MIT */
/*
 * board_irq_attach_blinky — STM1 CMP0 → irq_attach fast-path.
 *
 * UP-only: STM1 SR0 is CPU1 tick in SMP.
 *
 * Attach callback (no syscalls): rearm STM1 CMP0 and return true so the
 * kernel acks SRC + signals the attach notif.
 *
 * LED1/LED2: pinmux+gpio via board_leds_* in the waiting thread (same path
 * as board_blinky).  ISR must not call board_leds — that is IPC/syscalls.
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_console.h"
#include "board_leds.h"

void board_services_init(const ulmk_boot_info_t *info);

#define STM_OFF_TIM0	0x010u
#define STM_OFF_CMP0	0x030u
#define STM_OFF_CMCON	0x038u
#define STM_OFF_ICR	0x03Cu
#define STM_OFF_ISCR	0x040u
#define STM_ICR_CMP0EN		(1u << 0)
#define STM_ISCR_CMP0IRR	(1u << 0)

#define IRQ_BIT			(1u << 0)

/* ~5 Hz at fSTM = 100 MHz */
#define ATTACH_PERIOD_STM	(ULMK_BOARD_FSTM_HZ / 5u)

struct attach_ctx {
	volatile uint32_t *stm;
	volatile uint32_t hits;
};

static struct attach_ctx g_ctx __attribute__((section(".user_bss")));

static inline volatile uint32_t *stm_reg(volatile uint32_t *base, uint32_t off)
{
	return (volatile uint32_t *)((uintptr_t)base + off);
}

static bool stm1_attach_cb(void *data)
{
	struct attach_ctx *ctx = (struct attach_ctx *)data;
	uint32_t now;

	*stm_reg(ctx->stm, STM_OFF_ISCR) = STM_ISCR_CMP0IRR;
	now = *stm_reg(ctx->stm, STM_OFF_TIM0);
	*stm_reg(ctx->stm, STM_OFF_CMP0) = now + ATTACH_PERIOD_STM;
	*stm_reg(ctx->stm, STM_OFF_ISCR) = STM_ISCR_CMP0IRR;
	ctx->hits++;

	/* Kernel acks SRC + signals attach notif → thread toggles LEDs. */
	return true;
}

static void arm_stm1(volatile uint32_t *stm)
{
	uint32_t now;

	*stm_reg(stm, STM_OFF_CMCON) = 0x1Fu;
	*stm_reg(stm, STM_OFF_ICR) = 0u;
	*stm_reg(stm, STM_OFF_ISCR) = STM_ISCR_CMP0IRR;
	now = *stm_reg(stm, STM_OFF_TIM0);
	*stm_reg(stm, STM_OFF_CMP0) = now + ATTACH_PERIOD_STM;
	*stm_reg(stm, STM_OFF_ISCR) = STM_ISCR_CMP0IRR;
	*stm_reg(stm, STM_OFF_ICR) = STM_ICR_CMP0EN;
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_notif_t n;
	uint32_t bits;
	int phase;
	int rc;

	board_services_init(info);

	/*
	 * pinmux GPIO out on P00.5/P00.6 (active-low).  board_services may
	 * already have done this; fail loudly if pads are not ready.
	 */
	rc = board_leds_init();
	if (rc != ULMK_OK) {
		board_console_printf(
			"irq_attach_blinky: leds/pinmux FAIL rc=%d\n", rc);
		ulmk_thread_exit();
	}
	(void)board_leds_set(BOARD_LED_1, 1);
	(void)board_leds_set(BOARD_LED_2, 0);

	board_console_puts(
		"irq_attach_blinky: STM1 attach → LED1/LED2 @ ~5 Hz\n");

	g_ctx.stm = (volatile uint32_t *)(uintptr_t)ULMK_BOARD_STM1_BASE;
	g_ctx.hits = 0u;

	n = ulmk_irq_attach_hw(ULMK_BOARD_IRQ_STM1_ATTACH, stm1_attach_cb,
			       &g_ctx, ULMK_BOARD_SRC_STM1_SR0);
	if (n == ULMK_NOTIF_INVALID ||
	    ((int32_t)n < 0 && (int32_t)n >= -16)) {
		if ((int)n == ULMK_ENOTSUP)
			board_console_puts(
				"irq_attach_blinky: ENOTSUP "
				"(ULMK_CONFIG_IRQ_ATTACH=0)\n");
		else
			board_console_puts("irq_attach_blinky: attach FAIL\n");
		ulmk_thread_exit();
	}

	arm_stm1(g_ctx.stm);
	if (ulmk_irq_enable(ULMK_BOARD_IRQ_STM1_ATTACH) != ULMK_OK) {
		board_console_puts("irq_attach_blinky: enable FAIL\n");
		ulmk_thread_exit();
	}

	board_console_puts("irq_attach_blinky: attach ok (LEDs via gpio)\n");

	phase = 0;
	for (;;) {
		bits = 0u;
		if (ulmk_notif_wait(n, IRQ_BIT, &bits) != ULMK_OK)
			continue;
		if (!(bits & IRQ_BIT))
			continue;

		phase ^= 1;
		(void)board_leds_set(BOARD_LED_1, phase);
		(void)board_leds_set(BOARD_LED_2, !phase);

		if ((g_ctx.hits % 10u) == 0u)
			board_console_printf(
				"irq_attach_blinky: heartbeat hits=%u\n",
				(unsigned)g_ctx.hits);
	}
}
