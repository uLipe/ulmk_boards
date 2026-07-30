/* SPDX-License-Identifier: MIT */
/*
 * Interrupt matrix routing + CLIC ISR hook.
 *
 * The P4 puts a routing stage (INTMTX) between each peripheral and the CLIC.
 * Programming it is privileged, so drivers cannot do it themselves; the kernel
 * calls ulmk_board_irq_connect() from the bind path instead
 * (ULMK_CONFIG_BOARD_IRQ_CTRL=1), which keeps the table below as plain data
 * rather than a startup sequence that has to run before any driver binds.
 */
#include <stdint.h>
#include <stdbool.h>
#include <ulmk/microkernel.h>
#include <ulmk/board.h>
#include <ulmk_arch.h>
#include "board_config.h"

extern void ulmk_kern_timer_tick(void);

/* Board IRQ line -> {peripheral source, CLIC slot}. */
static const struct {
	uint8_t		srpn;
	uint8_t		source;
	uint8_t		slot;
} g_routes[] = {
	{ ULMK_BOARD_IRQ_TICK, ETS_SYSTIMER_TARGET0_INTR_SOURCE,
	  ULMK_BOARD_CLIC_IRQ_TICK },
	{ ULMK_BOARD_IRQ_I2C0, ETS_I2C0_INTR_SOURCE,
	  ULMK_BOARD_CLIC_IRQ_I2C0 },
	{ ULMK_BOARD_IRQ_DW_GDMA, ETS_DW_GDMA_INTR_SOURCE,
	  ULMK_BOARD_CLIC_IRQ_DW_GDMA },
	{ ULMK_BOARD_IRQ_UART_BASE + 0u, ETS_UART0_INTR_SOURCE + 0u,
	  ULMK_BOARD_CLIC_IRQ_UART_BASE + 0u },
	{ ULMK_BOARD_IRQ_UART_BASE + 1u, ETS_UART0_INTR_SOURCE + 1u,
	  ULMK_BOARD_CLIC_IRQ_UART_BASE + 1u },
	{ ULMK_BOARD_IRQ_UART_BASE + 2u, ETS_UART0_INTR_SOURCE + 2u,
	  ULMK_BOARD_CLIC_IRQ_UART_BASE + 2u },
	{ ULMK_BOARD_IRQ_UART_BASE + 3u, ETS_UART0_INTR_SOURCE + 3u,
	  ULMK_BOARD_CLIC_IRQ_UART_BASE + 3u },
	{ ULMK_BOARD_IRQ_UART_BASE + 4u, ETS_UART0_INTR_SOURCE + 4u,
	  ULMK_BOARD_CLIC_IRQ_UART_BASE + 4u },
	{ ULMK_BOARD_IRQ_TWAI0, ETS_TWAI0_INTR_SOURCE,
	  ULMK_BOARD_CLIC_IRQ_TWAI0 },
	{ ULMK_BOARD_IRQ_PDMA_IN_CH0, ETS_AHB_PDMA_IN_CH0_INTR_SOURCE,
	  ULMK_BOARD_CLIC_IRQ_PDMA_IN_CH0 },
	{ ULMK_BOARD_IRQ_PDMA_IN_CH1, ETS_AHB_PDMA_IN_CH1_INTR_SOURCE,
	  ULMK_BOARD_CLIC_IRQ_PDMA_IN_CH1 },
	{ ULMK_BOARD_IRQ_AXI_PDMA_IN_CH0, ETS_AXI_PDMA_IN_CH0_INTR_SOURCE,
	  ULMK_BOARD_CLIC_IRQ_AXI_PDMA_IN_CH0 },
	{ ULMK_BOARD_IRQ_AXI_PDMA_IN_CH1, ETS_AXI_PDMA_IN_CH1_INTR_SOURCE,
	  ULMK_BOARD_CLIC_IRQ_AXI_PDMA_IN_CH1 },
	{ ULMK_BOARD_IRQ_AXI_PDMA_OUT_CH1, ETS_AXI_PDMA_OUT_CH1_INTR_SOURCE,
	  ULMK_BOARD_CLIC_IRQ_AXI_PDMA_OUT_CH1 },
	{ ULMK_BOARD_IRQ_AXI_PDMA_IN_CH2, ETS_AXI_PDMA_IN_CH2_INTR_SOURCE,
	  ULMK_BOARD_CLIC_IRQ_AXI_PDMA_IN_CH2 },
	{ ULMK_BOARD_IRQ_AXI_PDMA_OUT_CH2, ETS_AXI_PDMA_OUT_CH2_INTR_SOURCE,
	  ULMK_BOARD_CLIC_IRQ_AXI_PDMA_OUT_CH2 },
};

#define ROUTE_COUNT	(sizeof(g_routes) / sizeof(g_routes[0]))

static void intmtx_set(uint8_t source, uint8_t cpu_irq)
{
	volatile uint32_t *map;

	map = (volatile uint32_t *)(uintptr_t)
		(ULMK_BOARD_INTMTX_BASE + 4u * (uint32_t)source);
	*map = (*map & ~0x3Fu) | ((uint32_t)cpu_irq & 0x3Fu);
}

static void route_apply(uint8_t srpn, bool on)
{
	uint32_t i;

	for (i = 0u; i < ROUTE_COUNT; i++) {
		if (g_routes[i].srpn != srpn)
			continue;
		/* Slot 0 parks the source: no CPU line receives it. */
		intmtx_set(g_routes[i].source, on ? g_routes[i].slot : 0u);
		return;
	}
}

void ulmk_board_irq_connect(uint8_t srpn)
{
	route_apply(srpn, true);
}

void ulmk_board_irq_disconnect(uint8_t srpn)
{
	route_apply(srpn, false);
}

bool ulmk_board_irq_claim(uint32_t irq)
{
	if (irq == ULMK_BOARD_CLIC_IRQ_TICK) {
		ulmk_kern_timer_tick();
		return true;
	}
	/*
	 * Everything else is a driver binding: let the generic CLIC path
	 * dispatch it (bind → notif, or attach → callback) and ack.
	 */
	return false;
}
