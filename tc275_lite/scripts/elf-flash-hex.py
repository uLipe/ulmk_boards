#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Pack ELF flash LOAD segments into one Intel HEX image @ 0x80000000."""

from __future__ import annotations

import argparse
import struct
import sys

FLASH_BASE = 0x80000000
FLASH_TOP = 0x80400000
CACHED_BASE = 0xA0000000
CACHED_TOP = 0xA0400000


def is_flash_paddr(paddr: int) -> bool:
	return (FLASH_BASE <= paddr < FLASH_TOP or
	        CACHED_BASE <= paddr < CACHED_TOP)


def normalize_paddr(paddr: int) -> int:
	if CACHED_BASE <= paddr < CACHED_TOP:
		return FLASH_BASE + (paddr - CACHED_BASE)
	return paddr


def parse_elf(path: str) -> tuple[list[dict], bytes]:
	with open(path, "rb") as fh:
		data = fh.read()

	if data[:4] != b"\x7fELF":
		raise ValueError(f"not an ELF file: {path}")

	e_phoff = struct.unpack_from("<I", data, 0x1C)[0]
	e_phentsize, e_phnum = struct.unpack_from("<HH", data, 0x2A)
	segments = []

	for i in range(e_phnum):
		off = e_phoff + i * e_phentsize
		if off + 32 > len(data):
			break
		p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz = struct.unpack_from(
			"<IIIIII", data, off)
		if p_type != 1 or p_filesz == 0:
			continue
		if not is_flash_paddr(p_paddr):
			continue
		segments.append({
			"offset": p_offset,
			"paddr": normalize_paddr(p_paddr),
			"filesz": p_filesz,
		})

	segments.sort(key=lambda s: s["offset"])
	return segments, data


def pack_image(segments: list[dict], data: bytes) -> tuple[int, bytearray]:
	if not segments:
		raise ValueError("no flash LOAD segments in ELF")

	max_end = 0
	for seg in segments:
		end = seg["paddr"] + seg["filesz"]
		if end > max_end:
			max_end = end

	image = bytearray(b"\xff" * (max_end - FLASH_BASE))
	for seg in segments:
		off = seg["paddr"] - FLASH_BASE
		chunk = data[seg["offset"]:seg["offset"] + seg["filesz"]]
		image[off:off + len(chunk)] = chunk

	return FLASH_BASE, image


def checksum(parts: list[int]) -> int:
	return (~sum(parts) + 1) & 0xFF


def ihex_line(addr: int, rectype: int, rec: bytes) -> str:
	count = len(rec)
	parts = [count, (addr >> 8) & 0xFF, addr & 0xFF, rectype, *rec]
	cs = checksum(parts)
	body = f"{count:02X}{addr:04X}{rectype:02X}{rec.hex().upper()}"
	return f":{body}{cs:02X}"


def write_ihex(path: str, base: int, image: bytes) -> None:
	lines: list[str] = []
	upper = (base >> 16) & 0xFFFF
	lines.append(ihex_line(0, 4, bytes([(upper >> 8) & 0xFF, upper & 0xFF])))

	addr = base & 0xFFFF
	upper_cur = upper
	pos = 0

	while pos < len(image):
		if (base + pos) >> 16 != upper_cur:
			upper_cur = (base + pos) >> 16
			lines.append(ihex_line(0, 4, bytes([
				(upper_cur >> 8) & 0xFF, upper_cur & 0xFF])))
			addr = (base + pos) & 0xFFFF

		chunk = image[pos:pos + 16]
		lines.append(ihex_line(addr, 0, chunk))
		addr = (addr + len(chunk)) & 0xFFFF
		pos += len(chunk)

	lines.append(":00000001FF")
	with open(path, "w", encoding="ascii") as fh:
		fh.write("\n".join(lines))
		fh.write("\n")


def main() -> int:
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument("elf")
	ap.add_argument("hex_out")
	args = ap.parse_args()

	segments, data = parse_elf(args.elf)
	base, image = pack_image(segments, data)
	write_ihex(args.hex_out, base, image)
	last_sector = (len(image) - 1) // 0x4000
	print(f"packed {len(image)} bytes from {len(segments)} segments @ 0x{base:08x} "
	      f"sectors 0-{last_sector}",
	      file=sys.stderr)
	return 0


if __name__ == "__main__":
	sys.exit(main())
