#!/usr/bin/env bash
set -euo pipefail
ROOT=/home/ulipe/fun/ulmk_boards/tc275_lite
ELF=/home/ulipe/fun/build/ulipe-tricore-tc275_lite/ulmk
source "$ROOT/scripts/aurix-env.sh"
source_aurix_env

pkill -9 -f '[o]penocd' 2>/dev/null || true
sleep 1
"$ULMK_AURIX_PREFIX/bin/openocd" \
  -s "$ULMK_AURIX_PREFIX/share/openocd/scripts" \
  -s "$ROOT/openocd" \
  -f "$ROOT/openocd/tc275_lite_hil.cfg" >/tmp/ocd.log 2>&1 &
sleep 3

docker run --rm --network host -v "$(dirname "$ELF"):/elf" ulipe-microkernel:dev \
  timeout 40 tricore-elf-gdb -batch /elf/ulmk \
  -ex 'set remotetimeout 20' \
  -ex 'target extended-remote :3333' \
  -ex 'monitor reset halt' \
  -ex 'break asclin_uart_init' \
  -ex 'continue' \
  -ex 'finish' \
  -ex 'x/1wx 0xF0000618' \
  -ex 'x/1wx 0xF000064C' \
  -ex 'x/1wx 0xF0000620' \
  -ex 'x/wx &g_ulmk_board_hil_scratch'
