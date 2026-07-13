#!/usr/bin/env bash
# HIL gpio_led_notify — flash and expect LED1 on via defer path (serial).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT}/scripts/hil-config.sh"

ELF="${1:-/home/ulipe/fun/build/ulipe-tricore-tc275_lite/ulmk}"
PATTERN="${HIL_GPIO_LED:-gpio_led_notify: LED1 on}"

exec "${ULMK_ROOT}/tools/hil/hil_serial_expect.sh" "$ELF" "$PATTERN" "${2:-30}"
