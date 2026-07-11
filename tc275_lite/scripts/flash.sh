#!/usr/bin/env bash
# tc275_lite/scripts/flash.sh — program ulmk.elf over TAS + AURIX OpenOCD.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:-}"
VERBOSE="${ULMK_FLASH_VERBOSE:-1}"
VERIFY="${ULMK_FLASH_VERIFY:-0}"

# shellcheck source=/dev/null
source "$(dirname "$0")/aurix-env.sh"
if ! source_aurix_env; then
	echo "env.sh not found — run: scripts/install-host-tools.sh" >&2
	exit 1
fi

OPENOCD="${ULMK_AURIX_PREFIX}/bin/openocd"
OCD_SCRIPTS="${ULMK_AURIX_PREFIX}/share/openocd/scripts"
BOARD_OCD="${ROOT}/openocd"
elf_to_hex() {
	local elf="$1" hex="$2"

	python3 "${ROOT}/scripts/elf-flash-hex.py" "$elf" "$hex" || return 1
	[[ -s "$hex" ]] || {
		echo "failed to produce Intel HEX from ELF: ${elf}" >&2
		return 1
	}
}

if [[ -z "$ELF" || ! -f "$ELF" ]]; then
	echo "usage: $0 <path/to/ulmk.elf>" >&2
	exit 1
fi

if [[ ! -x "$OPENOCD" ]]; then
	echo "openocd not found at ${OPENOCD}" >&2
	echo "run: scripts/install-host-tools.sh" >&2
	exit 1
fi

if [[ ! -f "${OCD_SCRIPTS}/interface/tas_client.cfg" ]]; then
	echo "aurix-openocd scripts missing under ${OCD_SCRIPTS}" >&2
	echo "re-run: scripts/install-host-tools.sh --skip-apt --skip-ftdi --skip-tas --skip-toolchain" >&2
	exit 1
fi

if ! pgrep -f tas_server >/dev/null 2>&1; then
	echo "warning: tas_server does not appear to be running." >&2
	echo "         start it first: ${ROOT}/scripts/start-tas.sh" >&2
fi

OCD_SEARCH=(-s "$OCD_SCRIPTS" -s "$BOARD_OCD")

if [[ "$VERBOSE" -eq 1 ]]; then
	echo "openocd: ${OPENOCD}"
	echo "elf:     ${ELF}"
	echo "config:  ${BOARD_OCD}/tc275_lite.cfg"
	"$OPENOCD" --version | head -1
fi

HEX="$(mktemp "${TMPDIR:-/tmp}/ulmk-flash-XXXXXX.hex")"
cleanup() { rm -f "$HEX"; }
trap cleanup EXIT

META="$(elf_to_hex "$ELF" "$HEX" 2>&1)"
echo "$META" >&2
LAST_SECTOR="$(echo "$META" | sed -n 's/.*sectors 0-\([0-9][0-9]*\).*/\1/p')"
if [[ -z "$LAST_SECTOR" ]]; then
	echo "failed to parse sector range from elf-flash-hex.py" >&2
	exit 1
fi

if [[ "$VERBOSE" -eq 1 ]]; then
	echo "hex:     ${HEX} ($(wc -l < "$HEX") records)"
	echo "erase:   pflash0 sectors 0-${LAST_SECTOR}"
fi

run_ocd() {
	"$OPENOCD" "${OCD_SEARCH[@]}" -f "${BOARD_OCD}/tc275_lite.cfg" -c "$1"
}

# TC27x: erase and program in separate OpenOCD sessions (write cmds fail after erase).
pkill -9 -f '[o]penocd' 2>/dev/null || true
sleep 0.3

if [[ "$VERBOSE" -eq 1 ]]; then
	echo "phase 1: erase"
fi
run_ocd "gdb port disabled; init; flash erase_sector pflash0 0 ${LAST_SECTOR}; shutdown"

if [[ "$VERBOSE" -eq 1 ]]; then
	echo "phase 2: program"
fi
if [[ "$VERIFY" -eq 1 ]]; then
	run_ocd "gdb port disabled; init; flash write_image ${HEX} 0; verify_image ${HEX} 0; reset run; shutdown"
else
	run_ocd "gdb port disabled; init; flash write_image ${HEX} 0; reset run; shutdown"
fi
