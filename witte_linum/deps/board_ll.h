/* SPDX-License-Identifier: MIT */
#ifndef WITTE_LINUM_BOARD_LL_H
#define WITTE_LINUM_BOARD_LL_H

#include "stm32_conf.h"
#include "stm32h7xx.h"
#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_rcc.h"
#include "stm32h7xx_ll_system.h"
#include "stm32h7xx_ll_pwr.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_usart.h"
#include "stm32h7xx_ll_tim.h"
#include "stm32h7xx_ll_adc.h"
#include "stm32h7xx_ll_dma.h"
#include "stm32h7xx_ll_dmamux.h"
#include "stm32h7xx_ll_cortex.h"
#include "stm32h7xx_ll_utils.h"

/*
 * stm32h7xx_ll_fmc.h is NOT included: it pulls stm32h7xx_hal_def.h.
 * FMC SDRAM bring-up in board_init.c uses CMSIS FMC_* register defs.
 */

#endif /* WITTE_LINUM_BOARD_LL_H */
