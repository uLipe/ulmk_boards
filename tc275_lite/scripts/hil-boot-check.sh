#!/usr/bin/env bash
# HIL boot check — GDB HW breakpoint on ulmk_root_thread.
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
TIMEOUT_S="${HIL_BOOT_TIMEOUT_S:-45}"
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
"$OCD" "${OCD_SEARCH[@]}" -f "${OCD_CFG}" >/tmp/ulmk-ocd-hil.log 2>&1 &
sleep 3

root_addr="$(docker run --rm -v "$(dirname "$ELF"):/elf" ulipe-microkernel:dev \
	tricore-elf-nm -n "/elf/$(basename "$ELF")" 2>/dev/null \
	| awk '/ ulmk_root_thread$/ {print $1}')"
panic_addr="$(docker run --rm -v "$(dirname "$ELF"):/elf" ulipe-microkernel:dev \
	tricore-elf-nm -n "/elf/$(basename "$ELF")" 2>/dev/null \
	| awk '/ ulmk_kern_trap_panic$/ {print $1}')"

echo "ELF: $(basename "$ELF")"
echo "  ulmk_root_thread  ${root_addr:-?}"
echo "  trap_panic        ${panic_addr:-?}"

out="$(docker run --rm --network host -v "$(dirname "$ELF"):/elf" ulipe-microkernel:dev \
	timeout "${TIMEOUT_S}" tricore-elf-gdb -batch "/elf/$(basename "$ELF")" \
	-ex "set remotetimeout ${TIMEOUT_S}" \
	-ex "target extended-remote :${GDB_PORT}" \
	-ex 'monitor reset halt' \
	-ex 'info register pc' \
	-ex 'break ulmk_kern_trap_panic' \
	-ex 'break ulmk_root_thread' \
	-ex 'continue' \
	-ex 'info register pc' \
	-ex 'bt 4' 2>&1)" || true

echo "$out"

if echo "$out" | grep -qE 'Breakpoint .*ulmk_root_thread|in ulmk_root_thread'; then
	echo "PASS: stopped at ulmk_root_thread"
	exit 0
fi

if echo "$out" | grep -qE 'in ulmk_kern_trap_panic'; then
	echo "FAIL: stopped at ulmk_kern_trap_panic" >&2
	exit 1
fi

echo "FAIL: ulmk_root_thread not reached within ${TIMEOUT_S}s" >&2
exit 1
