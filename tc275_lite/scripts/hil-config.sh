#!/usr/bin/env bash
# tc275_lite hil-config.sh — board-specific HIL environment for tools/hil/*
#
# Source from board hil shims.  flash.sh / debug.sh / OpenOCD stay here.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=/dev/null
source "${SCRIPTS}/aurix-env.sh"
source_aurix_env || return 1

# Kernel repo (tools/hil). Override with ULMK_ROOT if layout differs.
if [[ -z "${ULMK_ROOT:-}" ]]; then
	if [[ -d "${ROOT}/../../ulmk/tools/hil" ]]; then
		ULMK_ROOT="$(cd "${ROOT}/../../ulmk" && pwd)"
	elif [[ -d "${ROOT}/../ulmk/tools/hil" ]]; then
		ULMK_ROOT="$(cd "${ROOT}/../ulmk" && pwd)"
	else
		echo "hil-config: set ULMK_ROOT to the ulmk kernel checkout" >&2
		return 1
	fi
fi

export ULMK_ROOT
export ULMK_HIL_FLASH="${SCRIPTS}/flash.sh"
export ULMK_HIL_OCD="${ULMK_AURIX_PREFIX}/bin/openocd"
export ULMK_HIL_OCD_SCRIPTS="${ULMK_AURIX_PREFIX}/share/openocd/scripts"
export ULMK_HIL_OCD_BOARD_DIR="${ROOT}/openocd"
export ULMK_HIL_OCD_CFG="${ROOT}/openocd/tc275_lite_hil.cfg"
export ULMK_HIL_GDB_PORT="${ULMK_OCD_GDB_PORT:-3333}"
export ULMK_HIL_GDB_IMAGE="${ULMK_HIL_GDB_IMAGE:-ulipe-microkernel:dev}"
export ULMK_HIL_SERIAL="${ULMK_HIL_SERIAL:-/dev/ttyUSB1}"
export ULMK_HIL_SERIAL_BAUD="${ULMK_HIL_SERIAL_BAUD:-115200}"
export ULMK_HIL_REQUIRE_TAS="${ULMK_HIL_REQUIRE_TAS:-1}"
