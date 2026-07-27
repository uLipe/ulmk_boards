#!/usr/bin/env bash
# hil-silicon-common.sh — shared RTT HIL helper for silicon_* on witte_linum.
#
# Usage (from a shim):
#   source hil-silicon-common.sh
#   hil_silicon_rtt <expect_pass> [settle_secs] [elf]
set -euo pipefail

_HIL_SCRIPTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
source "${_HIL_SCRIPTS}/hil-config.sh"

hil_silicon_rtt()
{
	local expect="${1:?expect string}"
	local settle="${2:-5}"
	local elf="${3:-/home/ulipe/fun/build/ulipe-arm-witte_linum/ulmk}"

	exec "${_HIL_SCRIPTS}/hil-rtt-capture.sh" "$elf" "$expect" "$settle"
}
