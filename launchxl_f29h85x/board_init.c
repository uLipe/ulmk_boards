/* SPDX-License-Identifier: MIT */
/*
 * Early board bring-up — clocks, pinmux, UARTA @ 115200.
 * Called from ulmk_kern_start() before .data/.bss are live.
 */

#include <stdint.h>
#include <board_config.h>

#define MMIO32(a)	(*(volatile uint32_t *)(uintptr_t)(a))
#define MMIO16(a)	(*(volatile uint16_t *)(uintptr_t)(a))
#define MMIO8(a)	(*(volatile uint8_t *)(uintptr_t)(a))

#define GPIOCTRL_BASE	ULMK_BOARD_GPIOCTRL_BASE
#define CPUPERCFG_BASE	ULMK_BOARD_CPUPERCFG_BASE
#define UARTA_BASE	ULMK_BOARD_UARTA_BASE

#define SYSCTL_O_PCLKCR0	0x10u
#define SYSCTL_PERIPH_REG_M	0x003Fu
#define SYSCTL_PERIPH_BIT_M	0x1F00u
#define SYSCTL_PERIPH_BIT_S	8u
#define SYSCTL_PERIPH_CLK_UARTA		0x1007u
#define SYSCTL_PERIPH_CLK_CPUTIMER2	0x0600u

#define GPIO_42_UARTA_TX	0x008C1405u
#define GPIO_43_UARTA_RX	0x008C1605u
#define GPIO_MUX_TO_GMUX	0x34u	/* GPAGMUX1 - GPAMUX1 */

#define UART_O_DR	0x00u
#define UART_O_FR	0x18u
#define UART_O_IBRD	0x24u
#define UART_O_FBRD	0x28u
#define UART_O_LCRH	0x2Cu
#define UART_O_CTL	0x30u
#define UART_FR_TXFF	0x20u
#define UART_CTL_UARTEN	0x1u
#define UART_CTL_TXE	0x100u
#define UART_CTL_RXE	0x200u
#define UART_LCRH_WLEN8	0x60u

#define DEVCFG_BASE		0x30180000u
#define SYSCTL_O_CLKSRCCTL1	0x530u
#define SYSCTL_O_SYSPLLCTL1	0x53Cu
#define SYSCTL_O_SYSPLLMULT	0x548u
#define SYSCTL_O_SYSPLLSTS	0x54Cu
#define SYSCTL_O_SYSCLKDIVSEL	0x564u
#define SYSCTL_O_MCDCR		0x584u
#define SYSCTL_O_SYNCBUSY	0x5B0u

#define SYSPLLCTL1_PLLEN	0x1u
#define SYSPLLCTL1_PLLCLKEN	0x2u
#define SYSPLLSTS_LOCKS		0x1u
#define CLKSRCCTL1_OSCSRC_M	0x3u	/* 0 = INTOSC2 */
#define MCDCR_MCLKOFF		0x4u

#define SYNCBUSY_SYSCLKDIVSEL	0x00200000u
#define SYNCBUSY_SYSPLLCTL1	0x00400000u
#define SYNCBUSY_SYSPLLMULT	0x00800000u
#define SYNCBUSY_CLKSRCCTL1	0x04000000u

/* INTOSC2 10 MHz x IMULT(0x28) / (ODIV 1 + 1) = 200 MHz; REFDIV = 0. */
#define SYSPLL_MULT_200MHZ	0x00010028u

#define FRI1_BASE		0x301D0000u
#define FRI_O_FRDCNTL		0x10u
#define FRI_FRDCNTL_RWAIT_M	0xF00u
#define FRI_FRDCNTL_RWAIT_S	8u
#define FRI_RWAIT_200MHZ	3u

/*
 * Boot ROM / RTS normally grant this before C; our startup.S skips RTS, so a
 * headless POR arrives here with LINK2 AP override clear.  Without it, reads
 * of DEVCFG (SYNCBUSY etc.) can fault-as-ones and the PLL busy-wait never
 * returns — UART never comes up.  GEL sessions already set the bit.
 */
static void sync_busy_wait(uint32_t mask)
{
	uint32_t timeout;

	for (timeout = 1000000u; timeout != 0u; timeout--) {
		if ((MMIO32(DEVCFG_BASE + SYSCTL_O_SYNCBUSY) & mask) == 0u)
			return;
	}
}

static void clk_settle(uint32_t loops)
{
	volatile uint32_t i;

	for (i = 0u; i < loops; i++) {
	}
}

/*
 * Raise SYSCLK to the 200 MHz the UART divisor below assumes.  A debugger
 * session gets this from the target GEL script; a headless boot from flash has
 * no GEL, so the kernel must program the PLL itself or every clocked
 * peripheral runs at the 10 MHz reset oscillator rate.
 *
 * Flash read wait states are widened first, while still running at the slow
 * oscillator: growing them is always safe, so no RAM-resident helper is needed
 * (only shrinking them under an already-fast clock would require one).
 *
 * Order mirrors the SDK SysCtl_setClock(): bypass, repoint the oscillator,
 * reprogram the multiplier, wait for lock, then ramp the divider down to /1 so
 * the core voltage regulator is never stepped straight to full frequency.
 * Every write to a clock register must settle via SYNCBUSY or it is dropped.
 */
/*
 * Drop SYSCLK back to INTOSC2 (10 MHz).  Safe while XIP: we only slow the
 * clock.  Needed after a debugger flash session — GEL leaves SYSPLL at
 * 200 MHz and JTAG reset does not restore the oscillator, so a flash
 * image that assumes 10 MHz UART divisors otherwise prints garbage.
 * True POR already starts here; the sequence is idempotent.
 */
static void sysclk_force_intosc2(void)
{
	MMIO16(DEVCFG_BASE + SYSCTL_O_SYSPLLCTL1) &=
		(uint16_t)~SYSPLLCTL1_PLLCLKEN;
	clk_settle(256u);
	sync_busy_wait(SYNCBUSY_SYSPLLCTL1);

	MMIO16(DEVCFG_BASE + SYSCTL_O_SYSPLLCTL1) &=
		(uint16_t)~SYSPLLCTL1_PLLEN;
	clk_settle(128u);
	sync_busy_wait(SYNCBUSY_SYSPLLCTL1);

	MMIO16(DEVCFG_BASE + SYSCTL_O_CLKSRCCTL1) &=
		(uint16_t)~CLKSRCCTL1_OSCSRC_M;
	sync_busy_wait(SYNCBUSY_CLKSRCCTL1);
	clk_settle(128u);

	MMIO16(DEVCFG_BASE + SYSCTL_O_SYSCLKDIVSEL) = 0u;
	sync_busy_wait(SYNCBUSY_SYSCLKDIVSEL);
}

static void sysclk_init_200mhz(void)
{
	uint32_t timeout;

	MMIO32(FRI1_BASE + FRI_O_FRDCNTL) =
		(MMIO32(FRI1_BASE + FRI_O_FRDCNTL) & ~FRI_FRDCNTL_RWAIT_M) |
		(FRI_RWAIT_200MHZ << FRI_FRDCNTL_RWAIT_S);

	/* Bypass / disable PLL and select INTOSC2 before reprogramming. */
	sysclk_force_intosc2();

	MMIO16(DEVCFG_BASE + SYSCTL_O_SYSCLKDIVSEL) = 0u;
	sync_busy_wait(SYNCBUSY_SYSCLKDIVSEL);

	MMIO32(DEVCFG_BASE + SYSCTL_O_SYSPLLMULT) = SYSPLL_MULT_200MHZ;
	sync_busy_wait(SYNCBUSY_SYSPLLMULT);

	MMIO16(DEVCFG_BASE + SYSCTL_O_SYSPLLCTL1) |= SYSPLLCTL1_PLLEN;
	sync_busy_wait(SYNCBUSY_SYSPLLCTL1);

	for (timeout = 400000u; timeout != 0u; timeout--) {
		if ((MMIO16(DEVCFG_BASE + SYSCTL_O_SYSPLLSTS) &
		     SYSPLLSTS_LOCKS) != 0u) {
			break;
		}
	}

	/* No lock: stay on the oscillator rather than gate in a dead clock. */
	if (timeout == 0u) {
		return;
	}

	MMIO16(DEVCFG_BASE + SYSCTL_O_SYSCLKDIVSEL) = 4u;
	sync_busy_wait(SYNCBUSY_SYSCLKDIVSEL);

	MMIO16(DEVCFG_BASE + SYSCTL_O_SYSPLLCTL1) |= SYSPLLCTL1_PLLCLKEN;
	/* Errata: MCD must be off once the PLL feeds PLLSYSCLK. */
	MMIO16(DEVCFG_BASE + SYSCTL_O_MCDCR) |= MCDCR_MCLKOFF;
	clk_settle(512u);
	sync_busy_wait(SYNCBUSY_SYSPLLCTL1);

	MMIO16(DEVCFG_BASE + SYSCTL_O_SYSCLKDIVSEL) = 0u;
	sync_busy_wait(SYNCBUSY_SYSCLKDIVSEL);
}

static void periph_clk_enable(uint16_t peripheral)
{
	uint16_t reg_index = (uint16_t)(4u * (peripheral & SYSCTL_PERIPH_REG_M));
	uint16_t bit_index = (uint16_t)((peripheral & SYSCTL_PERIPH_BIT_M) >>
					SYSCTL_PERIPH_BIT_S);

	MMIO32(CPUPERCFG_BASE + SYSCTL_O_PCLKCR0 + reg_index) |=
		(1uL << bit_index);
}

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

static void uarta_init_115200(uint32_t sysclk_hz)
{
	/*
	 * BRD = sysclk / (16 * 115200); IBRD = floor(BRD);
	 * FBRD = floor((BRD - IBRD) * 64 + 0.5).
	 * 200 MHz → 108 / 32; 10 MHz INTOSC2 → 5 / 27.
	 */
	uint32_t ibrd = 108u;
	uint32_t fbrd = 32u;

	if (sysclk_hz <= 10000000u) {
		ibrd = 5u;
		fbrd = 27u;
	}

	MMIO32(UARTA_BASE + UART_O_CTL) = 0u;
	MMIO16(UARTA_BASE + UART_O_IBRD) = (uint16_t)ibrd;
	MMIO8(UARTA_BASE + UART_O_FBRD) = (uint8_t)fbrd;
	MMIO32(UARTA_BASE + UART_O_LCRH) = UART_LCRH_WLEN8;
	MMIO32(UARTA_BASE + UART_O_CTL) =
		UART_CTL_UARTEN | UART_CTL_TXE | UART_CTL_RXE;
}

void ulmk_board_init(void)
{
	uint32_t uart_hz = 200000000u;

	/* Watchdog also cleared in startup.S; repeat for GEL-bypass boots. */
	MMIO8(ULMK_BOARD_WD_DISABLE_ADDR) = ULMK_BOARD_WD_DISABLE_VAL;

#if defined(ULMK_C29_FLASH) && ULMK_C29_FLASH
	/*
	 * LINK2 AP override is set in startup.S (same as RTS _c_int00).
	 * Stay on INTOSC2 until .TI.ramfunc Flash_initModule exists; raising
	 * SYSPLL while XIP hangs the fetch pipeline after headless POR.
	 * Force INTOSC2 in case a prior GEL flash left SYSPLL at 200 MHz.
	 */
	sysclk_force_intosc2();
	uart_hz = 10000000u;
#else
	/* SPRZ569E: clear all DLB enables including sync-bridge. */
	MMIO32(ULMK_BOARD_MEMSSMISCI_BASE) &= ~0x47u;
	sysclk_init_200mhz();
#endif

	periph_clk_enable(SYSCTL_PERIPH_CLK_UARTA);
	periph_clk_enable(SYSCTL_PERIPH_CLK_CPUTIMER2);

	gpio_set_pin_config(GPIO_42_UARTA_TX);
	gpio_set_pin_config(GPIO_43_UARTA_RX);
	uarta_init_115200(uart_hz);
}
