#!/usr/bin/env bash
# hil-config.sh — ESP32-P4 Function EV Board HIL defaults.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ULMK_ROOT="${ULMK_ROOT:-$(cd "${ROOT}/../../ulmk" 2>/dev/null && pwd || echo /home/ulipe/fun/ulmk)}"

export ULMK_HIL_CHIP="${ULMK_HIL_CHIP:-esp32p4}"
export ULMK_HIL_BAUD="${ULMK_HIL_BAUD:-115200}"
export ULMK_HIL_FLASH_BAUD="${ULMK_HIL_FLASH_BAUD:-460800}"

if [[ -z "${ULMK_HIL_SERIAL:-}" ]]; then
	# Prefer UART bridge that esptool can sync (Function EV: often ttyUSB1).
	for cand in /dev/ttyUSB1 /dev/ttyUSB0 /dev/ttyACM0; do
		if [[ -e "$cand" ]]; then
			export ULMK_HIL_SERIAL="$cand"
			break
		fi
	done
fi

export ULMK_HIL_SERIAL="${ULMK_HIL_SERIAL:-/dev/ttyUSB1}"
# Optional separate console port (USB-Serial/JTAG); default = flash port.
export ULMK_HIL_CONSOLE="${ULMK_HIL_CONSOLE:-$ULMK_HIL_SERIAL}"

# IDF / esptool
export IDF_PATH="${IDF_PATH:-${ESP_IDF_PATH:-/home/ulipe/fun/esp-idf}}"
export ESP_IDF_PATH="${ESP_IDF_PATH:-$IDF_PATH}"

if [[ -f "${IDF_PATH}/export.sh" ]]; then
	# shellcheck disable=SC1091
	source "${IDF_PATH}/export.sh" >/dev/null 2>&1 || true
fi

ESPTOOL="${ESPTOOL:-}"
if [[ -z "$ESPTOOL" ]]; then
	if command -v esptool.py >/dev/null 2>&1; then
		ESPTOOL="$(command -v esptool.py)"
	elif command -v esptool >/dev/null 2>&1; then
		ESPTOOL="$(command -v esptool)"
	elif [[ -x "${HOME}/.espressif/python_env/idf5.5_py3.10_env/bin/esptool.py" ]]; then
		ESPTOOL="${HOME}/.espressif/python_env/idf5.5_py3.10_env/bin/esptool.py"
	else
		ESPTOOL="python3 -m esptool"
	fi
fi
export ESPTOOL

export ULMK_BOARD_ROOT="$ROOT"
export ULMK_PREBUILT="${ROOT}/scripts/prebuilt"
