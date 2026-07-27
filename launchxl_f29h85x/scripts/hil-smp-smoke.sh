#!/usr/bin/env bash
# HIL: c29_smp_smoke — expect ready mask 0x7 + PASS.
set -euo pipefail
BOARD_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:-/home/ulipe/fun/build/ulipe-c29-launchxl_f29h85x/ulmk}"
OUT="$ELF"; [[ -f "${ELF}.out" ]] && OUT="${ELF}.out"
export ULMK_HIL_CASE="${ULMK_HIL_CASE:-c29_smp_smoke}"
export ULMK_HIL_RESET="${ULMK_HIL_RESET:-1}"
export ULMK_HIL_RAMINIT="${ULMK_HIL_RAMINIT:-1}"
bash "$BOARD_DIR/scripts/hil-load-uart.sh" "$OUT" \
	'C29SMP_PASS|c29_smp_smoke:PASS|CORE_READY mask=0x7' 60
