#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compute TC27x BMHD CRChead (IEEE 802.3 / TriCore CRC bit order).

TC27x UM Table 4-1 — CRC covers the first 24 bytes (offsets 00H..17H):
  STADABM, BMI|BMHDID, ChkStart, ChkEnd, CRCrange, CRCrangeInv
Result goes at CRChead (18H); CRCheadInv (1CH) = ~CRChead.

Validated against a known TC277 header (STAD=0, BMI=0x0070 → 0x791EB864).
"""

from __future__ import annotations

import argparse

POLY = 0x04C11DB7


def _reflect32(x: int) -> int:
	x = ((x & 0xFFFF0000) >> 16) | ((x & 0x0000FFFF) << 16)
	x = ((x & 0xFF00FF00) >> 8) | ((x & 0x00FF00FF) << 8)
	x = ((x & 0xF0F0F0F0) >> 4) | ((x & 0x0F0F0F0F) << 4)
	x = ((x & 0xCCCCCCCC) >> 2) | ((x & 0x33333333) << 2)
	x = ((x & 0xAAAAAAAA) >> 1) | ((x & 0x55555555) << 1)
	return x & 0xFFFFFFFF


def _crc32_tricore_word(crc: int, word: int) -> int:
	for shift in (24, 16, 8, 0):
		byte = (word >> shift) & 0xFF
		for bit in range(8):
			bitval = (byte >> bit) & 1
			msb = (crc >> 31) & 1
			crc = (crc << 1) & 0xFFFFFFFF
			if msb ^ bitval:
				crc ^= POLY
	return crc


def bmhd_crc_head(stad: int, bmi: int, bmhdid: int = 0xB359,
		  chk_start: int = 0, chk_end: int = 0,
		  crc_range: int = 0, crc_range_inv: int = 0) -> tuple[int, int]:
	words = [
		stad & 0xFFFFFFFF,
		((bmhdid & 0xFFFF) << 16) | (bmi & 0xFFFF),
		chk_start & 0xFFFFFFFF,
		chk_end & 0xFFFFFFFF,
		crc_range & 0xFFFFFFFF,
		crc_range_inv & 0xFFFFFFFF,
	]
	crc = 0xFFFFFFFF
	for w in words:
		crc = _crc32_tricore_word(crc, w)
	crc = _reflect32((~crc) & 0xFFFFFFFF)
	return crc, (~crc) & 0xFFFFFFFF


def main() -> int:
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument("--stad", type=lambda x: int(x, 0), default=0xA0000020)
	ap.add_argument("--bmi", type=lambda x: int(x, 0), default=0x0078)
	ap.add_argument("--bmhdid", type=lambda x: int(x, 0), default=0xB359)
	args = ap.parse_args()

	crc, crc_inv = bmhd_crc_head(args.stad, args.bmi, args.bmhdid)
	print(f"STAD     = 0x{args.stad:08X}")
	print(f"BMI      = 0x{args.bmi:04X}")
	print(f"BMHDID   = 0x{args.bmhdid:04X}")
	print(f"CRChead  = 0x{crc:08X}")
	print(f"CRCheadInv = 0x{crc_inv:08X}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
