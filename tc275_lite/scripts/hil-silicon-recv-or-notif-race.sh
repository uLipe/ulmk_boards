#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "${ROOT}/hil-config.sh"
export ULMK_HIL_BREAK_DONE="${ULMK_HIL_BREAK_DONE:-silicon_recv_or_notif_race_done}"
exec "${ULMK_ROOT}/tools/hil/hil_ramlog_expect.sh" \
	"${1:-/home/ulipe/fun/build/ulipe-tricore-tc275_lite/ulmk}" \
	"${HIL_RECV_OR_NOTIF_RACE_EXPECT:-SILICON_RECV_OR_NOTIF_RACE: PASS}" \
	"SILICON_RECV_OR_NOTIF_RACE: FAIL"
