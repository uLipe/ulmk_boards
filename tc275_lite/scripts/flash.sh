#!/usr/bin/env bash
# tc275_lite/scripts/flash.sh — program ulmk.elf over TAS + AURIX OpenOCD.
#
# Contract: convert ELF → erase → program → reset halt/resume → exit.
# Never leaves OpenOCD hanging; each phase has a hard timeout.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:-}"
VERBOSE="${ULMK_FLASH_VERBOSE:-0}"
VERIFY="${ULMK_FLASH_VERIFY:-0}"
# Wall-clock budget for the program phase (TAS is slow even with burst).
FLASH_TIMEOUT="${ULMK_FLASH_TIMEOUT:-90}"

# shellcheck source=/dev/null
source "$(dirname "$0")/aurix-env.sh"
if ! source_aurix_env; then
	echo "env.sh not found — run: scripts/install-host-tools.sh" >&2
	exit 1
fi

OPENOCD="${ULMK_AURIX_PREFIX}/bin/openocd"
OCD_SCRIPTS="${ULMK_AURIX_PREFIX}/share/openocd/scripts"
BOARD_OCD="${ROOT}/openocd"
# CPU0-only: SMP examine of cpu1/cpu2 only slows flash and is unused.
OCD_CFG="${BOARD_OCD}/tc275_lite_hil.cfg"

log() { echo "$*"; }
vlog() { [[ "$VERBOSE" -eq 1 ]] && echo "$*" || true; }

elf_to_hex() {
	local elf="$1" hex="$2"

	python3 "${ROOT}/scripts/elf-flash-hex.py" "$elf" "$hex" || return 1
	[[ -s "$hex" ]] || {
		echo "failed to produce Intel HEX from ELF: ${elf}" >&2
		return 1
	}
}

kill_openocd() {
	# Match the binary name only — never -f with a path (kills this shell).
	pkill -9 -x openocd 2>/dev/null || true
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

vlog "openocd: ${OPENOCD}"
vlog "elf:     ${ELF}"
vlog "config:  ${OCD_CFG}"
[[ "$VERBOSE" -eq 1 ]] && "$OPENOCD" --version | head -1

HEX="$(mktemp "${TMPDIR:-/tmp}/ulmk-flash-XXXXXX.hex")"
OCD_LOG="$(mktemp "${TMPDIR:-/tmp}/ulmk-flash-XXXXXX.log")"
cleanup() {
	kill_openocd
	rm -f "$HEX" "$OCD_LOG"
}
trap cleanup EXIT

META="$(elf_to_hex "$ELF" "$HEX" 2>&1)"
vlog "$META"
LAST_SECTOR="$(echo "$META" | sed -n 's/.*sectors 0-\([0-9][0-9]*\).*/\1/p')"
if [[ -z "$LAST_SECTOR" ]]; then
	echo "failed to parse sector range from elf-flash-hex.py" >&2
	exit 1
fi

BYTES="$(echo "$META" | sed -n 's/.*packed \([0-9][0-9]*\) bytes.*/\1/p')"
[[ -n "$BYTES" ]] || BYTES="?"

run_ocd() {
	local label="$1"
	local timeout_s="$2"
	local cmd="$3"
	local cfg="${4:-$OCD_CFG}"
	local t0 t1 ec

	kill_openocd
	sleep 0.2

	t0="$(date +%s)"
	set +e
	timeout --signal=KILL "${timeout_s}" \
		"$OPENOCD" "${OCD_SEARCH[@]}" -f "$cfg" -c "$cmd" \
		>"$OCD_LOG" 2>&1
	ec=$?
	set -e
	t1="$(date +%s)"

	if [[ "$ec" -ne 0 ]]; then
		echo "FAIL: ${label} (exit ${ec}, $((t1 - t0))s)" >&2
		tail -30 "$OCD_LOG" >&2 || true
		return "$ec"
	fi
	vlog "ok: ${label} ($((t1 - t0))s)"
	return 0
}

log "flash: $(basename "$ELF") (${BYTES} B, sectors 0-${LAST_SECTOR})"

# TC27x: erase and program must be separate OpenOCD sessions
# (write_image fails if erase ran in the same init).
log "  erase..."
run_ocd erase 20 \
	"gdb port disabled; init; targets tc27x.cpu0; halt; flash erase_sector pflash0 0 ${LAST_SECTOR}; shutdown"

log "  program..."
if [[ "$VERIFY" -eq 1 ]]; then
	run_ocd program "$FLASH_TIMEOUT" \
		"gdb port disabled; init; targets tc27x.cpu0; halt; flash write_image ${HEX} 0; verify_image ${HEX} 0; shutdown"
else
	run_ocd program "$FLASH_TIMEOUT" \
		"gdb port disabled; init; targets tc27x.cpu0; halt; flash write_image ${HEX} 0; shutdown"
fi

# Final action MUST be a genuine "reset & run" (TAS FEAT_RESET=0x0001).
# That leaves OSTATE.OEN=0 so the next button/PORST boots without Halt-After-
# Reset.  Disable reset-end DAP pokes — they fail mid-reset ("OCDS sequence")
# and are unnecessary: board_wdt_early.S + board_init handle WDT/EndInit in SW.
log "  release..."
run_ocd release 20 \
	"gdb port disabled; init; targets tc27x.cpu0; tc27x.cpu0 configure -event reset-end {}; reset run; after 300; shutdown"

log "  flash + start — done"
