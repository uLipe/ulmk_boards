#!/usr/bin/env bash
# tc275_lite/scripts/hil-reset-cause.sh
#
# Post-mortem dump for "red ESR0 on standalone boot".
#
# Reproduce FIRST, then run WITHOUT power-cycling:
#   1. flash.sh <elf> and let OpenOCD detach
#   2. Press RESET (or USB unplug/replug) until red LED is on
#   3. Run this script (hot-attach, no /PORST)
#
# OpenOCD `reg pc` fails on hot-attach (GDB reg cache not built).  Read PC
# via OCDS CSFR mirror instead: CPU0 @ 0xF8810000 + 0xFE08.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# shellcheck source=/dev/null
source "$(dirname "$0")/aurix-env.sh"
source_aurix_env || { echo "env.sh not found — run scripts/install-host-tools.sh" >&2; exit 1; }

if ! pgrep -f tas_server >/dev/null 2>&1; then
	echo "tas_server not running — start it: ${ROOT}/scripts/start-tas.sh" >&2
	exit 1
fi

OCD="${ULMK_AURIX_PREFIX}/bin/openocd"
OCD_SEARCH=(-s "${ULMK_AURIX_PREFIX}/share/openocd/scripts" -s "${ROOT}/openocd")
OCD_CFG="${ROOT}/openocd/tc275_lite_hotattach.cfg"

# OCDS per-core window (aurix.c): base + 0x20000 * coreid
CPU0_OCDS=0xF8810000
CPU0_PC=$((CPU0_OCDS + 0xFE08))
CPU0_DBGSR=$((CPU0_OCDS + 0xFD00))

pkill -9 -f "${OCD}" 2>/dev/null || true
sleep 1

out="$("$OCD" "${OCD_SEARCH[@]}" -f "${OCD_CFG}" \
	-c "init" \
	-c "targets tc27x.cpu0" \
	-c "echo {=== CPU0 OCDS PC / DBGSR ===}" \
	-c "mdw 0x$(printf '%X' "${CPU0_PC}")" \
	-c "mdw 0x$(printf '%X' "${CPU0_DBGSR}")" \
	-c "echo {=== CBS_OSTATE (HARR=bit8) / TR0 EVT+ADR ===}" \
	-c "mdw 0xF0000480" \
	-c "mdw 0xF881F000 2" \
	-c "echo {=== RSTSTAT / RSTCON / STSTAT ===}" \
	-c "mdw 0xF0036050" \
	-c "mdw 0xF0036058" \
	-c "mdw 0xF00360C0" \
	-c "echo {=== ESROCFG / IOCR / ESR IN ===}" \
	-c "mdw 0xF0036078" \
	-c "mdw 0xF00360A0" \
	-c "mdw 0xF00360AC" \
	-c "echo {=== WDTS + WDTCPU0 CON0 CON1 ===}" \
	-c "mdw 0xF00360F0 2" \
	-c "mdw 0xF0036100 2" \
	-c "echo {=== FLASH0_PROCOND ===}" \
	-c "mdw 0xF8001030" \
	-c "echo {=== BMHD0 cached @ 0xA0000000 ===}" \
	-c "mdw 0xA0000000 8" \
	-c "echo {=== BMHD0 NC @ 0x80000000 ===}" \
	-c "mdw 0x80000000 8" \
	-c "shutdown" 2>&1)" || true

echo "$out"

python3 - "$out" <<'PY'
import re, sys

text = sys.argv[1]

def mdw(addr):
	m = re.search(rf"^0x{addr:08x}:\s+([0-9a-f]+)", text, re.I | re.M)
	return int(m.group(1), 16) if m else None

def mdw_block(addr, n=1):
	vals = []
	for i in range(n):
		v = mdw(addr + 4 * i)
		if v is None:
			break
		vals.append(v)
	return vals

print("\n--- decode ---")
rst = mdw(0xF0036050)
if rst is not None:
	print(f"RSTSTAT=0x{rst:08X}")
	warm = {
		"ESR0": rst & 1,
		"ESR1": (rst >> 1) & 1,
		"SMU": (rst >> 3) & 1,
		"SW": (rst >> 4) & 1,
		"STM0": (rst >> 5) & 1,
	}
	cold = {
		"PORST": (rst >> 16) & 1,
		"CB0": (rst >> 18) & 1,
		"CB3": (rst >> 20) & 1,
		"EVR13": (rst >> 23) & 1,
		"EVR33": (rst >> 24) & 1,
		"SWD": (rst >> 25) & 1,
		"STBYR": (rst >> 28) & 1,
	}
	print("  warm:", ", ".join(f"{k}={v}" for k, v in warm.items() if v))
	print("  cold:", ", ".join(f"{k}={v}" for k, v in cold.items() if v))
	if rst == 0x13810000:
		print("  => cold power-on default only (no warm SMU/WDT latched)")

pc = mdw(0xF881FE08)
dbgsr = mdw(0xF881FD00)
if pc is not None:
	print(f"CPU0 PC (OCDS)=0x{pc:08X}", end="")
	if 0xA0000000 <= pc <= 0xA0040000:
		print("  [PFlash user]")
	elif 0x8FFF0000 <= pc <= 0x90000000:
		print("  [BootROM/SSW region?]")
	else:
		print()
if dbgsr is not None:
	print(f"CPU0 DBGSR=0x{dbgsr:08X}  HALT={(dbgsr>>1)&1} EVTSRC={(dbgsr>>8)&0xFF}")

ostate = mdw(0xF0000480)
if ostate is not None:
	print(f"CBS_OSTATE=0x{ostate:08X}  OEN={ostate&1} HARR={(ostate>>8)&1} WDTSUS={(ostate>>7)&1}")
	if (ostate >> 8) & 1:
		print("  => Halt-After-Reset REQUEST sticky — button reset will halt at STAD")

tr = mdw_block(0xF881F000, 2)
if len(tr) >= 2:
	print(f"TR0 EVT=0x{tr[0]:08X} ADR=0x{tr[1]:08X}")
	if tr[0]:
		print("  => HW trigger 0 armed (SSW BAM / leftover breakpoint)")

esr = mdw(0xF0036078)
if esr is not None:
	print(f"ESROCFG=0x{esr:08X}  ARI={(esr&1)}  (1=app-reset indicator, holds ESR0/LED)")

esr_in = mdw(0xF00360AC)
if esr_in is not None:
	print(f"ESR IN.P0={(esr_in&1)}  (SSW waits for 1 before jump to user code)")

st = mdw(0xF00360C0)
if st is not None:
	hwcfg = st & 0xFF
	print(f"STSTAT.HWCFG=0x{hwcfg:02X}  (111b=internal flash)")

pro = mdw(0xF8001030)
if pro is not None:
	esr0cnt = (pro >> 16) & 0xFFF
	print(f"PROCOND.ESR0CNT=0x{esr0cnt:03X}  (FFF=SSW ignores ESR0)")

wdt = mdw_block(0xF00360F0, 4)
if len(wdt) >= 4:
	print(f"WDTS CON1=0x{wdt[1]:08X}  DR={(wdt[1]>>3)&1}")
	print(f"WDTCPU0 CON1=0x{wdt[3]:08X}  DR={(wdt[3]>>3)&1}")
	if not ((wdt[1] >> 3) & 1) and not ((wdt[3] >> 3) & 1):
		print("  => WDTs still enabled — user _start/board_init not reached")

bmhd = mdw_block(0xA0000000, 8)
if len(bmhd) >= 8:
	stad, bmi_bmhdid = bmhd[0], bmhd[1]
	bmi = bmi_bmhdid & 0xFFFF
	bmhdid = (bmi_bmhdid >> 16) & 0xFFFF
	crc_head, crc_inv = bmhd[6], bmhd[7]
	print(f"BMHD STAD=0x{stad:08X} BMI=0x{bmi:04X} ID=0x{bmhdid:04X}")
	print(f"BMHD CRChead=0x{crc_head:08X} ~CRC=0x{crc_inv:08X}")
	ok = (bmhdid == 0xB359 and stad == 0xA0000020 and
	      crc_inv == ((~crc_head) & 0xFFFFFFFF))
	print(f"BMHD layout/CRC quick-check: {'OK' if ok else 'MISMATCH'}")
PY
