#!/usr/bin/env bash
# hil-silicon-smp-smoke.sh — flash + expect SILICON_SMP_SMOKE: PASS on UART
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT}/scripts/hil-config.sh"
ELF="${1:-/home/ulipe/fun/build/ulipe-riscv-esp32p4_ev_function/ulmk}"
exec "${ROOT}/scripts/hil-serial-capture.sh" "$ELF" \
	'SILICON_SMP_SMOKE: PASS' 30
