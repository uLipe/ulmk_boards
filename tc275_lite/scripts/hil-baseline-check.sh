#!/usr/bin/env bash
# Silicon baseline — root_thread (JTAG) + userspace console (RAM log).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:-/home/ulipe/fun/build/ulipe-tricore-tc275_lite/ulmk}"

echo "=== phase 1: root_thread (JTAG) ==="
"${ROOT}/scripts/hil-boot-check.sh" "$ELF"

echo ""
echo "=== phase 2: console output (RAM log via JTAG) ==="
HIL_CONSOLE_EXPECT="${HIL_CONSOLE_EXPECT:-SILICON_BASELINE: PASS}" \
	"${ROOT}/scripts/hil-console-check.sh" "$ELF"

echo ""
echo "PASS: silicon baseline (root_thread + console)"
