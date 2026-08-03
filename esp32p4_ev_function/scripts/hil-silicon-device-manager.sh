#!/usr/bin/env bash
# hil-silicon-device-manager.sh — flash + expect SILICON_DEVICE_MANAGER: PASS
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT}/scripts/hil-config.sh"
ELF="${1:-/home/ulipe/fun/build/ulipe-riscv-esp32p4_ev_function/ulmk}"
exec "${ROOT}/scripts/hil-serial-capture.sh" "$ELF" \
	'SILICON_DEVICE_MANAGER: PASS' 40
