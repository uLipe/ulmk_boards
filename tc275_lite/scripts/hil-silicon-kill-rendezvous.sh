#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "${ROOT}/hil-config.sh"
export ULMK_HIL_BREAK_DONE="${ULMK_HIL_BREAK_DONE:-silicon_kill_rendezvous_done}"
exec "${ULMK_ROOT}/tools/hil/hil_ramlog_expect.sh" \
	"${1:-/home/ulipe/fun/build/ulipe-tricore-tc275_lite/ulmk}" \
	"${HIL_KILL_RENDEZVOUS_EXPECT:-SILICON_KILL_RENDEZVOUS: PASS}" \
	"SILICON_KILL_RENDEZVOUS: FAIL"
