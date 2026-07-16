/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/bmhd.c — TC27x Boot Mode Header (BMHD0 @ 0xA0000000).
 *
 * Layout is TC2xx-specific (Infineon TC27x UM Table 4-1), NOT the
 * TC3xx/iLLD "BMI, BMHDID, STAD, CRC" order:
 *
 *   00H STADABM     user code start (cached PFlash)
 *   04H BMI         boot mode index
 *   06H BMHDID      must be B359H
 *   08H ChkStart    ABM range (0 if Internal Flash start)
 *   0CH ChkEnd
 *   10H CRCrange / CRCrangeInv
 *   18H CRChead / CRCheadInv  — CRC over first 24 bytes (00H..17H)
 *
 * BMI 0x0078 = PINDIS=1, HWCFG=111B (Internal start from Flash), lockstep off.
 * A wrong layout makes BootROM reject every BMHD → Generic BSL / ESR0 red on
 * button and PORST, while OpenOCD "reset; resume" still works by forcing PC.
 */

#include <stdint.h>

typedef struct {
	uint32_t stad;		/* 00: STADABM */
	uint16_t bmi;		/* 04 */
	uint16_t bmhdid;	/* 06 */
	uint32_t chk_start;	/* 08 */
	uint32_t chk_end;	/* 0C */
	uint32_t crc_range;	/* 10 */
	uint32_t crc_range_inv;	/* 14 */
	uint32_t crc_head;	/* 18 */
	uint32_t crc_head_inv;	/* 1C */
} tc27x_bmhd_t;

/*
 * STAD = _start @ 0xA0000020.  CRC from tools/gen_bmhd_crc.py (24-byte header).
 * Re-run that script if STAD or BMI changes.
 */
const tc27x_bmhd_t __attribute__((section(".bmhd"), used)) _ulmk_bmhd = {
	.stad          = 0xA0000020u,
	.bmi           = 0x0078u,
	.bmhdid        = 0xB359u,
	.chk_start     = 0u,
	.chk_end       = 0u,
	.crc_range     = 0u,
	.crc_range_inv = 0u,
	.crc_head      = 0xBBA9D932u,
	.crc_head_inv  = 0x445626CDu,
};
