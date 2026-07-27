#!/usr/bin/env bash
# HIL: board_blinky on LAUNCHXL-F29H85X (UART + visual LED4/LED5).
#
# Usage:
#   scripts/hil-blinky.sh [path/to/ulmk.out]
#
set -euo pipefail

BOARD_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:-/home/ulipe/fun/build/ulipe-c29-launchxl_f29h85x/ulmk}"
OUT="$ELF"
[[ -f "${ELF}.out" ]] && OUT="${ELF}.out"

export ULMK_HIL_CASE="${ULMK_HIL_CASE:-c29_blinky}"
export ULMK_HIL_RESET="${ULMK_HIL_RESET:-1}"

echo "HIL blinky: expect UART C29BLINKY_PASS; observe LED4/LED5 alternating"
bash "$BOARD_DIR/scripts/hil-load-uart.sh" "$OUT" 'C29BLINKY_PASS|c29_blinky:PASS|blinky' 40
