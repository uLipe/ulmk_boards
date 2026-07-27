/* SPDX-License-Identifier: MIT */
/*
 * STM32H753 device + LL compile-time config for witte_linum.
 * Include this (or board_ll.h) before any stm32h7xx / LL header.
 */
#ifndef WITTE_LINUM_STM32_CONF_H
#define WITTE_LINUM_STM32_CONF_H

#ifndef STM32H753xx
#define STM32H753xx
#endif

#ifndef HSE_VALUE
#define HSE_VALUE	25000000U
#endif

/* Do not pull in HAL. */

#endif /* WITTE_LINUM_STM32_CONF_H */
