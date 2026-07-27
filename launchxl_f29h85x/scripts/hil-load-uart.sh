#!/usr/bin/env bash
# HIL: load ulmk.out to LAUNCHXL-F29H85X via loadti and capture UART if00.
#
# Usage:
#   scripts/hil-load-uart.sh [path/to/ulmk.out] [expect_regex] [timeout_s]
#
set -euo pipefail

BOARD_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:-}"
EXPECT="${2:-c29_banner|alive|ulmk:}"
TIMEOUT="${3:-20}"
BUILD_ID="${ULMK_HIL_BUILD_ID:-unknown}"
CASE_NAME="${ULMK_HIL_CASE:-hil-load-uart}"

TI_ROOT="${TI_INSTALL_ROOT:-/home/ulipe/ti}"
CCS_ROOT="${TI_CCS_ROOT:-$TI_ROOT/ccs2040/ccs}"
LOADTI="$CCS_ROOT/ccs_base/scripting/examples/loadti/loadti.sh"
DSS="$CCS_ROOT/ccs_base/scripting/bin/dss.sh"
DSS_RAMINIT="$BOARD_DIR/scripts/dss-raminit.js"
XDSRESET="$CCS_ROOT/ccs_base/common/uscif/xds110/xds110reset"
CCXML="${ULMK_HIL_CCXML:-$BOARD_DIR/targetconfigs/F29H850TU9.ccxml}"
LOCK_DIR="${ULMK_HIL_LOCK_DIR:-/tmp/ulmk-c29-hil.lock}"

SERIAL_BY_ID="/dev/serial/by-id"
SERIAL=""
PROBE_SERIAL="${ULMK_HIL_PROBE_SERIAL:-CL850001}"

if [[ -n "${ULMK_HIL_SERIAL:-}" ]]; then
	SERIAL="$ULMK_HIL_SERIAL"
else
	for cand in "$SERIAL_BY_ID"/usb-Texas_Instruments_XDS110*"${PROBE_SERIAL}"-if00 \
		    "$SERIAL_BY_ID"/usb-Texas_Instruments_XDS110*-if00; do
		if [[ -e "$cand" ]]; then
			SERIAL="$(readlink -f "$cand")"
			break
		fi
	done
fi

die() { echo "HIL FAIL [$CASE_NAME/$BUILD_ID]: $*" >&2; exit 1; }

if [[ -z "$ELF" || ! -f "$ELF" ]]; then
	echo "usage: $0 <ulmk.out> [expect_regex] [timeout_s]" >&2
	exit 2
fi
[[ -f "$LOADTI" ]] || die "loadti not found at $LOADTI"
[[ -f "$CCXML" ]] || die "ccxml not found: $CCXML"
[[ -n "$SERIAL" && -e "$SERIAL" ]] || die "UART serial not found (set ULMK_HIL_SERIAL)"

# Reject auxiliary XDS110 port (if03) — application UART is if00 only.
if [[ "$SERIAL" == *if03* ]]; then
	die "refusing auxiliary UART if03; use if00"
fi

# Exclusive board lock
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
	die "board lock busy ($LOCK_DIR)"
fi
cleanup_lock() { rmdir "$LOCK_DIR" 2>/dev/null || true; }
trap cleanup_lock EXIT

LOG="$(mktemp /tmp/ulmk-c29-hil.XXXXXX.log)"
cleanup() { rm -f "$LOG"; cleanup_lock; }
trap cleanup EXIT

echo "HIL CASE=$CASE_NAME BUILD=$BUILD_ID CORE_MASK=${ULMK_HIL_CORE_MASK:-0x1}"
echo "HIL: load $ELF via $CCXML"
echo "HIL: UART $SERIAL expect /$EXPECT/ timeout ${TIMEOUT}s"

# Optional soft recover before load
if [[ "${ULMK_HIL_RESET:-0}" == "1" && -x "$XDSRESET" ]]; then
	"$XDSRESET" || true
	sleep 1
fi

# SMP / secondary stubs live in LPA1+CPA0; loadti's OnPreFileLoaded only
# ECC-inits M0.  GEL ram_init() brings up the rest before the program load.
if [[ "${ULMK_HIL_RAMINIT:-0}" == "1" ]]; then
	[[ -f "$DSS" && -f "$DSS_RAMINIT" ]] || die "dss-raminit missing ($DSS / $DSS_RAMINIT)"
	echo "HIL: DSS GEL ram_init (LPA/CPA/LDA ECC)"
	set +e
	timeout --kill-after=10 90 bash "$DSS" "$DSS_RAMINIT" "$CCXML"
	RI_RC=$?
	set -e
	echo "HIL: raminit_rc=$RI_RC"
	[[ "$RI_RC" == "0" ]] || die "dss-raminit failed rc=$RI_RC"
fi

stty -F "$SERIAL" 115200 cs8 -cstopb -parenb raw -echo || true

# Capture after load starts; briefly pre-open to settle TTY.
timeout --kill-after=3 "$TIMEOUT" cat "$SERIAL" >"$LOG" &
CAP_PID=$!
sleep 0.2

set +e
timeout --kill-after=5 90 bash "$LOADTI" -c "$CCXML" -a -r "$ELF"
LOAD_RC=$?
set -e

wait "$CAP_PID" || true

echo "---- UART capture ----"
cat "$LOG"
echo "----------------------"
echo "HIL: load_rc=$LOAD_RC"

if ! grep -Eiq "$EXPECT" "$LOG"; then
	die "pattern /$EXPECT/ not seen (load_rc=$LOAD_RC)"
fi
echo "HIL PASS [$CASE_NAME/$BUILD_ID]"
exit 0
