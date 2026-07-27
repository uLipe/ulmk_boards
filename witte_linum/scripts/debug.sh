#!/usr/bin/env bash
# witte_linum/scripts/debug.sh — OpenOCD or J-Link GDB server.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROBE="${ULMK_PROBE:-jlink}"
GDB_PORT="${ULMK_GDB_PORT:-3333}"

case "$PROBE" in
stlink|openocd)
	OPENOCD="${ULMK_OPENOCD:-openocd}"
	OCD_CFG="${ROOT}/openocd/witte_linum.cfg"
	echo "GDB: arm-none-eabi-gdb -ex 'target remote :${GDB_PORT}' <elf>"
	exec "$OPENOCD" -f "$OCD_CFG" \
		-c "gdb_port ${GDB_PORT}" \
		-c "tcl_port disabled" \
		-c "telnet_port disabled"
	;;
jlink)
	JLINK_GDB="${ULMK_JLINK_GDB:-JLinkGDBServer}"
	echo "GDB: arm-none-eabi-gdb -ex 'target remote :${GDB_PORT}' <elf>"
	echo "RTT: JLinkRTTClient  (or ./scripts/hil-rtt-capture.sh)"
	exec "$JLINK_GDB" -device STM32H753ZI -if SWD -speed 4000 \
		-port "$GDB_PORT"
	;;
*)
	echo "unknown ULMK_PROBE=${PROBE}" >&2
	exit 1
	;;
esac
