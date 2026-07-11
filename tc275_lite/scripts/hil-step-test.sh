#!/usr/bin/env bash
# HIL single-step smoke test — steps from ulmk_kern_main toward ulmk_root_thread.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# shellcheck source=/dev/null
source "$(dirname "$0")/aurix-env.sh"
source_aurix_env || {
	echo "env.sh not found" >&2
	exit 1
}

ELF="${1:-/home/ulipe/fun/build/ulipe-tricore-tc275_lite/ulmk}"
GDB_PORT="${ULMK_OCD_GDB_PORT:-3333}"
TIMEOUT_S="${HIL_STEP_TIMEOUT_S:-60}"
OCD_CFG="${ULMK_OCD_CFG:-${ROOT}/openocd/tc275_lite_hil.cfg}"

if [[ ! -f "$ELF" ]]; then
	echo "ELF not found: ${ELF}" >&2
	exit 1
fi

if ! pgrep -f tas_server >/dev/null 2>&1; then
	echo "tas_server not running — start: ${ROOT}/scripts/start-tas.sh" >&2
	exit 1
fi

OCD="${ULMK_AURIX_PREFIX}/bin/openocd"
OCD_SEARCH=(-s "${ULMK_AURIX_PREFIX}/share/openocd/scripts" -s "${ROOT}/openocd")

pkill -9 -f "${OCD}" 2>/dev/null || true
sleep 1
"$OCD" "${OCD_SEARCH[@]}" -f "${OCD_CFG}" >/tmp/ulmk-ocd-step.log 2>&1 &
sleep 3

kern_main="$(docker run --rm -v "$(dirname "$ELF"):/elf" ulipe-microkernel:dev \
	tricore-elf-nm -n "/elf/$(basename "$ELF")" 2>/dev/null \
	| awk '/ ulmk_kern_main$/ {print $1}')"
root_addr="$(docker run --rm -v "$(dirname "$ELF"):/elf" ulipe-microkernel:dev \
	tricore-elf-nm -n "/elf/$(basename "$ELF")" 2>/dev/null \
	| awk '/ ulmk_root_thread$/ {print $1}')"

echo "ELF: $(basename "$ELF")"
echo "  ulmk_kern_main    ${kern_main:-?}"
echo "  ulmk_root_thread  ${root_addr:-?}"

out="$(docker run --rm --network host -v "$(dirname "$ELF"):/elf" ulipe-microkernel:dev \
	timeout "${TIMEOUT_S}" tricore-elf-gdb -batch "/elf/$(basename "$ELF")" \
	-ex "set remotetimeout ${TIMEOUT_S}" \
	-ex "target extended-remote :${GDB_PORT}" \
	-ex 'monitor reset halt' \
	-ex "break *0x${kern_main}" \
	-ex 'continue' \
	-ex 'info register pc' \
	-ex 'stepi' \
	-ex 'info register pc' \
	-ex 'stepi' \
	-ex 'info register pc' \
	-ex 'stepi' \
	-ex 'info register pc' \
	-ex "break *0x${root_addr}" \
	-ex 'continue' \
	-ex 'info register pc' \
	-ex 'stepi' \
	-ex 'info register pc' \
	-ex 'stepi' \
	-ex 'info register pc' 2>&1)" || true

echo "$out"

if echo "$out" | grep -qE "in ulmk_root_thread|0x${root_addr}"; then
	echo "PASS: reached ulmk_root_thread"
else
	echo "FAIL: did not reach ulmk_root_thread" >&2
	exit 1
fi

pc_lines="$(echo "$out" | grep -E '^pc[[:space:]]+' || true)"
if [[ "$(echo "$pc_lines" | wc -l)" -lt 4 ]]; then
	echo "FAIL: PC did not advance across stepi commands" >&2
	exit 1
fi

echo "PASS: stepi advanced PC"
exit 0
