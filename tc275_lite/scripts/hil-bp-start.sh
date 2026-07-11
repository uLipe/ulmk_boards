#!/usr/bin/env bash
set -euo pipefail
TARGET="${1:-ulmk_kern_start}"
TIMEOUT_S="${2:-60}"
pkill -9 -f 'aurix/bin/openocd' 2>/dev/null || true
sleep 2
/home/ulipe/.local/aurix/bin/openocd -d2 \
  -s /home/ulipe/.local/aurix/share/openocd/scripts \
  -s /home/ulipe/fun/ulmk_boards/tc275_lite/openocd \
  -f /home/ulipe/fun/ulmk_boards/tc275_lite/openocd/tc275_lite_hil.cfg \
  > /home/ulipe/fun/ulmk_boards/tc275_lite/scripts/ocd-bp.log 2>&1 &
sleep 4
docker run --rm --network host \
  -v /home/ulipe/fun/build/ulipe-tricore-tc275_lite:/elf \
  ulipe-microkernel:dev timeout "${TIMEOUT_S}" tricore-elf-gdb -batch /elf/ulmk \
  -ex "set remotetimeout ${TIMEOUT_S}" \
  -ex 'target extended-remote :3333' \
  -ex 'monitor reset halt' \
  -ex 'info register pc' \
  -ex "break ${TARGET}" \
  -ex 'break ulmk_kern_trap_panic' \
  -ex 'continue' \
  -ex 'info register pc' \
  -ex 'bt 4'
