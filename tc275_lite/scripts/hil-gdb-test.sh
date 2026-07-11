#!/usr/bin/env bash
set -x
pkill -9 -f '/home/ulipe/.local/aurix/bin/openocd' 2>/dev/null || true
sleep 2
/home/ulipe/.local/aurix/bin/openocd -d2 \
  -s /home/ulipe/.local/aurix/share/openocd/scripts \
  -s /home/ulipe/fun/ulmk_boards/tc275_lite/openocd \
  -f /home/ulipe/fun/ulmk_boards/tc275_lite/openocd/tc275_lite_hil.cfg \
  > /home/ulipe/fun/ulmk_boards/tc275_lite/scripts/ocd-dbg.log 2>&1 &
sleep 4
tail -8 /home/ulipe/fun/ulmk_boards/tc275_lite/scripts/ocd-dbg.log
docker run --rm --network host \
  -v /home/ulipe/fun/build/ulipe-tricore-tc275_lite:/elf \
  ulipe-microkernel:dev timeout 40 tricore-elf-gdb -batch /elf/ulmk \
  -ex 'set remotetimeout 40' \
  -ex 'target extended-remote :3333' \
  -ex 'monitor reset halt' \
  -ex 'info register pc' \
  -ex 'break ulmk_kern_start' \
  -ex 'break ulmk_root_thread' \
  -ex 'continue' \
  -ex 'info register pc' \
  -ex 'bt 3' 2>&1 | tee /home/ulipe/fun/ulmk_boards/tc275_lite/scripts/gdb-boot.log
echo GDB_EXIT:$?
grep -iE 'breakpoint|halt|TR_EVT|hw breakpoint|Target.*halt|resumed' /home/ulipe/fun/ulmk_boards/tc275_lite/scripts/ocd-dbg.log | tail -30
