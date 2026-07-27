#!/usr/bin/env bash
# HIL: flash-profile SMP — program Main flash (CPU1 + contiguous secondary
# stubs in FLASH_RP0), JTAG reset, expect ready mask 0x7.  BANKMODE0
# (reviewed); CPU1 copies stubs into LPA1/CPA0 before release.
set -euo pipefail
BOARD_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:-/home/ulipe/fun/build/ulipe-c29-launchxl_f29h85x/ulmk}"
OUT="$ELF"
[[ -f "${ELF}.out" ]] && OUT="${ELF}.out"

export ULMK_HIL_CASE="${ULMK_HIL_CASE:-c29_smp_flash}"
export ULMK_C29_FLASH=1
export ULMK_C29_FLASH_COMMIT="${ULMK_C29_FLASH_COMMIT:-1}"
export ULMK_C29_FLASH_SECONDARY=1
export ULMK_C29_BANKMODE="${ULMK_C29_BANKMODE:-0}"
export ULMK_C29_LIFECYCLE="${ULMK_C29_LIFECYCLE:-HSFS}"

bash "$BOARD_DIR/scripts/hil-flash-por.sh" "$OUT" \
	'C29SMP_PASS|c29_smp_smoke:PASS|CORE_READY mask=0x7|smp ready mask=0x7' 60
