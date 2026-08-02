/* SPDX-License-Identifier: MIT */
/*
 * esp32p4_ev_function/board_config.h — ESP32-P4 Function EV Board.
 */
#ifndef ULMK_BOARD_CONFIG_H
#define ULMK_BOARD_CONFIG_H

/*
 * Dual-core SoC.  UP builds still compile with NUM_CPU=2 but only CPU0 is
 * started unless ULMK_CONFIG_ENABLE_SMP=1.
 */
#ifndef ULMK_ARCH_NUM_CPU
#define ULMK_ARCH_NUM_CPU		2
#endif

#ifndef ULMK_ARCH_HAVE_CLINT
#define ULMK_ARCH_HAVE_CLINT		0
#endif

/* Soft IPI via HP_SYSTEM + INTMTX + CLIC (no CLINT MSIP on this SoC). */
#ifndef ULMK_ARCH_HAVE_BOARD_IPI
#define ULMK_ARCH_HAVE_BOARD_IPI	1
#endif

#ifndef ULMK_ARCH_HAVE_BOARD_CPU_START
#define ULMK_ARCH_HAVE_BOARD_CPU_START	1
#endif

#ifndef ULMK_ARCH_HAVE_PLIC
#define ULMK_ARCH_HAVE_PLIC		0
#endif

#ifndef ULMK_ARCH_HAVE_CLIC
#define ULMK_ARCH_HAVE_CLIC		1
#endif

#ifndef ULMK_ARCH_CLIC_VECTORED
#define ULMK_ARCH_CLIC_VECTORED		1
#endif

/* CLIC config @ 0x20800000; per-IRQ ctrl @ +0x1000 (matches arch_config). */
#define ULMK_BOARD_CLIC_BASE		0x20800000u

#ifndef ULMK_ARCH_HAVE_FPU
#define ULMK_ARCH_HAVE_FPU		1
#endif

/*
 * Framebuffers live in cached PSRAM and are consumed by DMA, so drivers need
 * range maintenance.  The sync engine is a non-atomic register sequence, hence
 * it is reached through the ulmk_dcache_* syscalls and runs in M-mode
 * (board_cache.c) rather than from the calling driver thread.
 */
#define ULMK_BOARD_ENABLE_CPU_CACHE	1

#ifndef ULMK_ARCH_PMP_NUM
/* ESP32-P4: 16 PMP entries. */
#define ULMK_ARCH_PMP_NUM		16
#endif

/*
 * The IDF bootloader hands over with slots 0, 1, 2 and 15 locked (ROM and a
 * couple of low RW windows) and the rest open.  Locked entries survive any
 * write, so every arch role has to sit on an unlocked slot or its grant is
 * dropped on the floor without a diagnostic.
 *
 * It also leaves slot 4 as an unlocked RWX TOR window over the whole of
 * internal SRAM.  PMP resolves on the lowest matching entry, so that one
 * outranks anything added higher up; the layout below replaces it outright
 * and pmp_clear_all() steps over the locked entries on its own.
 *
 * User RAM starts right above kernel RAM and is neither power-of-two sized
 * nor aligned, so NAPOT rounds its window out over kernel RAM and the ROM
 * data below it.  TOR (slot 6 carrying the lower bound) draws the line
 * exactly, but this board cannot take it yet: PSRAM/MSPI bring-up and the
 * CPLL switch run in the root thread and call ROM routines that read and
 * write ROM globals, which sit outside any window U-mode is entitled to.
 * Moving that bring-up into ulmk_board_init() is the prerequisite; until
 * then U-mode keeps read/write over the whole of internal SRAM here.
 * MMIO shares the board's HP peripheral slot — no reason to spend a second.
 */
#define ULMK_ARCH_PMP_KERNEL		3
#define ULMK_ARCH_PMP_KRAM		4
#define ULMK_ARCH_PMP_UTEXT		5
#define ULMK_ARCH_PMP_URAM		7
#define ULMK_ARCH_PMP_URAM_TOR		0
#define ULMK_ARCH_PMP_MMIO		13
#define ULMK_ARCH_PMP_USER_BASE		8
#define ULMK_ARCH_PMP_DYNAMIC_BASE	8

/* Slot 15 is locked, so both temp mappings share 14. */
#define ULMK_ARCH_PMP_TEMP0		14
#define ULMK_ARCH_PMP_TEMP1		14

/* Locked by boot (0-2, 15) plus the board extras in board_pmp.c (9-13). */
#define ULMK_ARCH_PMP_RESERVED_MASK	0xBE07u

#ifndef ULMK_ARCH_IDLE_IS_WFI
#define ULMK_ARCH_IDLE_IS_WFI		1
#endif

/* SYSTIMER counts XTAL 40 MHz through a fixed /2.5 divider → 16 MHz. */
#define ULMK_BOARD_TICK_CLOCK_HZ	16000000u
#define ULMK_BOARD_SYSTIMER_BASE	0x500E2000u
#define ULMK_BOARD_FCPU_HZ		400000000u

/*
 * Interrupt matrix (HP): core0 @ base, core1 @ base+0x800
 * (DR_REG_INTERRUPT_CORE1_BASE).
 */
#define ULMK_BOARD_INTMTX_BASE		0x500D6000u
#define ULMK_BOARD_INTMTX_CORE_STRIDE	0x800u
#define ETS_UART0_INTR_SOURCE			31u
#define ETS_SYSTIMER_TARGET0_INTR_SOURCE	53u
#define ETS_FROM_CPU_INTR0_SOURCE		79u
#define ETS_FROM_CPU_INTR1_SOURCE		80u

/* CPU CLIC IRQ slots reserved for board (external; keep < 32). */
#define ULMK_BOARD_CLIC_IRQ_TICK		16u
#define ULMK_BOARD_CLIC_IRQ_I2C0		17u
#define ULMK_BOARD_CLIC_IRQ_DW_GDMA		18u
/* UART0..UART4 take consecutive slots from these bases. */
#define ULMK_BOARD_CLIC_IRQ_UART_BASE		19u
#define ULMK_BOARD_CLIC_IRQ_TWAI0		24u
#define ULMK_BOARD_CLIC_IRQ_PDMA_IN_CH0		25u
#define ULMK_BOARD_CLIC_IRQ_PDMA_IN_CH1		26u
#define ULMK_BOARD_CLIC_IRQ_AXI_PDMA_IN_CH0	27u
#define ULMK_BOARD_CLIC_IRQ_AXI_PDMA_IN_CH1	28u
#define ULMK_BOARD_CLIC_IRQ_AXI_PDMA_OUT_CH1	29u
#define ULMK_BOARD_CLIC_IRQ_AXI_PDMA_IN_CH2	30u
#define ULMK_BOARD_CLIC_IRQ_AXI_PDMA_OUT_CH2	31u
/* Soft IPI — external CLIC lines start at 16; 16..31 are peripherals. */
#define ULMK_BOARD_CLIC_IRQ_IPI			32u
#define ULMK_BOARD_IRQ_TICK			1u
#define ULMK_BOARD_IRQ_I2C0			2u
#define ULMK_BOARD_IRQ_DW_GDMA			3u
#define ULMK_BOARD_IRQ_UART_BASE		4u
#define ULMK_BOARD_IRQ_TWAI0			9u
#define ULMK_BOARD_IRQ_PDMA_IN_CH0		10u
#define ULMK_BOARD_IRQ_PDMA_IN_CH1		11u
#define ULMK_BOARD_IRQ_AXI_PDMA_IN_CH0		12u
#define ULMK_BOARD_IRQ_AXI_PDMA_IN_CH1		13u
#define ULMK_BOARD_IRQ_AXI_PDMA_OUT_CH1		14u
#define ULMK_BOARD_IRQ_AXI_PDMA_IN_CH2		15u
#define ULMK_BOARD_IRQ_AXI_PDMA_OUT_CH2		16u
#define ULMK_BOARD_IRQ_IPI			17u

/* HP_SYSTEM soft-IRQ registers (cross-core). */
#define ULMK_BOARD_HP_SYS_BASE			0x500E5000u
#define ULMK_BOARD_HP_FROM_CPU0_REG \
	(ULMK_BOARD_HP_SYS_BASE + 0x10u)
#define ULMK_BOARD_HP_FROM_CPU1_REG \
	(ULMK_BOARD_HP_SYS_BASE + 0x14u)

#define ETS_I2C0_INTR_SOURCE			44u
#define ETS_DW_GDMA_INTR_SOURCE			24u
#define ETS_TWAI0_INTR_SOURCE			40u
/* Mem-to-mem completion is reported by the receiving channel. */
#define ETS_AHB_PDMA_IN_CH0_INTR_SOURCE		56u
/* Channel 1 carries ADC-digi conversion results. */
#define ETS_AHB_PDMA_IN_CH1_INTR_SOURCE		57u
#define ETS_AXI_PDMA_IN_CH0_INTR_SOURCE		62u
#define ETS_AXI_PDMA_IN_CH1_INTR_SOURCE		63u
#define ETS_AXI_PDMA_IN_CH2_INTR_SOURCE		64u
#define ETS_AXI_PDMA_OUT_CH1_INTR_SOURCE	66u
#define ETS_AXI_PDMA_OUT_CH2_INTR_SOURCE	67u

/* UART0 — USB-Serial/JTAG bridge on Function EV (bootloader @ 115200). */
#define ULMK_BOARD_UART0_BASE		0x500CA000u
#define ULMK_BOARD_UART0_SIZE		0x1000u
#define ULMK_BOARD_USJ_BASE		0x500D2000u
#define BOARD_CONSOLE_UART_BASE		ULMK_BOARD_USJ_BASE
#define BOARD_CONSOLE_UART_MAP_SIZE	0x1000u

#define ULMK_BOARD_PERIPH_BASE		0x50000000u
#define ULMK_BOARD_PERIPH_SIZE		0x200000u

/* PSRAM window (MSPI) — filled after board_psram_init. */
#define ULMK_BOARD_PSRAM_BASE		0x48000000u
#define ULMK_BOARD_PSRAM_SIZE		(8u * 1024u * 1024u)

/* GPIO / LEDC / I2C / ADC / TWAI / GPSPI / AHB-PDMA bases (HP). */
#define ULMK_BOARD_GPIO_BASE		0x500E0000u
#define ULMK_BOARD_IOMUX_BASE		0x500E1000u
#define ULMK_BOARD_LEDC_BASE		0x500D3000u
#define ULMK_BOARD_I2C0_BASE		0x500C4000u
#define ULMK_BOARD_ADC_BASE		0x500DE000u
#define ULMK_BOARD_TWAI0_BASE		0x500D7000u
#define ULMK_BOARD_AHB_PDMA_BASE	0x50085000u
#define ULMK_BOARD_GDMA_BASE		ULMK_BOARD_AHB_PDMA_BASE
#define ULMK_BOARD_AXI_PDMA_BASE	0x5008A000u
#define ULMK_BOARD_SPI2_BASE		0x500D0000u
#define ULMK_BOARD_SPI3_BASE		0x500D1000u

/* MIPI-DSI */
#define ULMK_BOARD_DSI_HOST_BASE	0x500A0000u
#define ULMK_BOARD_DSI_BRG_BASE		0x500A0800u

#define ULMK_BOARD_HIL_SERIAL_BAUD	115200u

#define ULMK_BOARD_GPIO_MAX		1u
#define ULMK_BOARD_PINMUX_MAX		1u
#define ULMK_BOARD_PWM_MAX		1u
#define ULMK_BOARD_ADC_CH_MAX		4u
#define ULMK_BOARD_I2C_MAX		1u
#define ULMK_BOARD_SPI_MAX		2u

/* GPSPI loopback demo pins; soft loopback does not need a jumper. */
#define ULMK_BOARD_SPI2_SCLK_GPIO	2u
#define ULMK_BOARD_SPI2_MOSI_GPIO	3u
#define ULMK_BOARD_SPI2_MISO_GPIO	4u
#define ULMK_BOARD_SPI2_CS_GPIO		5u
#define ULMK_BOARD_SPI3_SCLK_GPIO	9u
#define ULMK_BOARD_SPI3_MOSI_GPIO	10u
#define ULMK_BOARD_SPI3_MISO_GPIO	11u
#define ULMK_BOARD_SPI3_CS_GPIO		12u

/* TWAI0 pins (loopback mode does not need external wiring). */
#define ULMK_BOARD_TWAI_TX_GPIO		16u
#define ULMK_BOARD_TWAI_RX_GPIO		17u

/*
 * Custom HMI jumpers (this kit):
 *   backlight PWM → J1 GPIO26
 *   LCD reset      → J1 GPIO27
 * Stock Function EV uses GPIO23 for BL; we override here.
 * No discrete user LED — blinky reuses the backlight pad.
 */
#define ULMK_BOARD_BL_GPIO		26u
#define ULMK_BOARD_LCD_RST_GPIO		27u
#define ULMK_BOARD_LED1_GPIO		ULMK_BOARD_BL_GPIO
#define ULMK_BOARD_LED_COUNT		1u
#define ULMK_BOARD_DISPLAY_W		1024u
#define ULMK_BOARD_DISPLAY_H		600u
/* Bytes per pixel (RGB565): display ports use it as the stride factor. */
#define ULMK_BOARD_DISPLAY_BPP		2u
#define ULMK_BOARD_DISPLAY_FB_BYTES \
	(ULMK_BOARD_DISPLAY_W * ULMK_BOARD_DISPLAY_H * ULMK_BOARD_DISPLAY_BPP)
#define ULMK_BOARD_DISPLAY_FB_MAP_SIZE \
	(ULMK_BOARD_DISPLAY_FB_BYTES * 2u)

/*
 * LVGL tlsf pool in PSRAM after the dual FBs.
 *   FB = 1024*600*2 = 0x12C000; heap @ 0x48000000 + 2*FB + guard = 0x48268000
 *
 * The guard is not padding for alignment: with the pool butted up against
 * the second FB, anything that writes one row past the framebuffer lands on
 * the allocator's own block headers, and the failure surfaces later as a
 * corrupt pointer in unrelated LVGL code.
 */
#define ULMK_BOARD_LVGL_HEAP_GUARD	0x10000u
#define ULMK_BOARD_LVGL_HEAP_ADDR \
	(ULMK_BOARD_PSRAM_BASE + ULMK_BOARD_DISPLAY_FB_MAP_SIZE + \
	 ULMK_BOARD_LVGL_HEAP_GUARD)
#define ULMK_BOARD_LVGL_HEAP_SIZE	(5u * 1024u * 1024u)

#endif /* ULMK_BOARD_CONFIG_H */
