#!/usr/bin/env bash
set -euo pipefail
pkill -9 -f 'aurix/bin/openocd' 2>/dev/null || true
sleep 2
/home/ulipe/.local/aurix/bin/openocd -d2 \
  -s /home/ulipe/.local/aurix/share/openocd/scripts \
  -s /home/ulipe/fun/ulmk_boards/tc275_lite/openocd \
  -f /home/ulipe/fun/ulmk_boards/tc275_lite/openocd/tc275_lite_hil.cfg \
  > /home/ulipe/fun/ulmk_boards/tc275_lite/scripts/ocd-root.log 2>&1 &
sleep 4
docker run --rm --network host \
  -v /home/ulipe/fun/build/ulipe-tricore-tc275_lite:/elf \
  ulipe-microkernel:dev timeout 60 tricore-elf-gdb -batch /elf/ulmk \
  -ex 'set remotetimeout 60' \
  -ex 'target extended-remote :3333' \
  -ex 'monitor reset halt' \
  -ex 'break ulmk_kern_trap_panic' \
  -ex 'break ulmk_root_thread' \
  -ex 'continue' \
  -ex 'info register pc' \
  -ex 'bt 4' > /home/ulipe/fun/ulmk_boards/tc275_lite/scripts/gdb-root.log 2>&1 || true
cat /home/ulipe/fun/ulmk_boards/tc275_lite/scripts/gdb-root.log
