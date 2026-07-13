#!/usr/bin/env bash
# HIL board_i2c_scanner — flash and expect scan grid / found line on serial.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT}/scripts/hil-config.sh"

ELF="${1:-/home/ulipe/fun/build/ulipe-tricore-tc275_lite/ulmk}"
# Scan completes with "found N device(s)" even when the bus is empty (all --/TO).
PATTERN="${HIL_I2C_EXPECT:-found [0-9]+ device}"

exec "${ULMK_ROOT}/tools/hil/hil_serial_expect.sh" "$ELF" "$PATTERN" "${2:-60}"
