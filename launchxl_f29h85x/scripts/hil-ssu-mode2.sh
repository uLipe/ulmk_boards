#!/usr/bin/env bash
# HIL: flash SSUMODE2 SECCFG (NonMain) + c29_ssu_neg isolation probe.
#
# DANGER: programs NonMain SECCFG.  Invalid CRC/APR can brick the kit.
# Requires explicit ULMK_C29_SECCFG_COMMIT=1 (default refuses).
#
# Flash path uses loadti RAM warm-up + dss-flash-norest.js (see hil-flash-por).
# Set ULMK_C29_RAM_ELF to a RAM-profile ulmk (build without --flash).
# After program, TI requires a physical XRSn for SECCFG to take effect;
# JTAG reset alone leaves MODE_REG at 0x30 and may stick in BootROM.
#
# Usage:
#   ULMK_C29_SECCFG_COMMIT=1 ULMK_C29_RAM_ELF=<ram-ulmk> \
#     scripts/hil-ssu-mode2.sh <flash-ulmk>
#
set -euo pipefail
BOARD_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:?usage: ULMK_C29_SECCFG_COMMIT=1 $0 <ulmk.out>}"

if [[ "${ULMK_C29_SECCFG_COMMIT:-0}" != "1" ]]; then
	echo "error: refusing NonMain SECCFG flash without ULMK_C29_SECCFG_COMMIT=1" >&2
	echo "error: review docs/gates/c29f_gate_evidence.md Gate D before enabling" >&2
	exit 1
fi

export ULMK_HIL_CASE="${ULMK_HIL_CASE:-c29_ssu_mode2}"
export ULMK_C29_FLASH=1
export ULMK_C29_FLASH_COMMIT="${ULMK_C29_FLASH_COMMIT:-1}"
export ULMK_C29_SECCFG_COMMIT=1
export ULMK_C29_BANKMODE="${ULMK_C29_BANKMODE:-0}"
export ULMK_C29_LIFECYCLE="${ULMK_C29_LIFECYCLE:-HSFS}"

bash "$BOARD_DIR/scripts/hil-flash-por.sh" "$ELF" \
	'C29SSU_PASS|c29_ssu_neg:PASS|TRAP: killing|C29SSU_SKIP' 60
