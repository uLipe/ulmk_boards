#!/usr/bin/env bash
# HIL: board_irq_attach_blinky — expect banner + heartbeat after flash.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
ELF="${1:-}"
# shellcheck source=/dev/null
source "${ROOT}/hil-config.sh"
exec "${ULMK_ROOT}/tools/hil/hil_serial_expect.sh" "$ELF" \
	'attach ok|heartbeat hits=' 25
