#!/usr/bin/env bash
set -euo pipefail
LOG=/home/ulipe/fun/ulmk_boards/tc275_lite/scripts/hil-clean-test.log
: > "$LOG"

pkill -9 -f 'aurix/bin/openocd' 2>/dev/null || true
sleep 2

/home/ulipe/.local/aurix/bin/openocd -d3 \
  -s /home/ulipe/.local/aurix/share/openocd/scripts \
  -s /home/ulipe/fun/ulmk_boards/tc275_lite/openocd \
  -f /home/ulipe/fun/ulmk_boards/tc275_lite/openocd/tc275_lite_hil.cfg \
  >> "$LOG" 2>&1 &
sleep 5

docker run --rm --network host \
  -v /home/ulipe/fun/build/ulipe-tricore-tc275_lite:/elf \
  ulipe-microkernel:dev timeout 30 tricore-elf-gdb -batch /elf/ulmk \
  -ex 'set remotetimeout 30' \
  -ex 'target extended-remote :3333' \
  -ex 'monitor reset halt' \
  -ex 'info register pc' \
  -ex 'monitor mdw 0xF881F000 4' \
  -ex 'break ulmk_root_thread' \
  -ex 'continue' \
  -ex 'info register pc' >> /home/ulipe/fun/ulmk_boards/tc275_lite/scripts/gdb-clean.log 2>&1 || true

echo "=== GDB ===" >> "$LOG"
cat /home/ulipe/fun/ulmk_boards/tc275_lite/scripts/gdb-clean.log >> "$LOG"
echo "=== OCD tail ===" >> "$LOG"
tail -40 "$LOG" >> "$LOG"
