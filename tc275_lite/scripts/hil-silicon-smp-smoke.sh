#!/usr/bin/env bash
# Shim → tools/hil/hil_ramlog_expect.sh (SMP OpenOCD: CPU0 + deferred CPU1)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "${ROOT}/hil-config.sh"
export ULMK_HIL_BREAK_DONE="${ULMK_HIL_BREAK_DONE:-silicon_smp_smoke_done}"
# CPU0-only cfg is enough for RAM-log HIL; dual-core GDB uses tc275_lite_hil_smp.cfg.
export ULMK_HIL_OCD_CFG="${ULMK_HIL_OCD_CFG_SMP:-${ULMK_HIL_OCD_BOARD_DIR}/tc275_lite_hil.cfg}"
exec "${ULMK_ROOT}/tools/hil/hil_ramlog_expect.sh" \
	"${1:-/home/ulipe/fun/build/ulipe-tricore-tc275_lite/ulmk}" \
	"${HIL_SMP_SMOKE_EXPECT:-SILICON_SMP_SMOKE: PASS}" \
	"SILICON_SMP_SMOKE: FAIL"
