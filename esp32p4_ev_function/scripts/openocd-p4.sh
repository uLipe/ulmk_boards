#!/usr/bin/env bash
# openocd-p4.sh — ESP32-P4 built-in USB Serial/JTAG (not the OTG "USB Device" port).
#
# Board ports (esp-dev-kits Function EV):
#   USB Serial/JTAG  → OpenOCD + optional /dev/ttyACM* console  ← use THIS
#   USB 2.0 Type-C   → OTG HS "USB Device" (gadget) — NOT JTAG
#   USB Full-speed   → FS OTG / power
#
# Prerequisite: lsusb | grep -i 303a   (Espressif USB-JTAG)
set -euo pipefail

OPENOCD="${OPENOCD:-}"
if [[ -z "$OPENOCD" ]]; then
	OPENOCD=$(ls -d "$HOME"/.espressif/tools/openocd-esp32/*/openocd-esp32/bin/openocd 2>/dev/null | sort -V | tail -1 || true)
fi
if [[ -z "${OPENOCD}" || ! -x "${OPENOCD}" ]]; then
	if command -v openocd >/dev/null 2>&1; then
		OPENOCD=$(command -v openocd)
	fi
fi
if [[ -z "${OPENOCD}" || ! -x "${OPENOCD}" ]]; then
	echo "openocd-esp32 not found; install via IDF or set OPENOCD=" >&2
	exit 1
fi

SCRIPTS=$(dirname "$(dirname "$OPENOCD")")/share/openocd/scripts
CFG_BOARD=
for f in \
	"$SCRIPTS/board/esp32p4-builtin.cfg" \
	"$SCRIPTS/board/esp32p4-usb-jtag.cfg" \
	"$SCRIPTS/board/esp32p4-bridge.cfg"
do
	if [[ -f "$f" ]]; then
		CFG_BOARD=$f
		break
	fi
done

if [[ -z "$CFG_BOARD" ]]; then
	echo "No esp32p4 board cfg under $SCRIPTS/board" >&2
	ls "$SCRIPTS/board" | grep -i p4 || true
	exit 1
fi

if ! lsusb 2>/dev/null | grep -qiE '303a|Espressif'; then
	echo "WARNING: no Espressif USB device (303a). Plug USB Serial/JTAG port," >&2
	echo "         not the USB 2.0 OTG 'Device' Type-C." >&2
fi

echo "Using: $OPENOCD"
echo "Board: $CFG_BOARD"
exec "$OPENOCD" -s "$SCRIPTS" -f "$CFG_BOARD" "$@"
