#!/usr/bin/env bash
# hil-serial-capture.sh — flash then expect regex on UART
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT}/scripts/hil-config.sh"

ELF="${1:-}"
PATTERN="${2:-hello}"
TIMEOUT="${3:-20}"

if [[ -z "$ELF" || ! -f "$ELF" ]]; then
	echo "usage: $0 <ulmk.elf> <regex> [timeout_s]" >&2
	exit 1
fi

bash "${ROOT}/scripts/flash.sh" "$ELF"

python3 - "$ULMK_HIL_CONSOLE" "$ULMK_HIL_BAUD" "$PATTERN" "$TIMEOUT" <<'PY'
import re, sys, time, serial

port, baud_s, pattern, timeout_s = sys.argv[1:5]
baud = int(baud_s)
timeout = float(timeout_s)
rx = re.compile(pattern.encode("utf-8"))

ser = serial.Serial(port, baud, timeout=0.2)
ser.reset_input_buffer()
deadline = time.time() + timeout
buf = bytearray()
print(f"capture {port} @{baud} expect /{pattern}/ ({timeout}s)", flush=True)
while time.time() < deadline:
    chunk = ser.read(512)
    if chunk:
        sys.stdout.buffer.write(chunk)
        sys.stdout.flush()
        buf.extend(chunk)
        if rx.search(buf):
            print("\nHIL PASS", flush=True)
            ser.close()
            sys.exit(0)
ser.close()
print("\nHIL FAIL: pattern not seen", flush=True)
sys.exit(1)
PY
