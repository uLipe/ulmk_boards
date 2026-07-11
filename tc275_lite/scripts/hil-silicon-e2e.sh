#!/usr/bin/env bash
# HIL silicon e2e — flash silicon_e2e firmware, dump RAM log for PASS sentinel.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# shellcheck source=/dev/null
source "$(dirname "$0")/aurix-env.sh"
source_aurix_env || exit 1

ELF="${1:-/home/ulipe/fun/build/ulipe-tricore-tc275_lite/ulmk}"
EXPECT="${HIL_E2E_EXPECT:-SILICON_E2E: PASS}"
GDB_PORT="${ULMK_OCD_GDB_PORT:-3333}"

if [[ ! -f "$ELF" ]]; then
	echo "ELF not found: ${ELF}" >&2
	exit 1
fi
if ! pgrep -f tas_server >/dev/null; then
	echo "tas_server not running" >&2
	exit 1
fi

echo "ELF:    $(basename "$ELF")"
echo "expect: ${EXPECT}"

"${ROOT}/scripts/flash.sh" "$ELF" >/dev/null

OCD="${ULMK_AURIX_PREFIX}/bin/openocd"
pkill -9 -f "${OCD}" 2>/dev/null || true
sleep 1
"$OCD" -s "${ULMK_AURIX_PREFIX}/share/openocd/scripts" -s "${ROOT}/openocd" \
	-f "${ROOT}/openocd/tc275_lite_hil.cfg" >/tmp/ulmk-ocd-e2e.log 2>&1 &
sleep 3

GDB_OUT=/tmp/ulmk-gdb-e2e.log
cleanup() { pkill -9 -f "${OCD}" 2>/dev/null || true; }
trap cleanup EXIT

# Root prints PASS then thread_exit.  Break on exit after begin.
docker run --rm --network host -v "$(dirname "$ELF"):/elf" ulipe-microkernel:dev \
	timeout 90 tricore-elf-gdb -batch "/elf/$(basename "$ELF")" \
	-ex "set remotetimeout 60" \
	-ex "target extended-remote :${GDB_PORT}" \
	-ex "monitor reset halt" \
	-ex "break ulmk_kern_trap_panic" \
	-ex "break silicon_e2e_done" \
	-ex "continue" \
	-ex "x/wx &g_ulmk_board_hil_scratch" \
	-ex "x/wx &g_ulmk_console_log_len" \
	-ex "x/2048cb &g_ulmk_console_log" \
	-ex "bt 4" >"$GDB_OUT" 2>&1 || true

echo "--- gdb ---"
/usr/bin/tail -80 "$GDB_OUT"
echo "--- end gdb ---"

DECODED="$(python3 - "$GDB_OUT" <<'PY'
import re, sys
text = open(sys.argv[1], errors="replace").read()
idx = text.find("g_ulmk_console_log>:")
if idx < 0:
    idx = text.find("g_ulmk_console_log")
chunk = text[idx:] if idx >= 0 else text
chars = []
for m in re.finditer(r"(-?\d+)\s+'((?:\\.|[^'\\])*)'", chunk):
    b = int(m.group(1)) & 0xFF
    if b == 0:
        break
    chars.append(chr(b))
    if len(chars) >= 2048:
        break
print("".join(chars))
PY
)"

echo "--- decoded log ---"
printf '%s\n' "$DECODED"
echo "--- end decoded ---"

if printf '%s' "$DECODED" | grep -qF "$EXPECT"; then
	echo "PASS: ${EXPECT}"
	exit 0
fi

if printf '%s' "$DECODED" | grep -qF "SILICON_E2E: FAIL"; then
	echo "FAIL: silicon e2e reported FAIL" >&2
	exit 1
fi

if grep -qE "ulmk_kern_trap_panic" "$GDB_OUT"; then
	echo "FAIL: trap_panic hit" >&2
	exit 1
fi

echo "FAIL: \"${EXPECT}\" not found" >&2
exit 1
