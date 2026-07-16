#!/usr/bin/env bash
# tc275_lite/scripts/debug.sh — start OpenOCD GDB server (port 3333).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# shellcheck source=/dev/null
source "$(dirname "$0")/aurix-env.sh"
if ! source_aurix_env; then
	echo "env.sh not found — run: scripts/install-host-tools.sh" >&2
	exit 1
fi

OPENOCD="${ULMK_AURIX_PREFIX}/bin/openocd"
OCD_SCRIPTS="${ULMK_AURIX_PREFIX}/share/openocd/scripts"
BOARD_OCD="${ROOT}/openocd"
OCD_CFG="${ULMK_OCD_CFG:-${BOARD_OCD}/tc275_lite_hil.cfg}"

if [[ ! -x "$OPENOCD" ]]; then
	echo "openocd not found at ${OPENOCD}" >&2
	exit 1
fi

if ! pgrep -f tas_server >/dev/null 2>&1; then
	echo "warning: tas_server does not appear to be running." >&2
	echo "         start it first: ${ROOT}/scripts/start-tas.sh" >&2
fi

OCD_SEARCH=(-s "$OCD_SCRIPTS" -s "$BOARD_OCD")

# On exit (Ctrl-C / GDB detach), leave the core running standalone.  A debug
# session ends with the target halted; without this the board stays frozen at
# the last PC after the debugger is gone.  Mirror flash.sh's proven release:
# genuine "reset run" with the reset-end DAP pokes disabled (SW handles WDT /
# EndInit) so the next button/PORST boots cleanly.
release_on_exit() {
	pkill -9 -x openocd 2>/dev/null || true
	sleep 1
	"$OPENOCD" "${OCD_SEARCH[@]}" -f "${OCD_CFG}" \
		-c "gdb port disabled; init; targets tc27x.cpu0; tc27x.cpu0 configure -event reset-end {}; reset run; after 300; shutdown" \
		>/dev/null 2>&1 || true
	echo "debug: released target (reset run)"
}
trap release_on_exit EXIT

echo "openocd: ${OPENOCD}"
echo "config:  ${OCD_CFG}"
echo "GDB: tricore-elf-gdb -ex 'target remote :3333' <elf>"
echo "(on exit: automatic reset-run release)"
"$OPENOCD" "${OCD_SEARCH[@]}" -f "${OCD_CFG}"
