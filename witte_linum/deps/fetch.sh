#!/usr/bin/env bash
# Fetch STM32 CMSIS + LL headers (no HAL objects linked).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

clone_tag() {
	local url="$1" dir="$2" tag="$3"
	if [[ -d "$dir/.git" ]]; then
		echo "ok: $dir"
		return 0
	fi
	rm -rf "$dir"
	git clone --depth 1 --branch "$tag" "$url" "$dir"
}

clone_tag https://github.com/STMicroelectronics/cmsis_device_h7.git \
	cmsis_device_h7 v1.10.7
clone_tag https://github.com/STMicroelectronics/cmsis_core.git \
	cmsis_core v5.9.0
clone_tag https://github.com/STMicroelectronics/stm32h7xx_hal_driver.git \
	stm32h7xx_hal_driver v1.11.6

if [[ ! -d rtt/.git ]]; then
	rm -rf rtt
	git clone --depth 1 https://github.com/SEGGERMicro/RTT.git rtt
fi

echo "deps ready under $ROOT"
