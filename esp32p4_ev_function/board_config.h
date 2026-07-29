/* SPDX-License-Identifier: MIT */
/*
 * esp32p4_ev_function/board_config.h — ESP32-P4 Function EV Board.
 */
#ifndef ULMK_BOARD_CONFIG_H
#define ULMK_BOARD_CONFIG_H

#ifndef ULMK_ARCH_NUM_CPU
#define ULMK_ARCH_NUM_CPU		1
#endif

#ifndef ULMK_ARCH_HAVE_CLINT
#define ULMK_ARCH_HAVE_CLINT		0
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
/* ESP32-P4: 16 PMP entries. Boot firmware locks early slots — preserve them. */
#define ULMK_ARCH_PMP_NUM		16
#endif

/* Do not wipe bootloader PMP (locked); overlay free high slots only. */
#ifndef ULMK_ARCH_PMP_PRESERVE_BOOT
#define ULMK_ARCH_PMP_PRESERVE_BOOT	1
#endif

#ifndef ULMK_ARCH_IDLE_IS_WFI
#define ULMK_ARCH_IDLE_IS_WFI		1
#endif

/* SYSTIMER counts XTAL 40 MHz through a fixed /2.5 divider → 16 MHz. */
#define ULMK_BOARD_TICK_CLOCK_HZ	16000000u
#define ULMK_BOARD_SYSTIMER_BASE	0x500E2000u
#define ULMK_BOARD_FCPU_HZ		360000000u

/* Interrupt matrix (HP) — soc/interrupts.h (alias ETS_LP_TSENS does not bump). */
#define ULMK_BOARD_INTMTX_BASE		0x500D6000u
#define ETS_UART0_INTR_SOURCE			31u
#define ETS_SYSTIMER_TARGET0_INTR_SOURCE	53u

/* CPU CLIC IRQ slots reserved for board (external; keep < 32). */
#define ULMK_BOARD_CLIC_IRQ_TICK		16u
#define ULMK_BOARD_CLIC_IRQ_I2C0		17u
#define ULMK_BOARD_CLIC_IRQ_DW_GDMA		18u
/* UART0..UART4 take consecutive slots from these bases. */
#define ULMK_BOARD_CLIC_IRQ_UART_BASE		19u
#define ULMK_BOARD_CLIC_IRQ_TWAI0		24u
#define ULMK_BOARD_CLIC_IRQ_PDMA_IN_CH0		25u
#define ULMK_BOARD_CLIC_IRQ_PDMA_IN_CH1		26u
#define ULMK_BOARD_IRQ_TICK			1u
#define ULMK_BOARD_IRQ_I2C0			2u
#define ULMK_BOARD_IRQ_DW_GDMA			3u
#define ULMK_BOARD_IRQ_UART_BASE		4u
#define ULMK_BOARD_IRQ_TWAI0			9u
#define ULMK_BOARD_IRQ_PDMA_IN_CH0		10u
#define ULMK_BOARD_IRQ_PDMA_IN_CH1		11u

#define ETS_I2C0_INTR_SOURCE			44u
#define ETS_DW_GDMA_INTR_SOURCE			24u
#define ETS_TWAI0_INTR_SOURCE			40u
/* Mem-to-mem completion is reported by the receiving channel. */
#define ETS_AHB_PDMA_IN_CH0_INTR_SOURCE		56u
/* Channel 1 carries ADC-digi conversion results. */
#define ETS_AHB_PDMA_IN_CH1_INTR_SOURCE		57u

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
#define ULMK_BOARD_SPI_MAX		1u

/* GPSPI2 loopback demo pins (MOSI↔MISO jumper or SPI ctrl loop). */
#define ULMK_BOARD_SPI2_SCLK_GPIO	2u
#define ULMK_BOARD_SPI2_MOSI_GPIO	3u
#define ULMK_BOARD_SPI2_MISO_GPIO	4u
#define ULMK_BOARD_SPI2_CS_GPIO		5u

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
#define ULMK_BOARD_DISPLAY_BPP		16u
#define ULMK_BOARD_DISPLAY_FB_BYTES \
	(ULMK_BOARD_DISPLAY_W * ULMK_BOARD_DISPLAY_H * 2u)
#define ULMK_BOARD_DISPLAY_FB_MAP_SIZE \
	(ULMK_BOARD_DISPLAY_FB_BYTES * 2u)


#endif /* ULMK_BOARD_CONFIG_H */
