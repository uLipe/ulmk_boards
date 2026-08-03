#!/usr/bin/env bash
# Shim → hil-rtt-capture.sh (silicon device_manager)
set -euo pipefail
SCRIPTS="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "${SCRIPTS}/hil-config.sh"
exec "${SCRIPTS}/hil-rtt-capture.sh" \
	"${1:-/home/ulipe/fun/build/ulipe-arm-witte_linum/ulmk}" \
	"SILICON_DEVICE_MANAGER: PASS" \
	15
