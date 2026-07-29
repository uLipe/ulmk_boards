#!/usr/bin/env bash
# monitor.sh — serial console
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT}/scripts/hil-config.sh"

if command -v idf_monitor.py >/dev/null 2>&1; then
	exec idf_monitor.py -p "${ULMK_HIL_SERIAL}" -b "${ULMK_HIL_BAUD}"
fi
exec python3 -m serial.tools.miniterm "${ULMK_HIL_SERIAL}" "${ULMK_HIL_BAUD}"
