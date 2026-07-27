#!/usr/bin/env bash
# witte_linum hil-config.sh — board HIL env for tools/hil/*
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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
export ULMK_HIL_SERIAL="${ULMK_HIL_SERIAL:-/dev/ttyACM0}"
export ULMK_HIL_SERIAL_BAUD="${ULMK_HIL_SERIAL_BAUD:-921600}"
export ULMK_PROBE="${ULMK_PROBE:-jlink}"
