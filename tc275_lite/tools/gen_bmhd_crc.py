#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compute TC27x BMHD CRC (IEEE 802.3 / TriCore CRC instruction bit order)."""

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


def bmhd_crc(bmi: int, bmhdid: int, stad: int) -> tuple[int, int]:
	w0 = (bmhdid << 16) | (bmi & 0xFFFF)
	crc = 0xFFFFFFFF
	crc = _crc32_tricore_word(crc, w0)
	crc = _crc32_tricore_word(crc, stad & 0xFFFFFFFF)
	crc = _reflect32((~crc) & 0xFFFFFFFF)
	return crc, (~crc) & 0xFFFFFFFF


def main() -> int:
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument("--bmi", type=lambda x: int(x, 0), default=0x003E)
	ap.add_argument("--bmhdid", type=lambda x: int(x, 0), default=0xB359)
	ap.add_argument("--stad", type=lambda x: int(x, 0), default=0xA0000030)
	args = ap.parse_args()

	crc, crc_inv = bmhd_crc(args.bmi, args.bmhdid, args.stad)
	print(f"STAD=0x{args.stad:08X}")
	print(f"CRC     = 0x{crc:08X}")
	print(f"CRC_INV = 0x{crc_inv:08X}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
