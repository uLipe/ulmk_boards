#!/usr/bin/env bash
set -euo pipefail
pkill -9 -f 'aurix/bin/openocd' 2>/dev/null || true
sleep 2
/home/ulipe/.local/aurix/bin/openocd -s /home/ulipe/.local/aurix/share/openocd/scripts \
  -s /home/ulipe/fun/ulmk_boards/tc275_lite/openocd \
  -f /home/ulipe/fun/ulmk_boards/tc275_lite/openocd/tc275_lite_hil.cfg \
  > /home/ulipe/fun/ulmk_boards/tc275_lite/scripts/ocd-boot.log 2>&1 &
sleep 4
docker run --rm --network host \
  -v /home/ulipe/fun/build/ulipe-tricore-tc275_lite:/elf \
  ulipe-microkernel:dev timeout 45 tricore-elf-gdb -batch /elf/ulmk \
  -ex 'set remotetimeout 45' \
  -ex 'target extended-remote :3333' \
  -ex 'monitor reset halt' \
  -ex 'break ulmk_kern_start' \
  -ex 'break ulmk_board_init' \
  -ex 'break ulmk_arch_init' \
  -ex 'break ulmk_kern_main' \
  -ex 'break ulmk_root_thread' \
  -ex 'break ulmk_kern_trap_panic' \
  -ex 'continue' \
  -ex 'info register pc' \
  -ex 'continue' \
  -ex 'info register pc' \
  -ex 'continue' \
  -ex 'info register pc' \
  -ex 'continue' \
  -ex 'info register pc' \
  -ex 'continue' \
  -ex 'info register pc' \
  -ex 'continue' \
  -ex 'info register pc'
