#!/usr/bin/env bash
# Flash + POR HIL helper for LAUNCHXL-F29H85X.
#
# Default (RAM profile): loadti into LPA/LDA and run (delegates to
# hil-load-uart.sh).
#
# Flash profile (ULMK_C29_FLASH=1): package a signed (HSFS dev-key) flash
# image, program Main flash headless via DSLite, issue a JTAG system reset
# (dss-reset-run.js — no ELF reload), disconnect, and capture UART.
# Note: xds110reset nSRST does not restart C29 on this kit; DSS target.reset
# re-enters boot ROM.  Button/POR XRSn remains the gold physical check.
#
# Safety contract (Gate G):
#   * BANKMODE / LIFECYCLE must match the reviewed profile (README.md).
#   * Erases "Necessary Banks Only" (Main flash) — SECCFG/NonMain untouched,
#     so the operation is reversible by re-flashing Main.  MODE3 / COMMIT of
#     bank-management is refused here.
#   * The destructive erase/program runs ONLY when ULMK_C29_FLASH_COMMIT=1;
#     otherwise the script stops after packaging and prints the exact command.
#   * Recovery (xds110reset + RAM reload) must be available before flashing.
#
# Usage:
#   scripts/hil-flash-por.sh <ulmk.out> [expect_regex] [timeout_s]
#
set -euo pipefail

BOARD_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${1:?usage: $0 <ulmk.out> [expect] [timeout]}"
EXPECT="${2:-C29SLEEP_PASS|c29_banner|ulmk:}"
TIMEOUT="${3:-55}"

TI_ROOT="${TI_INSTALL_ROOT:-/home/ulipe/ti}"
CCS_ROOT="${TI_CCS_ROOT:-$TI_ROOT/ccs2040/ccs}"
XDSRESET="$CCS_ROOT/ccs_base/common/uscif/xds110/xds110reset"
DSS="$CCS_ROOT/ccs_base/scripting/bin/dss.sh"
DSS_RESET="$BOARD_DIR/scripts/dss-reset-run.js"
DSLITE="$CCS_ROOT/ccs_base/DebugServer/bin/DSLite"
CCXML="${ULMK_HIL_CCXML:-$BOARD_DIR/targetconfigs/F29H850TU9.ccxml}"
LOCK_DIR="${ULMK_HIL_LOCK_DIR:-/tmp/ulmk-c29-hil.lock}"

# Reviewed profile — must match README.md
REVIEWED_BANKMODE="0"
REVIEWED_LIFECYCLE="HSFS"
BANKMODE="${ULMK_C29_BANKMODE:-$REVIEWED_BANKMODE}"
LIFECYCLE="${ULMK_C29_LIFECYCLE:-$REVIEWED_LIFECYCLE}"

CERT_ADDR="0x10000000"

#
# Flash-profile recovery.  Never loadti a flash-profile ELF: the debugger would
# program Main flash straight from the ELF, which has no boot certificate at
# CERT_ADDR, leaving an image the boot ROM refuses — the board then looks dead
# even though flash is intact.  Re-flash a packaged image instead.
#
recover_flash() {
	echo "HIL: flash recovery"
	if [[ -n "${ULMK_C29_RECOVERY_HEX:-}" && -f "$ULMK_C29_RECOVERY_HEX" ]]; then
		echo "HIL: re-flashing known-good $ULMK_C29_RECOVERY_HEX"
		"$DSLITE" flash --config="$CCXML" \
			-s "FlashNonMainBankModeBANKMGMT=$BANKMODE" \
			-s "FlashEraseSelection=Necessary Banks Only (for Program Load)" \
			-s "FlashNonMainSECCFGEraseToggle=false" \
			--flash "$ULMK_C29_RECOVERY_HEX" || true
	else
		echo "HIL: no ULMK_C29_RECOVERY_HEX set; flash keeps the image just"
		echo "HIL: programmed (cert intact).  Re-run with a known-good ELF to"
		echo "HIL: restore, or build the RAM profile and use hil-load-uart.sh."
	fi
	"$XDSRESET" || true
}

recover_ram() {
	echo "HIL: recovery — xds110reset + RAM reload"
	# Release our board lock first — hil-load-uart.sh takes its own.
	rmdir "$LOCK_DIR" 2>/dev/null || true
	if [[ -x "$XDSRESET" ]]; then
		"$XDSRESET" || true
		sleep 1
	fi
	export ULMK_HIL_CASE="${ULMK_HIL_CASE:-hil-flash-por-recover}"
	bash "$BOARD_DIR/scripts/hil-load-uart.sh" "$ELF" "$EXPECT" "$TIMEOUT"
}

# --- RAM profile (unchanged default path) ---
if [[ "${ULMK_C29_FLASH:-0}" != "1" ]]; then
	if [[ "${ULMK_C29_RECOVER:-0}" == "1" ]]; then
		recover_ram
		exit 0
	fi
	export ULMK_HIL_CASE="${ULMK_HIL_CASE:-hil-flash-por-ram}"
	exec "$BOARD_DIR/scripts/hil-load-uart.sh" "$ELF" "$EXPECT" "$TIMEOUT"
fi

# --- Flash profile ---
echo "ULMK-HIL:hil-flash-por:START elf=$ELF bankmode=$BANKMODE lc=$LIFECYCLE"

[[ "$BANKMODE" == "$REVIEWED_BANKMODE" ]] || {
	echo "error: BANKMODE='$BANKMODE' != reviewed '$REVIEWED_BANKMODE'" >&2; exit 1; }
[[ "$LIFECYCLE" == "$REVIEWED_LIFECYCLE" ]] || {
	echo "error: LIFECYCLE='$LIFECYCLE' != reviewed '$REVIEWED_LIFECYCLE'" >&2; exit 1; }
[[ "$BANKMODE" == "3" ]] && { echo "error: refusing irreversible BANKMODE 3" >&2; exit 1; }
[[ -f "$DSLITE" ]] || { echo "error: DSLite not found at $DSLITE" >&2; exit 1; }
[[ -f "$CCXML" ]]  || { echo "error: ccxml not found: $CCXML" >&2; exit 1; }
[[ -x "$XDSRESET" ]] || { echo "error: xds110reset missing — refuse to flash without recovery" >&2; exit 1; }

# Package: SDK-style flashable ELF with cert section updated in-place.
PKG="${ULMK_C29_PKG_DIR:-$(mktemp -d /tmp/ulmk-flashpkg.XXXXXX)}"
bash "$BOARD_DIR/scripts/package-seccfg.sh" "$ELF" "$PKG"
FLASHABLE="$PKG/ulmk_flashable.out"
[[ -f "$FLASHABLE" ]] || { echo "error: packaging did not produce $FLASHABLE" >&2; exit 1; }

# Main-flash program via the ELF (flash plugin ECC path, same as TI CCS).
# SECCFG/NonMain untouched.  After program, DSS target.reset()+runAsynch
# (not xds110reset — nSRST on this kit does not restart the C29).
#
# Contiguous FLASH_RP0 image (CPU1 + optional .cpu2_stub/.cpu3_stub).  Keep
# Necessary Banks Only — Entire Flash + free-run kit stuck on wr_pll.alg.
# NonMain SECCFG erase is opt-in only (ULMK_C29_SECCFG_COMMIT=1).
SECCFG_ERASE="false"
if [[ "${ULMK_C29_SECCFG_COMMIT:-0}" == "1" ]]; then
	SECCFG_ERASE="true"
	echo "HIL: WARNING — NonMain SECCFG erase/program enabled"
fi
DSLITE_FLASH=( "$DSLITE" flash --config="$CCXML"
	-s "FlashNonMainBankModeBANKMGMT=$BANKMODE"
	-s "FlashEraseSelection=Necessary Banks Only (for Program Load)"
	-s "FlashNonMainSECCFGEraseToggle=$SECCFG_ERASE"
	--flash "$FLASHABLE" )

if [[ "${ULMK_C29_FLASH_COMMIT:-0}" != "1" ]]; then
	echo "HIL: dry-run (set ULMK_C29_FLASH_COMMIT=1 to program Main flash)."
	echo "HIL: would run:"
	printf '   %q' "${DSLITE_FLASH[@]}"; echo
	echo "HIL: then: dss.sh dss-reset-run.js (JTAG system reset, no reload)"
	echo "HIL: recovery ready: $XDSRESET + hil-load-uart.sh"
	echo "ULMK-HIL:hil-flash-por:DRYRUN"
	exit 0
fi

[[ -x "$DSS" || -f "$DSS" ]] || { echo "error: dss.sh not found at $DSS" >&2; exit 1; }
[[ -f "$DSS_RESET" ]] || { echo "error: missing $DSS_RESET" >&2; exit 1; }

# Exclusive board lock + headless UART capture around the flash+reset.
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
	echo "error: board lock busy ($LOCK_DIR)" >&2; exit 1
fi
LOG="$(mktemp /tmp/ulmk-c29-flash.XXXXXX.log)"
DSLITE_LOG="$(mktemp /tmp/ulmk-c29-dslite.XXXXXX.log)"
cleanup() { rm -f "$LOG" "$DSLITE_LOG"; rmdir "$LOCK_DIR" 2>/dev/null || true; }
trap cleanup EXIT

SERIAL="${ULMK_HIL_SERIAL:-}"
if [[ -z "$SERIAL" ]]; then
	for cand in /dev/serial/by-id/usb-Texas_Instruments_XDS110*-if00; do
		[[ -e "$cand" ]] && SERIAL="$(readlink -f "$cand")" && break
	done
fi
[[ -n "$SERIAL" && -e "$SERIAL" ]] || { echo "error: UART not found (set ULMK_HIL_SERIAL)" >&2; exit 1; }

# Idle XIP after a prior flash POR leaves the core free-running; DSLite's
# wr_pll.alg then times out ("Stuck in free-running state").  Halt via DSS
# immediately before programming.  Skip xds110reset here — nSRST restarts
# XIP free-run and fights the halt; probe reset stays available in recover_*.
# Flash programming path:
#   * Main-only: DSLite after optional DSS preflash-halt (historical path).
#   * SECCFG COMMIT: DSLite often hits wr_pll.alg ("Stuck in free-running
#     state") when Main XIP is faulted.  Use loadti RAM warm-up + DSS
#     FlashResetOnOperation=false (dss-flash-norest.js) instead.
DSS_NOREST="$BOARD_DIR/scripts/dss-flash-norest.js"
LOADTI="$CCS_ROOT/ccs_base/scripting/examples/loadti/loadti.sh"
if [[ "$SECCFG_ERASE" == "true" && -f "$DSS_NOREST" ]]; then
	RAM_ELF="${ULMK_C29_RAM_ELF:-}"
	if [[ -z "$RAM_ELF" || ! -f "$RAM_ELF" ]]; then
		echo "error: SECCFG COMMIT flash needs a RAM-profile ELF for loadti" >&2
		echo "error: rebuild without --flash and set ULMK_C29_RAM_ELF=<ulmk>" >&2
		exit 1
	fi
	echo "HIL: WARNING — NonMain SECCFG via DSS no-reset (loadti warm-up)"
	echo "HIL: loadti RAM $RAM_ELF"
	set +e
	timeout --kill-after=10 90 bash "$LOADTI" -c "$CCXML" -a -r "$RAM_ELF" \
		>"$DSLITE_LOG" 2>&1
	set -e
	sleep 1
	echo "HIL: dss-flash-norest seccfg=1"
	set +e
	timeout --kill-after=30 240 bash "$DSS" "$DSS_NOREST" \
		"$CCXML" "$FLASHABLE" "-" 1 >>"$DSLITE_LOG" 2>&1
	FLASH_RC=$?
	set -e
	cat "$DSLITE_LOG"
	echo "HIL: dss-flash-norest rc=$FLASH_RC"
	# Treat missing OK line as failure even if timeout returned 0.
	if [[ "$FLASH_RC" == "0" ]] && ! grep -q 'dss-flash-norest: OK' "$DSLITE_LOG"; then
		FLASH_RC=1
	fi
else
	PREFLASH_HALT="$BOARD_DIR/scripts/dss-preflash-halt.js"
	if [[ -f "$PREFLASH_HALT" ]]; then
		echo "HIL: pre-flash halt (avoid wr_pll.alg free-run timeout)"
		set +e
		timeout --kill-after=15 90 bash "$DSS" "$PREFLASH_HALT" "$CCXML" C29xx_CPU1
		PREFLASH_RC=$?
		set -e
		echo "HIL: pre-flash halt rc=$PREFLASH_RC"
		sleep 2
	fi
	echo "HIL: programming Main flash headless via DSLite (SECCFG untouched)"
	set +e
	timeout --kill-after=20 300 "${DSLITE_FLASH[@]}" >"$DSLITE_LOG" 2>&1
	FLASH_RC=$?
	set -e
	cat "$DSLITE_LOG"
	echo "HIL: dslite rc=$FLASH_RC"
fi
# DSLite often exits 0 even when individual segment programs fail.
if [[ "$FLASH_RC" != "0" ]] || grep -Eqi 'Error during Flash Programming|already been programmed' "$DSLITE_LOG"; then
	echo "HIL: flash failed — proving recovery"
	recover_flash
	echo "ULMK-HIL:hil-flash-por:FAIL flash rc=$FLASH_RC"
	exit 1
fi

# Open UART *before* DSS reset: flash boot on INTOSC2 reaches idle and prints
# once within ~100ms; opening the port after sleep 1 drops every banner
# (DSS showed PC in ulmk_arch_idle_entry with an empty post-hoc capture).
stty -F "$SERIAL" 115200 cs8 -cstopb -parenb raw -echo || true
# Drain stale TX from the DSLite session.
timeout --kill-after=1 2 cat "$SERIAL" >/dev/null 2>&1 || true

echo "HIL: DSS system reset + run (no reload, then disconnect)"
: >"$LOG"
timeout --kill-after=3 "$TIMEOUT" cat "$SERIAL" >>"$LOG" &
CAT_PID=$!
# Brief settle so cat holds the port before reset releases the core.
sleep 0.3
set +e
timeout --kill-after=10 90 bash "$DSS" "$DSS_RESET" "$CCXML" C29xx_CPU1
DSS_RC=$?
set -e
echo "HIL: dss-reset-run rc=$DSS_RC"
# Let the app finish printing; then stop the capture.
sleep 2
kill "$CAT_PID" 2>/dev/null || true
wait "$CAT_PID" 2>/dev/null || true

if [[ "$DSS_RC" != "0" ]]; then
	echo "HIL: DSS reset failed — proving recovery"
	recover_flash
	echo "ULMK-HIL:hil-flash-por:FAIL dss-reset rc=$DSS_RC"
	exit 1
fi

echo "---- UART capture (post-reset, headless) ----"
cat "$LOG"
echo "-------------------------------------------"
# Reject sole leftover "alive" from a pre-reset session: require a boot banner.
if ! grep -Eiq "$EXPECT" "$LOG"; then
	echo "HIL: pattern /$EXPECT/ not seen — proving recovery"
	recover_flash
	echo "ULMK-HIL:hil-flash-por:FAIL pattern"
	exit 1
fi
if ! grep -Eiq 'c29_sleep:START|on LAUNCHXL|c29_banner|ulmk:' "$LOG"; then
	echo "HIL: only weak UART traffic (no boot banner) — refuse PASS"
	recover_flash
	echo "ULMK-HIL:hil-flash-por:FAIL no-boot-banner"
	exit 1
fi
echo "ULMK-HIL:hil-flash-por:PASS"
exit 0
