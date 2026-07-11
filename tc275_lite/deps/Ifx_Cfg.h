/* SPDX-License-Identifier: MIT */
/*
 * Board-local Ifx_Cfg.h for Infineon iLLD (TC27D / TC275 Lite Kit).
 *
 * XTAL = 20 MHz (X301), PLL target = 200 MHz — matches board_config.h.
 * IFASLL applies to the iLLD sources under deps/illd_tc2x/; this shim is MIT.
 */

#ifndef IFX_CFG_H
#define IFX_CFG_H 1

#define IFX_CFG_SCU_XTAL_FREQUENCY	(20000000)
#define IFX_CFG_SCU_PLL_FREQUENCY	(200000000)

/*
 * Do not pull Infineon default linker symbols (__A0_MEM, …) — ulmk owns
 * the linker script.
 */
#define IFX_CFG_USE_COMPILER_DEFAULT_LINKER

#endif /* IFX_CFG_H */
