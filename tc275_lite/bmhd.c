/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/bmhd.c — TC2xx Boot Mode Header (BMHD0 @ 0xA0000000).
 *
 * Layout matches Infineon UM / Lauterbach BMHD0 window (0x20 bytes).
 * STAD is the cached-alias address of _start (see bmhd.ld.in).
 * CRC covers BMI + BMHDID + STAD (first 8 bytes).
 */

#include <stdint.h>

typedef struct {
	uint16_t bmi;
	uint16_t bmhdid;
	uint32_t stad;
	uint32_t crc;
	uint32_t crc_inv;
	uint32_t reserved[4]; /* pad to 0x20 — end of BMHD0 */
} tc27x_bmhd_t;

/*
 * BMI 0x003E, STAD = _start @ 0xA0000020 — CRC from tools/gen_bmhd_crc.py.
 * Re-run that script if STAD or BMI changes.
 */
const tc27x_bmhd_t __attribute__((section(".bmhd"), used)) _ulmk_bmhd = {
	.bmi      = 0x003Eu,
	.bmhdid   = 0xB359u,
	.stad     = 0xA0000020u,
	.crc      = 0xE3029D63u,
	.crc_inv  = 0x1CFD629Cu,
	.reserved = { 0 },
};
