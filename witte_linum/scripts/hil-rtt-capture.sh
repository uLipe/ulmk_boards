#!/usr/bin/env bash
# hil-rtt-capture.sh — flash + dump SEGGER RTT up-buffer; match regex.
#
# Uses JLinkExe savebin (reliable).  JLinkRTTLogger often misses the CB when
# it resets the target before SEGGER_RTT_Init().
#
# Usage: hil-rtt-capture.sh <elf> <regex> [settle_secs]
set -euo pipefail

SCRIPTS="$(cd "$(dirname "$0")" && pwd)"
ELF="${1:-}"
PATTERN="${2:-.}"
SETTLE_S="${3:-2}"

if [[ -z "$ELF" || ! -f "$ELF" ]]; then
	echo "usage: $0 <elf> <regex> [settle_secs]" >&2
	exit 1
fi

# shellcheck source=/dev/null
source "${SCRIPTS}/hil-config.sh"

OBJCOPY="${ULMK_OBJCOPY:-arm-none-eabi-objcopy}"
NM="${ULMK_NM:-arm-none-eabi-nm}"
BIN="$(mktemp "${TMPDIR:-/tmp}/ulmk-rtt-XXXXXX.bin")"
LOG="$(mktemp "${TMPDIR:-/tmp}/ulmk-rtt-XXXXXX.log")"
SCRIPT="$(mktemp "${TMPDIR:-/tmp}/ulmk-rtt-XXXXXX.jlink")"
cleanup() { rm -f "$BIN" "$LOG" "$SCRIPT" /tmp/ulmk-rtt-cb.bin /tmp/ulmk-rtt-pay.bin; }
trap cleanup EXIT

if [[ "${ULMK_HIL_SKIP_FLASH:-0}" != "1" ]]; then
	ULMK_PROBE=jlink "${SCRIPTS}/flash.sh" "$ELF"
fi

RTT_ADDR="$("$NM" "$ELF" | awk '/_SEGGER_RTT$/{print $1}')"
if [[ -z "$RTT_ADDR" ]]; then
	echo "error: _SEGGER_RTT not in ELF" >&2
	exit 1
fi
CB="0x${RTT_ADDR}"

# Run, settle, dump CB + payload.
cat >"$SCRIPT" <<EOF
si SWD
speed 4000
connect
r
g
sleep $((SETTLE_S * 1000))
halt
savebin /tmp/ulmk-rtt-cb.bin ${CB} 0x40
g
q
EOF
JLinkExe -device STM32H753ZI -if SWD -speed 4000 -autoconnect 1 \
	-CommanderScript "$SCRIPT" >/tmp/ulmk-rtt-jlink.log 2>&1

python3 - "$LOG" <<'PY'
import struct, subprocess, sys
out = sys.argv[1]
cb = open('/tmp/ulmk-rtt-cb.bin', 'rb').read()
if cb[:10] != b'SEGGER RTT':
    sys.stderr.write(f'RTT CB magic missing: {cb[:16]!r}\n')
    sys.exit(2)
# aUp[0] @ +0x18: sName, pBuffer, Size, WrOff, RdOff, Flags
pbuf, size, wroff = struct.unpack_from('<III', cb, 0x1C)
if size == 0:
    sys.stderr.write('RTT up-buffer size 0\n')
    open(out, 'wb').write(b'')
    sys.exit(3)
script = f"""si SWD
speed 4000
connect
halt
savebin /tmp/ulmk-rtt-pay.bin 0x{pbuf:08x} {size}
g
q
"""
open('/tmp/ulmk-rtt-pay.jlink', 'w').write(script)
subprocess.run(
    ['JLinkExe', '-device', 'STM32H753ZI', '-if', 'SWD', '-speed', '4000',
     '-autoconnect', '1', '-CommanderScript', '/tmp/ulmk-rtt-pay.jlink'],
    capture_output=True, check=False)
raw = open('/tmp/ulmk-rtt-pay.bin', 'rb').read()[:size]
wroff %= size if size else 0
if wroff == 0:
    pay = b''
elif not any(raw[wroff:]):
    pay = raw[:wroff]
else:
    pay = raw[wroff:] + raw[:wroff]
if not pay:
    sys.stderr.write('RTT up-buffer empty\n')
    open(out, 'wb').write(b'')
    sys.exit(3)
open(out, 'wb').write(pay)
print(f'RTT dump: {len(pay)} bytes (WrOff={wroff}, Size={size}, buf=0x{pbuf:08x})')
PY

echo "===== RTT capture ====="
cat "$LOG"
echo
echo "======================="

if grep -aE "$PATTERN" "$LOG" >/dev/null 2>&1; then
	echo "PASS: matched /$PATTERN/"
	exit 0
fi
echo "FAIL: no match for /$PATTERN/" >&2
exit 1
