#!/usr/bin/env bash
# Host build for ESP32-P4 (riscv32-esp-elf + ESP-IDF headers). Not QEMU.
set -euo pipefail

BOARD_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ULMK_ROOT="${ULMK_ROOT:-/home/ulipe/fun/ulmk}"
BUILD_ROOT="${BUILD_ROOT:-/home/ulipe/fun/build}"
BUILD_DIR="${BUILD_DIR:-${BUILD_ROOT}/ulipe-riscv-esp32p4_ev_function}"
IDF_PATH="${IDF_PATH:-${ESP_IDF_PATH:-/home/ulipe/fun/esp-idf}}"
export IDF_PATH ESP_IDF_PATH="$IDF_PATH"

# Espressif toolchain on PATH
ESP_GCC_BIN="$(ls -d "${HOME}/.espressif/tools/riscv32-esp-elf/"*/riscv32-esp-elf/bin 2>/dev/null | sort | tail -1 || true)"
if [[ -n "$ESP_GCC_BIN" ]]; then
	export PATH="${ESP_GCC_BIN}:$PATH"
fi

if [[ -f "${IDF_PATH}/export.sh" ]]; then
	# shellcheck disable=SC1091
	set +u
	source "${IDF_PATH}/export.sh"
	set -u
fi

CLEAN=0
COMPONENT_FLAGS=()
while [[ $# -gt 0 ]]; do
	case "$1" in
	--clean) CLEAN=1; shift ;;
	--component)
		COMPONENT_FLAGS+=("-DULMK_COMPONENT_$2=ON")
		shift 2
		;;
	--no-components)
		COMPONENT_FLAGS+=("-DULMK_NO_DEFAULT_COMPONENTS=ON")
		shift
		;;
	*)
		echo "unknown arg: $1" >&2
		exit 1
		;;
	esac
done

if [[ "$CLEAN" -eq 1 ]]; then
	rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

cmake -S "$ULMK_ROOT" -B "$BUILD_DIR" \
	-DCMAKE_TOOLCHAIN_FILE="${ULMK_ROOT}/cmake/toolchain-riscv-esp32p4.cmake" \
	-DULMK_CHIP_DIR="${BOARD_DIR}" \
	-GNinja \
	--no-warn-unused-cli \
	"${COMPONENT_FLAGS[@]+"${COMPONENT_FLAGS[@]}"}"

ninja -C "$BUILD_DIR"
echo "Build OK → ${BUILD_DIR}/ulmk"
