#!/usr/bin/env bash
# flash.sh — bootloader @0x2000 + partition @0x8000 + ulmk app @0x10000
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT}/scripts/hil-config.sh"

ELF="${1:-}"
if [[ -z "$ELF" || ! -f "$ELF" ]]; then
	echo "usage: $0 <path/to/ulmk>" >&2
	exit 1
fi

BIN="${ELF}.bin"
if [[ ! -f "$BIN" ]] || [[ "$ELF" -nt "$BIN" ]]; then
	echo "--- elf2image ---"
	$ESPTOOL --chip "${ULMK_HIL_CHIP}" elf2image \
		--use_segments \
		--min-rev-full 0 --max-rev-full 65535 \
		--flash_mode dio --flash_freq 80m --flash_size 16MB \
		-o "$BIN" "$ELF"
fi

BL="${ULMK_PREBUILT}/bootloader.bin"
PT="${ULMK_PREBUILT}/partition-table.bin"
if [[ ! -f "$BL" || ! -f "$PT" ]]; then
	echo "missing prebuilt bootloader/partition under ${ULMK_PREBUILT}" >&2
	exit 1
fi

echo "--- flash (${ULMK_HIL_SERIAL}) ---"
$ESPTOOL --chip "${ULMK_HIL_CHIP}" -p "${ULMK_HIL_SERIAL}" \
	-b "${ULMK_HIL_FLASH_BAUD}" \
	--before default_reset --after hard_reset \
	write_flash \
	0x2000 "$BL" \
	0x8000 "$PT" \
	0x10000 "$BIN"

echo "flash + start — done"
