/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/bmhd.c — TC2xx Boot Mode Header (BMHD0 layout).
 *
 * STAD must match the linked _start address (cached flash alias).  CRC pair
 * covers the first 8 bytes (BMI + BMHDID + STAD) per Infineon UM.
 */

#include <stdint.h>

typedef struct {
	uint16_t bmi;
	uint16_t bmhdid;
	uint32_t stad;
	uint32_t crc;
	uint32_t crc_inv;
	uint32_t reserved[8];
} tc27x_bmhd_t;

extern char _start;

/*
 * BMI 0x003E, STAD = _start LMA cached alias @ 0xA0000030 — CRC precomputed.
 * Re-run tools/gen_bmhd_crc.py if STAD or BMI changes.
 */
const tc27x_bmhd_t __attribute__((section(".bmhd"), used)) _ulmk_bmhd = {
	.bmi      = 0x003Eu,
	.bmhdid   = 0xB359u,
	.stad     = 0xA0000030u,
	.crc      = 0xFEB58D07u,
	.crc_inv  = 0x014A72F8u,
	.reserved = { 0 },
};
