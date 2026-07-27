#!/usr/bin/env bash
# Shim → tools/hil/hil_serial_expect.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "${ROOT}/hil-config.sh"
exec "${ULMK_ROOT}/tools/hil/hil_serial_expect.sh" "$@"
