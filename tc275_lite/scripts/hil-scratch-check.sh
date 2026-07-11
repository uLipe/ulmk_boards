#!/usr/bin/env bash
# Poll g_ulmk_board_hil_scratch after reset run (console server milestone >= 4).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:-/home/ulipe/fun/build/ulipe-tricore-tc275_lite/ulmk}"
WAIT_MS="${HIL_SCRATCH_WAIT_MS:-8000}"
MIN_MARK="${HIL_SCRATCH_MIN:-4}"
OCD_CFG="${ULMK_OCD_CFG:-${ROOT}/openocd/tc275_lite_hil.cfg}"

# shellcheck source=/dev/null
source "$(dirname "$0")/aurix-env.sh"
source_aurix_env || exit 1

if [[ ! -f "$ELF" ]]; then
	echo "ELF not found: ${ELF}" >&2
	exit 1
fi

if ! pgrep -f tas_server >/dev/null 2>&1; then
	echo "tas_server not running" >&2
	exit 1
fi

scratch_addr="$(docker run --rm -v "$(dirname "$ELF"):/elf" ulipe-microkernel:dev \
	tricore-elf-nm -n "/elf/$(basename "$ELF")" 2>/dev/null \
	| awk '/ g_ulmk_board_hil_scratch$/ {print $1}')"

if [[ -z "$scratch_addr" ]]; then
	echo "g_ulmk_board_hil_scratch not found in ELF" >&2
	exit 1
fi

"${ROOT}/scripts/flash.sh" "$ELF" >/dev/null 2>&1

OCD="${ULMK_AURIX_PREFIX}/bin/openocd"
OCD_SEARCH=(-s "${ULMK_AURIX_PREFIX}/share/openocd/scripts" -s "${ROOT}/openocd")
pkill -9 -f "${OCD}" 2>/dev/null || true
sleep 1

out="$("$OCD" "${OCD_SEARCH[@]}" -f "${OCD_CFG}" \
	-c "init; targets tc27x.cpu0; resume; after ${WAIT_MS}; halt; mdw 0x${scratch_addr}; reg pc; shutdown" 2>&1)" || true

echo "$out" | tail -15

val="$(echo "$out" | sed -n "s/^0x${scratch_addr}: //p" | awk '{print $1}' | head -1)"
val_dec="$(printf "%d" "0x${val:-0}" 2>/dev/null || echo 0)"
pc="$(echo "$out" | sed -n 's/^pc: //p' | awk '{print $1}' | head -1)"

echo "HIL scratch @ 0x${scratch_addr}: ${val:-?} (dec ${val_dec})"
echo "CPU0 pc: ${pc:-?}"

if [[ -n "${val:-}" ]] && (( val_dec >= MIN_MARK )); then
	echo "PASS: milestone >= ${MIN_MARK}"
	exit 0
fi

echo "FAIL: milestone < ${MIN_MARK}" >&2
exit 1
