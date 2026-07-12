#!/usr/bin/env bash
# hil-serial-capture.sh — flash ELF, then capture /dev/ttyUSB0 until PATTERN
# or timeout.
#
# Serial opens AFTER flash+start, then the core is reset once more with the
# port already open so boot banners are not lost to the flash halt window.
#
# Usage: hil-serial-capture.sh <elf> <pattern> [timeout_s]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:?elf}"
PATTERN="${2:?regex pattern}"
TIMEOUT_S="${3:-30}"

# shellcheck source=/dev/null
source "${HOME}/.local/aurix/env.sh" 2>/dev/null || true
source "${ROOT}/scripts/aurix-env.sh" 2>/dev/null || true
source_aurix_env 2>/dev/null || true

pkill -9 -x openocd 2>/dev/null || true
fuser -k /dev/ttyUSB0 2>/dev/null || true
sleep 0.3

"$ROOT/scripts/flash.sh" "$ELF"

OPENOCD="${ULMK_AURIX_PREFIX}/bin/openocd"
OCD_SCRIPTS="${ULMK_AURIX_PREFIX}/share/openocd/scripts"
OCD_CFG="${ROOT}/openocd/tc275_lite_hil.cfg"

OUT="$(mktemp)"
python3 - "$OUT" "$PATTERN" "$TIMEOUT_S" <<'PY' &
import serial, time, sys, re

out, pat, to = sys.argv[1], sys.argv[2], float(sys.argv[3])
rx = re.compile(pat.encode() if isinstance(pat, str) else pat)
s = serial.Serial('/dev/ttyUSB0', 115200, timeout=0.2)
time.sleep(0.05)
s.reset_input_buffer()
buf = b''
t0 = time.time()
while time.time() - t0 < to:
	d = s.read(4096)
	if d:
		buf += d
	if rx.search(buf):
		time.sleep(0.5)
		buf += s.read(8192)
		break
open(out, 'wb').write(buf)
s.close()
print(f'captured {len(buf)} bytes -> {out}', file=sys.stderr)
PY
CAP=$!
sleep 0.2

# Re-enter userspace with UART listener already armed.
pkill -9 -x openocd 2>/dev/null || true
"$OPENOCD" -s "$OCD_SCRIPTS" -s "${ROOT}/openocd" -f "$OCD_CFG" \
	-c "gdb port disabled; init; reset halt; resume; shutdown" \
	>/dev/null 2>&1 || true

wait "$CAP"
echo "===== uart ====="
cat "$OUT"
rm -f "$OUT"
