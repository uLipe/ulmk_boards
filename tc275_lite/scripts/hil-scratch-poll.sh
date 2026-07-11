#!/usr/bin/env bash
# Poll g_ulmk_board_hil_scratch after free-run (expect >= 4 after console init).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:-/home/ulipe/fun/build/ulipe-tricore-tc275_lite/ulmk}"
WAIT_S="${HIL_SCRATCH_WAIT_S:-5}"
GDB_PORT="${ULMK_OCD_GDB_PORT:-3333}"

source "$(dirname "$0")/aurix-env.sh"
source_aurix_env || exit 1

ADDR=$(docker run --rm -v "$(dirname "$ELF"):/elf" ulipe-microkernel:dev \
	tricore-elf-nm -n "/elf/$(basename "$ELF")" \
	| awk '/ g_ulmk_board_hil_scratch$/ {print $1}')
echo "scratch @ 0x${ADDR}"

pkill -9 -f '[o]penocd' 2>/dev/null || true
sleep 1
"$ULMK_AURIX_PREFIX/bin/openocd" \
	-s "$ULMK_AURIX_PREFIX/share/openocd/scripts" \
	-s "$ROOT/openocd" \
	-f "$ROOT/openocd/tc275_lite_hil.cfg" >/tmp/ocd-scratch.log 2>&1 &
sleep 3

docker run --rm --network host -v "$(dirname "$ELF"):/elf" ulipe-microkernel:dev \
	timeout 40 tricore-elf-gdb -batch "/elf/$(basename "$ELF")" \
	-ex 'set remotetimeout 20' \
	-ex "target extended-remote :${GDB_PORT}" \
	-ex 'monitor reset halt' \
	-ex 'break ulmk_kern_trap_panic' \
	-ex 'break console_server' \
	-ex 'continue' \
	-ex 'info register pc' \
	-ex 'bt 8' \
	-ex "x/wx 0x${ADDR}" 2>&1 | tee /tmp/gdb-scratch.log | tail -25

echo "---"
grep -E 'Breakpoint|scratch|pc |PASS|FAIL|trap_panic|console_server|asclin' /tmp/gdb-scratch.log || true
