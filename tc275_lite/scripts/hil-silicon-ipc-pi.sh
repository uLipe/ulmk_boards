#!/usr/bin/env bash
# Shim → tools/hil/hil_ramlog_expect.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "${ROOT}/hil-config.sh"
export ULMK_HIL_BREAK_DONE="${ULMK_HIL_BREAK_DONE:-silicon_ipc_pi_done}"
exec "${ULMK_ROOT}/tools/hil/hil_ramlog_expect.sh" \
	"${1:-/home/ulipe/fun/build/ulipe-tricore-tc275_lite/ulmk}" \
	"${HIL_IPC_PI_EXPECT:-SILICON_IPC_PI: PASS}" \
	"SILICON_IPC_PI: FAIL"
