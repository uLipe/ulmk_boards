#!/usr/bin/env bash
# Smoke-check: after flash, CPU halts inside _start (OpenOCD telnet, no GDB).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# shellcheck source=/dev/null
source "$(dirname "$0")/aurix-env.sh"
source_aurix_env || {
	echo "env.sh not found" >&2
	exit 1
}

OCD="${ULMK_AURIX_PREFIX}/bin/openocd"
OCD_SEARCH=(-s "${ULMK_AURIX_PREFIX}/share/openocd/scripts" -s "${ROOT}/openocd")
TELNET_PORT="${ULMK_OCD_TELNET_PORT:-4444}"

if ! pgrep -f tas_server >/dev/null 2>&1; then
	echo "tas_server not running — start: ${ROOT}/scripts/start-tas.sh" >&2
	exit 1
fi

if ! pgrep -f '[o]penocd' >/dev/null 2>&1; then
	echo "starting OpenOCD (background)..."
	"$OCD" "${OCD_SEARCH[@]}" -f "${ROOT}/openocd/tc275_lite.cfg" >/tmp/ulmk-ocd.log 2>&1 &
	sleep 1
fi

pc="$(printf 'reset halt\nreg PC\n' | nc -w 5 127.0.0.1 "${TELNET_PORT}" 2>/dev/null \
	| strings | grep -oE 'PC \(/32\): 0x[0-9a-fA-F]+' | awk '{print $3}')"

if [[ -z "$pc" ]]; then
	echo "failed to read PC via telnet :${TELNET_PORT}" >&2
	echo "no PC line in telnet response" >&2
	exit 1
fi

pc_val=$((pc))
start=0xA0000010
end=0xA00000D4

if (( pc_val >= start && pc_val <= end )); then
	echo "OK: PC=${pc} (inside _start @ 0xA0000030)"
	exit 0
fi

echo "FAIL: PC=${pc} not in _start [0xA0000010, 0xA00000D4]" >&2
exit 1
