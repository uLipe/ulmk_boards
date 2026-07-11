#!/usr/bin/env bash
# HIL timer smoke — flash ELF that calls board_services_init + sleep.
# Expects scratch milestone >= 3 and no trap_panic.
# Typical ELF: silicon_stress or silicon_e2e after timer fix.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# shellcheck source=/dev/null
source "$(dirname "$0")/aurix-env.sh"
source_aurix_env || exit 1

ELF="${1:-/home/ulipe/fun/build/ulipe-tricore-tc275_lite/ulmk}"
GDB_PORT="${ULMK_OCD_GDB_PORT:-3333}"
if [[ -n "${HIL_TIMER_BP:-}" ]]; then
	BP="$HIL_TIMER_BP"
else
	SYMS="$(docker run --rm -v "$(dirname "$ELF"):/elf" ulipe-microkernel:dev \
		tricore-elf-nm "/elf/$(basename "$ELF")" 2>/dev/null || true)"
	if printf '%s\n' "$SYMS" | grep -q ' silicon_stress_done$'; then
		BP=silicon_stress_done
	elif printf '%s\n' "$SYMS" | grep -q ' silicon_e2e_done$'; then
		BP=silicon_e2e_done
	else
		BP=ulmk_thread_exit
	fi
fi

if [[ ! -f "$ELF" ]]; then
	echo "ELF not found: ${ELF}" >&2
	exit 1
fi
if ! pgrep -f tas_server >/dev/null; then
	echo "tas_server not running" >&2
	exit 1
fi

echo "ELF: $(basename "$ELF")  bp=${BP}"

"${ROOT}/scripts/flash.sh" "$ELF" >/dev/null

OCD="${ULMK_AURIX_PREFIX}/bin/openocd"
pkill -9 -f "${OCD}" 2>/dev/null || true
sleep 1
"$OCD" -s "${ULMK_AURIX_PREFIX}/share/openocd/scripts" -s "${ROOT}/openocd" \
	-f "${ROOT}/openocd/tc275_lite_hil.cfg" >/tmp/ulmk-ocd-timer.log 2>&1 &
sleep 3

GDB_OUT=/tmp/ulmk-gdb-timer.log
cleanup() { pkill -9 -f "${OCD}" 2>/dev/null || true; }
trap cleanup EXIT

docker run --rm --network host -v "$(dirname "$ELF"):/elf" ulipe-microkernel:dev \
	timeout 60 tricore-elf-gdb -batch "/elf/$(basename "$ELF")" \
	-ex "set remotetimeout 60" \
	-ex "target extended-remote :${GDB_PORT}" \
	-ex "monitor reset halt" \
	-ex "break ulmk_kern_trap_panic" \
	-ex "break ${BP}" \
	-ex "continue" \
	-ex "x/wx &g_ulmk_board_hil_scratch" \
	-ex "bt 4" >"$GDB_OUT" 2>&1 || true

echo "--- gdb ---"
/usr/bin/tail -40 "$GDB_OUT"
echo "--- end gdb ---"

if grep -qE "Breakpoint .*ulmk_kern_trap_panic" "$GDB_OUT"; then
	echo "FAIL: trap_panic" >&2
	exit 1
fi

SCRATCH="$(python3 - "$GDB_OUT" <<'PY'
import re, sys
text = open(sys.argv[1], errors="replace").read()
m = re.search(r"g_ulmk_board_hil_scratch>:\s*(0x[0-9a-fA-F]+)", text)
print(m.group(1) if m else "0")
PY
)"

echo "scratch=${SCRATCH}"
# 0x3 after board_services, or stress PASS marker 0x57E55, or e2e 0xE2EA
python3 - <<PY
s = int("${SCRATCH}", 0)
if s >= 3 or s in (0x57E55, 0xE2EA):
    print("PASS: timer path reached (scratch ok)")
    raise SystemExit(0)
print("FAIL: scratch too low — board_timer likely did not complete", file=__import__("sys").stderr)
raise SystemExit(1)
PY
