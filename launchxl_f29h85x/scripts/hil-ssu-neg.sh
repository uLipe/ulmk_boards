#!/usr/bin/env bash
# HIL: c29_ssu_neg — PASS under SSUMODE2 kill, or SKIP in MODE1 bring-up.
set -euo pipefail
BOARD_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:-/home/ulipe/fun/build/ulipe-c29-launchxl_f29h85x/ulmk}"
OUT="$ELF"; [[ -f "${ELF}.out" ]] && OUT="${ELF}.out"
export ULMK_HIL_CASE="${ULMK_HIL_CASE:-c29_ssu_neg}"
export ULMK_HIL_RESET="${ULMK_HIL_RESET:-1}"
bash "$BOARD_DIR/scripts/hil-load-uart.sh" "$OUT" \
	'C29SSU_SKIP|c29_ssu_neg:SKIP|c29_ssu_neg:PASS|TRAP: killing' 40
