#!/usr/bin/env bash
# witte_linum/scripts/flash.sh — program ulmk ELF via ST-Link (OpenOCD) or J-Link.
#
# Usage: flash.sh <path/to/ulmk.elf>
# Env:   ULMK_PROBE=stlink|jlink   (default jlink on this board)
#        ULMK_FLASH_TIMEOUT=60
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:-}"
PROBE="${ULMK_PROBE:-jlink}"
FLASH_TIMEOUT="${ULMK_FLASH_TIMEOUT:-60}"

if [[ -z "$ELF" || ! -f "$ELF" ]]; then
	echo "usage: $0 <path/to/ulmk.elf>" >&2
	exit 1
fi

BIN="$(mktemp "${TMPDIR:-/tmp}/ulmk-flash-XXXXXX.bin")"
SCRIPT=""
cleanup() {
	rm -f "$BIN" ${SCRIPT:+"$SCRIPT"}
}
trap cleanup EXIT

OBJCOPY="${ULMK_OBJCOPY:-arm-none-eabi-objcopy}"
if ! command -v "$OBJCOPY" >/dev/null 2>&1; then
	echo "objcopy not found: $OBJCOPY" >&2
	exit 1
fi
"$OBJCOPY" -O binary "$ELF" "$BIN"
BYTES="$(wc -c < "$BIN" | tr -d ' ')"
echo "flash: ${ELF} → ${BYTES} bytes (probe=${PROBE})"

case "$PROBE" in
stlink|openocd)
	OPENOCD="${ULMK_OPENOCD:-openocd}"
	OCD_CFG="${ROOT}/openocd/witte_linum.cfg"
	if ! command -v "$OPENOCD" >/dev/null 2>&1; then
		echo "openocd not found — install OpenOCD or set ULMK_OPENOCD" >&2
		exit 1
	fi
	timeout --kill-after=5 "$FLASH_TIMEOUT" "$OPENOCD" \
		-f "$OCD_CFG" \
		-c "program ${BIN} 0x08000000 verify reset exit" \
		|| { echo "flash failed (openocd)" >&2; exit 1; }
	;;
jlink)
	JLINK="${ULMK_JLINK:-JLinkExe}"
	SCRIPT="$(mktemp "${TMPDIR:-/tmp}/ulmk-jlink-XXXXXX.jlink")"
	cat >"$SCRIPT" <<EOF
si SWD
speed 4000
connect
r
h
loadbin ${BIN} 0x08000000
verifybin ${BIN} 0x08000000
r
g
q
EOF
	timeout --kill-after=5 "$FLASH_TIMEOUT" "$JLINK" \
		-device STM32H753ZI -if SWD -speed 4000 -autoconnect 1 \
		-CommanderScript "$SCRIPT" \
		|| { echo "flash failed (jlink)" >&2; exit 1; }
	;;
*)
	echo "unknown ULMK_PROBE=${PROBE} (use stlink or jlink)" >&2
	exit 1
	;;
esac

echo "flash + start — done"
