#!/usr/bin/env bash
set -euo pipefail
ELF=/home/ulipe/fun/build/ulipe-tricore-tc275_lite/ulmk
docker run --rm --network host -v "$(dirname "$ELF"):/elf" ulipe-microkernel:dev \
  timeout 15 tricore-elf-gdb -batch "/elf/$(basename "$ELF")" \
  -ex 'set remotetimeout 15' \
  -ex 'target extended-remote :3333' \
  -ex 'monitor reset halt' \
  -ex 'info register pc' \
  -ex 'break *0xa0000030' \
  -ex 'continue' \
  -ex 'info register pc' 2>&1
