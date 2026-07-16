#!/usr/bin/env bash
# tc275_lite/scripts/hil-release-run.sh
#
# Unstick a Lite Kit left halted at STAD by sticky HARR / leftover TRn.
# Hot-attach (no PORST), clear triggers + HARR, resume, exit.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# shellcheck source=/dev/null
source "$(dirname "$0")/aurix-env.sh"
source_aurix_env || { echo "env.sh not found" >&2; exit 1; }

if ! pgrep -f tas_server >/dev/null 2>&1; then
	echo "tas_server not running" >&2
	exit 1
fi

OCD="${ULMK_AURIX_PREFIX}/bin/openocd"
OCD_SEARCH=(-s "${ULMK_AURIX_PREFIX}/share/openocd/scripts" -s "${ROOT}/openocd")
# Use hil.cfg (has Tcl helpers); override reset so we do not PORST.
OCD_CFG="${ROOT}/openocd/tc275_lite_hil.cfg"

pkill -9 -x openocd 2>/dev/null || true
sleep 1

out="$("$OCD" "${OCD_SEARCH[@]}" -f "${OCD_CFG}" \
	-c "reset_config none separate" \
	-c "init" \
	-c "targets tc27x.cpu0" \
	-c "echo {=== before ===}" \
	-c "mdw 0xF881FE08" \
	-c "mdw 0xF881FD00" \
	-c "mdw 0xF0000480" \
	-c "mdw 0xF881F000 2" \
	-c "ulmk_release_run" \
	-c "resume" \
	-c "after 500" \
	-c "echo {=== after 500ms (halt to sample) ===}" \
	-c "halt" \
	-c "mdw 0xF881FE08" \
	-c "mdw 0xF881FD00" \
	-c "mdw 0xF0000480" \
	-c "mdw 0xF0036104" \
	-c "mdw 0xF00360F4" \
	-c "resume" \
	-c "shutdown" 2>&1)" || true

echo "$out"

pc="$(echo "$out" | sed -n 's/^0xf881fe08: //p' | awk 'NR==2 {print $1}')"
wdt="$(echo "$out" | sed -n 's/^0xf0036104: //p' | awk '{print $1}' | head -1)"
ostate="$(echo "$out" | sed -n 's/^0xf0000480: //p' | awk 'NR==2 {print $1}')"
echo
echo "post PC=${pc:-?}  WDTCPU0_CON1=${wdt:-?}  OSTATE=${ostate:-?}"
if [[ -n "${ostate:-}" ]]; then
	harr=$(( (16#${ostate#0x} >> 8) & 1 ))
	echo "HARR=${harr}"
	if [[ "$harr" -ne 0 ]]; then
		echo "FAIL: HARR still set after clear" >&2
		exit 1
	fi
fi
if [[ -n "${pc:-}" && "${pc}" != "a0000020" && "${pc}" != "0xa0000020" ]]; then
	echo "PASS: PC advanced past _start"
	exit 0
fi
if [[ -n "${wdt:-}" && "$((16#${wdt#0x} & 8))" -ne 0 ]]; then
	echo "PASS: WDT DR=1 (early disable ran)"
	exit 0
fi
echo "CHECK: PC still at entry or WDT still armed — see dump above" >&2
exit 1
