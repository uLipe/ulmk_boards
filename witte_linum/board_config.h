/* SPDX-License-Identifier: MIT */
/*
 * witte_linum/board_config.h — Witte Technology Linum (STM32H753ZI).
 *
 * DTS reference: dts/zephyr.dts (Zephyr linum_dev, not parsed by the build).
 *   HSE 25 MHz → SYSCLK 480 MHz; console USART1 @ PB14/PB15 @ 921600;
 *   status LED green = GPIOG2 (active-low).
 */

#ifndef ULMK_BOARD_CONFIG_H
#define ULMK_BOARD_CONFIG_H

#ifndef ULMK_ARCH_NUM_CPU
#define ULMK_ARCH_NUM_CPU		1
#endif

/* ARMv7-M (PMSAv7 MPU).  H753 exposes 16 regions — need headroom for
 * root-thread PERIPH maps (timer/GPIO) plus ULMK_MMAP_SHARED (SDRAM). */
#define ULMK_ARCH_ARMV8M		0
#ifndef ULMK_ARCH_MPU_REGIONS
#define ULMK_ARCH_MPU_REGIONS		16
#endif

/* FPU presence follows ULMK_CONFIG_FPU; see cmake/arm_fpu.cmake. */

/*
 * Enable Cortex-M7 I+D cache after MPU bring-up (arch_init).  SHARED SDRAM
 * is Normal WB — clean FB before LTDC present; DMA buffers need clean/inv.
 */
#define ULMK_BOARD_ENABLE_CPU_CACHE	1

#ifndef ULMK_ARCH_IDLE_IS_WFI
#define ULMK_ARCH_IDLE_IS_WFI		1
#endif

/* ── Clocks (25 MHz HSE → 480 MHz PLL1P in board_init) ───────────────── */

#define ULMK_BOARD_FOSC_HZ		25000000u
#define ULMK_BOARD_FCPU_HZ		480000000u
#define BOARD_CPU_HZ			ULMK_BOARD_FCPU_HZ
/* AHB after HPRE=/2; APB2 after D2PPRE2=/2 → USART1 kernel clock. */
#define ULMK_BOARD_FAHB_HZ		240000000u
#define ULMK_BOARD_FAPB1_HZ		120000000u
#define ULMK_BOARD_FAPB2_HZ		120000000u
#define ULMK_BOARD_FUART_HZ		ULMK_BOARD_FAPB2_HZ
/* TIM2 free-run @ 2×PCLK1 = 240 MHz (APB1 prescaler /2). */
#define ULMK_BOARD_FSTM_HZ		240000000u

/* ── IRQ / NVIC (CMSIS IRQn values) ──────────────────────────────────── */

#define ULMK_BOARD_IRQ_USART1		10u	/* kernel SRPN slot */
#define ULMK_BOARD_NVIC_USART1		37u	/* USART1_IRQn */
/* Same encoding as arch/arm ULMK_ARCH_NVIC_SRC() — usable from userspace. */
#define ULMK_BOARD_NVIC_SRC(irq) \
	(0x8000u | ((uint32_t)(irq) & 0x7FFFu))

/* ── Driver instance limits ──────────────────────────────────────────── */

#define ULMK_BOARD_PINMUX_MAX		1u
#define ULMK_BOARD_GPIO_MAX		1u
#define ULMK_BOARD_UART_MAX		1u

/* ── USART1 console (PB14 TX / PB15 RX, AF4) ─────────────────────────── */

#define ULMK_BOARD_USART1_BASE		0x40011000u
#define ULMK_BOARD_USART1_MAP_SIZE	0x400u
#define BOARD_CONSOLE_UART_BASE		ULMK_BOARD_USART1_BASE
#define BOARD_CONSOLE_UART_MAP_SIZE	ULMK_BOARD_USART1_MAP_SIZE
#define ULMK_BOARD_CONSOLE_BAUD		921600u
#define ULMK_BOARD_USART1_TX_PORT	1u	/* GPIOB */
#define ULMK_BOARD_USART1_TX_PIN	14u
#define ULMK_BOARD_USART1_RX_PORT	1u
#define ULMK_BOARD_USART1_RX_PIN	15u
#define ULMK_BOARD_USART1_AF		4u

/* ── Status LEDs (active-low) ────────────────────────────────────────── */

#define ULMK_BOARD_LED1_PORT		6u	/* GPIOG — green / led0 */
#define ULMK_BOARD_LED1_PIN		2u
#define ULMK_BOARD_LED2_PORT		1u	/* GPIOB — red / led1 */
#define ULMK_BOARD_LED2_PIN		2u
#define ULMK_BOARD_LED3_PORT		6u	/* GPIOG — blue / led2 */
#define ULMK_BOARD_LED3_PIN		3u

/* ── GPIO / RCC bases (AHB4) ─────────────────────────────────────────── */

#define ULMK_BOARD_GPIOA_BASE		0x58020000u
#define ULMK_BOARD_GPIO_STRIDE		0x400u
#define ULMK_BOARD_RCC_BASE		0x58024400u
#define ULMK_BOARD_RCC_MAP_SIZE		0x400u

/* Generic MMIO window for ULMK_MMAP_PERIPH tests. */
#define ULMK_BOARD_PERIPH_BASE		ULMK_BOARD_USART1_BASE
#define ULMK_BOARD_PERIPH_SIZE		ULMK_BOARD_USART1_MAP_SIZE

/*
 * HIL programmable timer (TIM2) — silicon_irq_stress / silicon_wcet.
 * Userspace mmaps TIM2 and arms update IRQs via EGR; NVIC vector TIM2_IRQn.
 * SRPN slot must differ from the kernel SysTick path (not a BIV SRPN on ARM —
 * ulmk_irq_bind_hw uses ULMK_BOARD_NVIC_SRC encoding).
 */
#define ULMK_BOARD_HIL_TIMER_BASE	0x40000000u	/* TIM2 */
#define ULMK_BOARD_HIL_TIMER_SIZE	0x400u
#define ULMK_BOARD_IRQ_HIL_TIMER	10u		/* kernel IRQ slot */
#define ULMK_BOARD_NVIC_TIM2		28u		/* TIM2_IRQn */
#define ULMK_BOARD_SRC_HIL_TIMER \
	ULMK_BOARD_NVIC_SRC(ULMK_BOARD_NVIC_TIM2)

/* TIM2 free-run clock (APB1=/2 → timer clk = 2×PCLK1 = 240 MHz). */
#ifndef ULMK_BOARD_FSTM_HZ
#define ULMK_BOARD_FSTM_HZ		240000000u
#endif

/* ── External SDRAM (FMC bank1 @ 0xC0000000, 8 MiB) ─────────────────── */

#define ULMK_BOARD_SDRAM_BASE		0xC0000000u
#define ULMK_BOARD_SDRAM_SIZE		(8u * 1024u * 1024u)

/* ── PWM ─────────────────────────────────────────────────────────────── */

#define ULMK_BOARD_PWM_MAX		2u
#define ULMK_BOARD_PWM_BACKLIGHT	0u	/* TIM12 CH1 PH6 */
#define ULMK_BOARD_PWM_BUZZER		1u	/* TIM4 CH2 PB7 */
#define ULMK_BOARD_PWM_TIM_CLK_HZ	240000000u

/* ── FDCAN ───────────────────────────────────────────────────────────── */

#define ULMK_BOARD_CAN_MAX		2u
#define ULMK_BOARD_FDCAN1_BASE		0x4000A000u
#define ULMK_BOARD_FDCAN2_BASE		0x4000A400u
#define ULMK_BOARD_FDCAN_MAP_SIZE	0x400u
#define ULMK_BOARD_CAN_STD1_PORT	8u	/* GPIOI */
#define ULMK_BOARD_CAN_STD1_PIN		2u
#define ULMK_BOARD_CAN_STD2_PORT	4u	/* GPIOE */
#define ULMK_BOARD_CAN_STD2_PIN		3u
#define ULMK_BOARD_NVIC_FDCAN1_IT0	19u
#define ULMK_BOARD_NVIC_FDCAN2_IT0	20u
#define ULMK_BOARD_IRQ_FDCAN1		12u
#define ULMK_BOARD_IRQ_FDCAN2		13u

/* ── ADC expansion connector (10 channels) ───────────────────────────── */

#define ULMK_BOARD_ADC_MAX		1u
#define ULMK_BOARD_ADC_CH_MAX		10u
#define ULMK_BOARD_ADC1_BASE		0x40022000u
#define ULMK_BOARD_ADC_MAP_SIZE		0x400u
#define ULMK_BOARD_NVIC_ADC		18u	/* ADC_IRQn */
#define ULMK_BOARD_IRQ_ADC		14u

/* ── DMA1 + DMAMUX (owned by drivers/dma; slots 0..2) ───────────────── */
#define ULMK_BOARD_DMA1_BASE		0x40020000u
#define ULMK_BOARD_DMA1_MAP_SIZE	0x800u
#define ULMK_BOARD_DMA1_FULL_MAP_SIZE	0xC00u	/* DMA1 + DMAMUX */
#define ULMK_BOARD_DMA_MAX_SLOTS	3u
#define ULMK_BOARD_DMA_SLOT_ADC		0u
#define ULMK_BOARD_DMA_SLOT_I2C_TX	1u
#define ULMK_BOARD_DMA_SLOT_I2C_RX	2u
#define ULMK_BOARD_DMAMUX_ADC1		9u
#define ULMK_BOARD_DMAMUX_I2C3_RX	73u
#define ULMK_BOARD_DMAMUX_I2C3_TX	74u
#define ULMK_BOARD_NVIC_DMA1_STR0	11u
#define ULMK_BOARD_NVIC_DMA1_STR1	12u
#define ULMK_BOARD_NVIC_DMA1_STR2	13u
#define ULMK_BOARD_IRQ_DMA_STR0		15u
#define ULMK_BOARD_IRQ_DMA_STR1		1u
#define ULMK_BOARD_IRQ_DMA_STR2		2u
#define ULMK_BOARD_IRQ_DMA_ADC		ULMK_BOARD_IRQ_DMA_STR0
#define ULMK_BOARD_IRQ_DMA_I2C_TX	ULMK_BOARD_IRQ_DMA_STR1
#define ULMK_BOARD_IRQ_DMA_I2C_RX	ULMK_BOARD_IRQ_DMA_STR2

/* ADC1 channel and pad maps for the expansion connector. */
#define ULMK_BOARD_ADC_CH0		15u	/* PA3 */
#define ULMK_BOARD_ADC_CH1		18u	/* PA4 */
#define ULMK_BOARD_ADC_CH2		3u	/* PA6 */
#define ULMK_BOARD_ADC_CH3		9u	/* PB0 */
#define ULMK_BOARD_ADC_CH4		5u	/* PB1 */
#define ULMK_BOARD_ADC_CH5		16u	/* PI0 */
#define ULMK_BOARD_ADC_CH6		19u	/* PI5 */
#define ULMK_BOARD_ADC_CH7		13u	/* PH2 */
#define ULMK_BOARD_ADC_CH8		14u	/* PH3 */
#define ULMK_BOARD_ADC_CH9		17u	/* PI6 */

/* ── LTDC display ────────────────────────────────────────────────────── */

#define ULMK_BOARD_LTDC_BASE		0x50001000u
#define ULMK_BOARD_LTDC_MAP_SIZE	0x200u
#define ULMK_BOARD_DISPLAY_W		1024u
#define ULMK_BOARD_DISPLAY_H		600u
#define ULMK_BOARD_DISPLAY_BPP		2u
#define ULMK_BOARD_DISPLAY_FB_BYTES \
	(ULMK_BOARD_DISPLAY_W * ULMK_BOARD_DISPLAY_H * ULMK_BOARD_DISPLAY_BPP)
/* Cover both RGB565 framebuffers; size must fit PMSAv7 power-of-two round-up. */
#define ULMK_BOARD_DISPLAY_FB_MAP_SIZE	ULMK_BOARD_SDRAM_SIZE
/*
 * LVGL tlsf pool in SDRAM after the dual LTDC FBs (identity-mapped).
 * 8 MiB − 2×FB ≈ 5.6 MiB; use 5 MiB for widgets / opa_layer / scrolling.
 */
#define ULMK_BOARD_LVGL_HEAP_OFF	(2u * ULMK_BOARD_DISPLAY_FB_BYTES)
#define ULMK_BOARD_LVGL_HEAP_SIZE	(5u * 1024u * 1024u)
#define ULMK_BOARD_LVGL_HEAP_ADDR \
	(ULMK_BOARD_SDRAM_BASE + ULMK_BOARD_LVGL_HEAP_OFF)
#define ULMK_BOARD_NVIC_LTDC		88u
#define ULMK_BOARD_IRQ_LTDC		16u
#define ULMK_BOARD_DISP_ON_PORT		8u	/* GPIOI */
#define ULMK_BOARD_DISP_ON_PIN		7u

/* ── I2C3 (touch FT5446 @ 0x38, PH7 SCL / PH8 SDA AF4) ──────────────── */

#define ULMK_BOARD_I2C_MAX		1u
#define ULMK_BOARD_I2C3_BASE		0x40005C00u
#define ULMK_BOARD_I2C_MAP_SIZE		0x400u
#define ULMK_BOARD_I2C_BITRATE_HZ	400000u
#define ULMK_BOARD_I2C_SCL_PORT		7u	/* GPIOH */
#define ULMK_BOARD_I2C_SCL_PIN		7u
#define ULMK_BOARD_I2C_SDA_PORT		7u
#define ULMK_BOARD_I2C_SDA_PIN		8u
#define ULMK_BOARD_I2C_AF		4u
#define ULMK_BOARD_NVIC_I2C3_EV		72u
#define ULMK_BOARD_IRQ_I2C3_EV		3u
/* FT5446 INT PH9 active-low → EXTI9 */
#define ULMK_BOARD_TOUCH_INT_PORT	7u
#define ULMK_BOARD_TOUCH_INT_PIN	9u
#define ULMK_BOARD_TOUCH_ADDR7		0x38u
#define ULMK_BOARD_NVIC_EXTI9_5		23u
#define ULMK_BOARD_IRQ_TOUCH_EXTI	4u
#define ULMK_BOARD_EXTI_BASE		0x58000000u
#define ULMK_BOARD_SYSCFG_BASE		0x58000400u
#define ULMK_BOARD_EXTI_MAP_SIZE	0x400u

/* ── QUADSPI NOR W25Q128 (PF6–10 / PG6 AF9) ─────────────────────────── */

#define ULMK_BOARD_QSPI_MAX		1u
#define ULMK_BOARD_QSPI_BASE		0x52005000u
#define ULMK_BOARD_QSPI_MAP_SIZE	0x1000u
#define ULMK_BOARD_QSPI_FLASH_BASE	0x90000000u
#define ULMK_BOARD_QSPI_FLASH_SIZE	(16u * 1024u * 1024u)
#define ULMK_BOARD_NVIC_QSPI		92u
#define ULMK_BOARD_IRQ_QSPI		5u
	/* AHB 240 MHz / 16 = 15 MHz — conservative for bring-up */
#define ULMK_BOARD_QSPI_PRESCALER	15u

#endif /* ULMK_BOARD_CONFIG_H */
