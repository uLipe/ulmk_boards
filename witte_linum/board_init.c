/* SPDX-License-Identifier: MIT */
/*
 * board_init.c — STM32H753 clock / early console / FMC SDRAM (kernel, pre-.data).
 *
 * Constraints: no .data/.bss globals, no ulmk_* API, must return.
 * Uses STM32 LL header inlines only (no HAL).  FMC SDRAM programmed via
 * CMSIS register defs (ll_fmc.h pulls HAL).
 *
 * Clock tree from dts/zephyr.dts:
 *   HSE 25 MHz, PLL1 M=5 N=192 P=2 → SYSCLK 480 MHz
 *   HPRE=/2 → AHB 240 MHz; APB2=/2 → USART1 120 MHz
 *
 * SDRAM (Zephyr linum_dev bank@0): NC_8 NR_12 MWID_16 NB_4 CAS_2
 * SDCLK_PERIOD_3 RBURST RPIPE_0; timing <2 6 4 6 2 2 2>; MR 0x220;
 * refresh 1562; power-up 100 us; 8 auto-refresh.
 */

#include "board_ll.h"
#include "board_config.h"

/* FMC SDRAM bank0 — NuttX linum-stm32h753bi proven values (IS42S16400). */
#define SDRAM_SDCR0							\
	(0u | /* NC_8 */						\
	 (1u << FMC_SDCRx_NR_Pos) | /* NR_12 */				\
	 (1u << FMC_SDCRx_MWID_Pos) | /* MWID_16 */			\
	 FMC_SDCRx_NB | /* NB_4 */					\
	 (3u << FMC_SDCRx_CAS_Pos) | /* CAS_3 */			\
	 (2u << FMC_SDCRx_SDCLK_Pos) | /* SDCLK = HCLK/2 */		\
	 FMC_SDCRx_RBURST) /* RPIPE_0 */

/* Timing <2 6 4 6 2 2 2> — register fields store (n - 1). */
#define SDRAM_SDTR0							\
	(((2u - 1u) << FMC_SDTRx_TMRD_Pos) |				\
	 ((6u - 1u) << FMC_SDTRx_TXSR_Pos) |				\
	 ((4u - 1u) << FMC_SDTRx_TRAS_Pos) |				\
	 ((6u - 1u) << FMC_SDTRx_TRC_Pos) |				\
	 ((2u - 1u) << FMC_SDTRx_TWR_Pos) |				\
	 ((2u - 1u) << FMC_SDTRx_TRP_Pos) |				\
	 ((2u - 1u) << FMC_SDTRx_TRCD_Pos))

/* Burst len 4 | seq | CAS3 | standard | write single */
#define SDRAM_MODE_REGISTER		0x230u
/* COUNT ≈ (SDCLK_Hz * 64e-3 / 4096) - 20; SDCLK=HCLK/2=120 MHz → 1855 */
#define SDRAM_REFRESH_COUNT		1855u
#define SDRAM_AUTO_REFRESH_N		8u
#define SDRAM_CMD_CLK_ENABLE		0x1u
#define SDRAM_CMD_PALL			0x2u
#define SDRAM_CMD_AUTOREFRESH		0x3u
#define SDRAM_CMD_LOAD_MODE		0x4u

static void spin_wait(volatile uint32_t n)
{
	while (n-- > 0u)
		;
}

static void clock_init_480mhz(void)
{
	volatile uint32_t guard;

	LL_APB4_GRP1_EnableClock(LL_APB4_GRP1_PERIPH_SYSCFG);
	spin_wait(16u);

	LL_PWR_ConfigSupply(LL_PWR_LDO_SUPPLY);
	guard = 1000000u;
	while (!LL_PWR_IsActiveFlag_ACTVOS() && guard-- > 0u)
		;

	/* VOS1 then VOS0 (ODEN) for 480 MHz — RM0433. */
	LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
	guard = 1000000u;
	while (!LL_PWR_IsActiveFlag_VOS() && guard-- > 0u)
		;

	SET_BIT(SYSCFG->PWRCR, SYSCFG_PWRCR_ODEN);
	spin_wait(1000u);
	LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE0);
	guard = 1000000u;
	while (!LL_PWR_IsActiveFlag_VOS() && guard-- > 0u)
		;

	LL_FLASH_SetLatency(LL_FLASH_LATENCY_4);
	guard = 10000u;
	while (LL_FLASH_GetLatency() != LL_FLASH_LATENCY_4 && guard-- > 0u)
		;

	LL_RCC_HSE_Enable();
	guard = 1000000u;
	while (!LL_RCC_HSE_IsReady() && guard-- > 0u)
		;

	LL_RCC_PLL_SetSource(LL_RCC_PLLSOURCE_HSE);
	LL_RCC_PLL1_SetVCOInputRange(LL_RCC_PLLINPUTRANGE_4_8);
	LL_RCC_PLL1_SetVCOOutputRange(LL_RCC_PLLVCORANGE_WIDE);
	LL_RCC_PLL1_SetM(5u);
	LL_RCC_PLL1_SetN(192u);
	LL_RCC_PLL1_SetP(2u);
	LL_RCC_PLL1_SetQ(4u);
	LL_RCC_PLL1_SetR(4u);
	LL_RCC_PLL1P_Enable();
	LL_RCC_PLL1Q_Enable();
	LL_RCC_PLL1_Enable();
	guard = 1000000u;
	while (!LL_RCC_PLL1_IsReady() && guard-- > 0u)
		;

	LL_RCC_SetSysPrescaler(LL_RCC_SYSCLK_DIV_1);
	LL_RCC_SetAHBPrescaler(LL_RCC_AHB_DIV_2);
	LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_2);
	LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_2);
	LL_RCC_SetAPB3Prescaler(LL_RCC_APB3_DIV_2);
	LL_RCC_SetAPB4Prescaler(LL_RCC_APB4_DIV_2);

	LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL1);
	guard = 1000000u;
	while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL1 &&
	       guard-- > 0u)
		;
	/* SystemCoreClock lives in .data — updated after relocation. */
}

static void periph_clocks(void)
{
	LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOB);
	LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOC);
	LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOD);
	LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOE);
	LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOF);
	LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOG);
	LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOH);
	LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOI);
	LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOJ);
	LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOK);
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_ADC12);
	LL_AHB3_GRP1_EnableClock(LL_AHB3_GRP1_PERIPH_FMC);
	LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM4);
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM12);
	LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_FDCAN);
	LL_RCC_SetFDCANClockSource(LL_RCC_FDCAN_CLKSOURCE_PLL1Q);
	/* TIM2 for silicon_irq_stress / silicon_wcet HIL (userspace MMIO). */
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);
	LL_APB3_GRP1_EnableClock(LL_APB3_GRP1_PERIPH_LTDC);
	spin_wait(16u);

	/*
	 * H753ZI: close PC2/PC3 switches (connect die to PC2_C/PC3_C balls)
	 * and enable I/O analog switch booster.
	 */
	CLEAR_BIT(SYSCFG->PMCR, SYSCFG_PMCR_PC2SO | SYSCFG_PMCR_PC3SO);
	SET_BIT(SYSCFG->PMCR, SYSCFG_PMCR_BOOSTEN);
}

static void early_usart1(void)
{
	/* PB14/PB15 AF4 USART1 */
	LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_14, LL_GPIO_MODE_ALTERNATE);
	LL_GPIO_SetPinSpeed(GPIOB, LL_GPIO_PIN_14, LL_GPIO_SPEED_FREQ_HIGH);
	LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_14, LL_GPIO_PULL_NO);
	LL_GPIO_SetAFPin_8_15(GPIOB, LL_GPIO_PIN_14, LL_GPIO_AF_4);

	LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_15, LL_GPIO_MODE_ALTERNATE);
	LL_GPIO_SetPinSpeed(GPIOB, LL_GPIO_PIN_15, LL_GPIO_SPEED_FREQ_HIGH);
	LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_15, LL_GPIO_PULL_UP);
	LL_GPIO_SetAFPin_8_15(GPIOB, LL_GPIO_PIN_15, LL_GPIO_AF_4);

	LL_USART_Disable(USART1);
	LL_USART_SetTransferDirection(USART1, LL_USART_DIRECTION_TX_RX);
	LL_USART_ConfigCharacter(USART1, LL_USART_DATAWIDTH_8B,
				 LL_USART_PARITY_NONE, LL_USART_STOPBITS_1);
	LL_USART_SetBaudRate(USART1, ULMK_BOARD_FUART_HZ,
			     LL_USART_PRESCALER_DIV1, LL_USART_OVERSAMPLING_16,
			     ULMK_BOARD_CONSOLE_BAUD);
	LL_USART_Enable(USART1);
}

static void early_leds(void)
{
	/* Active-low LEDs off (drive high). */
	LL_GPIO_SetPinMode(GPIOG, LL_GPIO_PIN_2, LL_GPIO_MODE_OUTPUT);
	LL_GPIO_SetPinOutputType(GPIOG, LL_GPIO_PIN_2, LL_GPIO_OUTPUT_PUSHPULL);
	LL_GPIO_SetPinPull(GPIOG, LL_GPIO_PIN_2, LL_GPIO_PULL_NO);
	LL_GPIO_SetOutputPin(GPIOG, LL_GPIO_PIN_2);

	LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_2, LL_GPIO_MODE_OUTPUT);
	LL_GPIO_SetPinOutputType(GPIOB, LL_GPIO_PIN_2, LL_GPIO_OUTPUT_PUSHPULL);
	LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_2);

	LL_GPIO_SetPinMode(GPIOG, LL_GPIO_PIN_3, LL_GPIO_MODE_OUTPUT);
	LL_GPIO_SetPinOutputType(GPIOG, LL_GPIO_PIN_3, LL_GPIO_OUTPUT_PUSHPULL);
	LL_GPIO_SetOutputPin(GPIOG, LL_GPIO_PIN_3);
}

static void fmc_gpio_af12(GPIO_TypeDef *port, uint32_t pin)
{
	LL_GPIO_SetPinMode(port, pin, LL_GPIO_MODE_ALTERNATE);
	LL_GPIO_SetPinSpeed(port, pin, LL_GPIO_SPEED_FREQ_VERY_HIGH);
	LL_GPIO_SetPinOutputType(port, pin, LL_GPIO_OUTPUT_PUSHPULL);
	LL_GPIO_SetPinPull(port, pin, LL_GPIO_PULL_NO);
	if (pin >= LL_GPIO_PIN_8)
		LL_GPIO_SetAFPin_8_15(port, pin, LL_GPIO_AF_12);
	else
		LL_GPIO_SetAFPin_0_7(port, pin, LL_GPIO_AF_12);
}

static void fmc_sdram_pinmux(void)
{
	/* Address A0-A5 = PF0-5, A6-A9 = PF12-15, A10-A11 = PG0-1 */
	fmc_gpio_af12(GPIOF, LL_GPIO_PIN_0);
	fmc_gpio_af12(GPIOF, LL_GPIO_PIN_1);
	fmc_gpio_af12(GPIOF, LL_GPIO_PIN_2);
	fmc_gpio_af12(GPIOF, LL_GPIO_PIN_3);
	fmc_gpio_af12(GPIOF, LL_GPIO_PIN_4);
	fmc_gpio_af12(GPIOF, LL_GPIO_PIN_5);
	fmc_gpio_af12(GPIOF, LL_GPIO_PIN_12);
	fmc_gpio_af12(GPIOF, LL_GPIO_PIN_13);
	fmc_gpio_af12(GPIOF, LL_GPIO_PIN_14);
	fmc_gpio_af12(GPIOF, LL_GPIO_PIN_15);
	fmc_gpio_af12(GPIOG, LL_GPIO_PIN_0);
	fmc_gpio_af12(GPIOG, LL_GPIO_PIN_1);

	/* BA0-BA1 = PG4-5 */
	fmc_gpio_af12(GPIOG, LL_GPIO_PIN_4);
	fmc_gpio_af12(GPIOG, LL_GPIO_PIN_5);

	/* Data D0-D1 PD14-15, D2-D3 PD0-1, D4-D12 PE7-15, D13-D15 PD8-10 */
	fmc_gpio_af12(GPIOD, LL_GPIO_PIN_14);
	fmc_gpio_af12(GPIOD, LL_GPIO_PIN_15);
	fmc_gpio_af12(GPIOD, LL_GPIO_PIN_0);
	fmc_gpio_af12(GPIOD, LL_GPIO_PIN_1);
	fmc_gpio_af12(GPIOE, LL_GPIO_PIN_7);
	fmc_gpio_af12(GPIOE, LL_GPIO_PIN_8);
	fmc_gpio_af12(GPIOE, LL_GPIO_PIN_9);
	fmc_gpio_af12(GPIOE, LL_GPIO_PIN_10);
	fmc_gpio_af12(GPIOE, LL_GPIO_PIN_11);
	fmc_gpio_af12(GPIOE, LL_GPIO_PIN_12);
	fmc_gpio_af12(GPIOE, LL_GPIO_PIN_13);
	fmc_gpio_af12(GPIOE, LL_GPIO_PIN_14);
	fmc_gpio_af12(GPIOE, LL_GPIO_PIN_15);
	fmc_gpio_af12(GPIOD, LL_GPIO_PIN_8);
	fmc_gpio_af12(GPIOD, LL_GPIO_PIN_9);
	fmc_gpio_af12(GPIOD, LL_GPIO_PIN_10);

	/* NBL0-1 PE0-1; SDCKE0 PC3; SDCLK PG8; SDNCAS PG15;
	 * SDNE0 PC2; SDNRAS PF11; SDNWE PC0
	 */
	fmc_gpio_af12(GPIOE, LL_GPIO_PIN_0);
	fmc_gpio_af12(GPIOE, LL_GPIO_PIN_1);
	fmc_gpio_af12(GPIOC, LL_GPIO_PIN_3);
	fmc_gpio_af12(GPIOG, LL_GPIO_PIN_8);
	fmc_gpio_af12(GPIOG, LL_GPIO_PIN_15);
	fmc_gpio_af12(GPIOC, LL_GPIO_PIN_2);
	fmc_gpio_af12(GPIOF, LL_GPIO_PIN_11);
	fmc_gpio_af12(GPIOC, LL_GPIO_PIN_0);
}

static void sdram_wait_ready(void)
{
	volatile uint32_t guard = 1000000u;

	/* MODES1 == 0 → normal mode (command complete). */
	while ((FMC_Bank5_6_R->SDSR & FMC_SDSR_MODES1) != 0u && guard-- > 0u)
		;
}

static void sdram_send_cmd(uint32_t mode, uint32_t nrfs, uint32_t mrd)
{
	FMC_Bank5_6_R->SDCMR = mode | FMC_SDCMR_CTB1 |
			       ((nrfs - 1u) << FMC_SDCMR_NRFS_Pos) |
			       (mrd << FMC_SDCMR_MRD_Pos);
	sdram_wait_ready();
}

static void fmc_sdram_init(void)
{
	/*
	 * ~100 us power-up after clock enable (Zephyr power-up-delay).
	 * CPU @ 480 MHz; spin_wait is a few cycles/iter — overshoot is fine.
	 */
	const uint32_t power_up_spins = 200000u;

	fmc_sdram_pinmux();

	/* FMC kernel clock = HCLK (240 MHz); SDCLK = HCLK/3 ≈ 80 MHz. */
	LL_RCC_SetFMCClockSource(LL_RCC_FMC_CLKSOURCE_HCLK);

	FMC_Bank5_6_R->SDCR[0] = SDRAM_SDCR0;
	FMC_Bank5_6_R->SDTR[0] = SDRAM_SDTR0;

	/* H7: FMCEN in BCR1 enables the controller. */
	FMC_Bank1_R->BTCR[0] |= FMC_BCR1_FMCEN;

	sdram_send_cmd(SDRAM_CMD_CLK_ENABLE, 1u, 0u);
	spin_wait(power_up_spins);

	sdram_send_cmd(SDRAM_CMD_PALL, 1u, 0u);
	sdram_send_cmd(SDRAM_CMD_AUTOREFRESH, SDRAM_AUTO_REFRESH_N, 0u);
	sdram_send_cmd(SDRAM_CMD_LOAD_MODE, 1u, SDRAM_MODE_REGISTER);

	MODIFY_REG(FMC_Bank5_6_R->SDRTR, FMC_SDRTR_COUNT,
		   (SDRAM_REFRESH_COUNT << FMC_SDRTR_COUNT_Pos));
}

void ulmk_board_init(void)
{
	/*
	 * Keep SWD alive across WFI (idle).  Without this, J-Link loses the
	 * core after the first sleep and all mem/reg reads return 0xA05F0001.
	 */
	LL_DBGMCU_EnableD1DebugInSleepMode();
	LL_DBGMCU_EnableD1DebugInStopMode();
	LL_DBGMCU_EnableD1DebugInStandbyMode();

	clock_init_480mhz();
	periph_clocks();
	early_usart1();
	early_leds();
	fmc_sdram_init();

	/*
	 * Privileged SDRAM probe with barriers (store-buffer safe).
	 * Result in fixed AXI marker for J-Link / board_sdram_ready().
	 */
	{
		volatile uint32_t *s = (volatile uint32_t *)0xC0000000u;
		volatile uint32_t *mark = (volatile uint32_t *)0x2407FF00u;
		uint32_t v0;
		uint32_t v1;

		s[0] = 0u;
		s[1] = 0u;
		__asm__ volatile("dsb" ::: "memory");
		s[0] = 0xA5A55A5Au;
		s[1] = 0x5A5AA5A5u;
		__asm__ volatile("dsb" ::: "memory");
		v0 = s[0];
		v1 = s[1];
		__asm__ volatile("dsb" ::: "memory");
		*mark = (v0 == 0xA5A55A5Au && v1 == 0x5A5AA5A5u) ?
			0x53444F4Bu : 0x53444641u; /* "SDOK" / "SDFA" */
		(void)*mark;
	}
}
