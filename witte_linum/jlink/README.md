# J-Link notes for Witte-Linum (STM32H753ZI)
#
# Flash:
#   ULMK_PROBE=jlink ./scripts/flash.sh /path/to/ulmk.elf
#
# GDB server:
#   ULMK_PROBE=jlink ./scripts/debug.sh
#   arm-none-eabi-gdb -ex 'target remote :3333' ulmk.elf
#
# Device name: STM32H753ZI
