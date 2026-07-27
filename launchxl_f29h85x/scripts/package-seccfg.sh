#!/usr/bin/env bash
# Package a flash-boot image for F29H85x (Gate F/G).
#
# Matches the TI SDK post-build flow (DUMMY_CERT=1):
#   1. objcopy -O binary (drop cert + secondary banks) → ulmk_flash.bin
#   2. updateDummyCert patches a copy of the crypto-unlock cert with image size
#   3. --update-section cert=<patched> into a flashable ELF (DSLite programs this)
#   4. Also emit ulmk_cert.bin / .hex for tools that want a flat image
#
# Usage:
#   scripts/package-seccfg.sh <ulmk.out> <out_dir>
#
set -euo pipefail

ELF="${1:?usage: $0 <ulmk.out> <out_dir>}"
OUT="${2:?usage: $0 <ulmk.out> <out_dir>}"

TI_ROOT="${TI_INSTALL_ROOT:-/home/ulipe/ti}"
CCS_ROOT="${TI_CCS_ROOT:-$TI_ROOT/ccs2040/ccs}"
SDK_ROOT="${TI_F29_SDK_ROOT:-$TI_ROOT/f29h85x-sdk_1_02_01_00}"

CERT_ADDR="0x10000000"
LOAD_ADDR="0x10001000"

REVIEWED_BANKMODE="0"
REVIEWED_LIFECYCLE="HSFS"
BANKMODE="${ULMK_C29_BANKMODE:-$REVIEWED_BANKMODE}"
LIFECYCLE="${ULMK_C29_LIFECYCLE:-$REVIEWED_LIFECYCLE}"
SIGN_KEY="${ULMK_C29_SIGN_KEY:-$SDK_ROOT/tools/boot/signing/mcu_gpkey.pem}"

die() { echo "package-seccfg: error: $*" >&2; exit 1; }

[[ -f "$ELF" ]] || die "missing ELF $ELF"
[[ "$BANKMODE" == "$REVIEWED_BANKMODE" ]] || \
	die "BANKMODE='$BANKMODE' != reviewed '$REVIEWED_BANKMODE'"
[[ "$LIFECYCLE" == "$REVIEWED_LIFECYCLE" ]] || \
	die "LIFECYCLE='$LIFECYCLE' != reviewed '$REVIEWED_LIFECYCLE'"
[[ "$BANKMODE" == "3" ]] && die "refusing irreversible BANKMODE 3 / COMMIT"

SIGN_MODE="${ULMK_C29_SIGN:-hsfs}"
DUMMY_CERT="$SDK_ROOT/source/dummycert/dummy_cert_crypto_unlock_flash.cert"
UPDCERT="$SDK_ROOT/tools/misc/updateDummyCert.bin"
GEN="$SDK_ROOT/tools/boot/signing/mcu_rom_image_gen.py"

if [[ "$SIGN_MODE" == "hsfs" ]]; then
	[[ -f "$DUMMY_CERT" ]] || die "dummy cert not found: $DUMMY_CERT"
	[[ -f "$UPDCERT" ]] || die "updateDummyCert not found: $UPDCERT"
	[[ -x "$UPDCERT" ]] || chmod +x "$UPDCERT" 2>/dev/null || true
else
	[[ -f "$GEN" ]] || die "SDK cert tool not found: $GEN"
	[[ -f "$SIGN_KEY" ]] || die "signing key not found: $SIGN_KEY"
fi

OBJCOPY=""
for d in "$CCS_ROOT"/tools/compiler/ti-cgt-c29*/bin; do
	[[ -x "$d/c29objcopy" ]] && OBJCOPY="$d/c29objcopy" && break
done
[[ -n "$OBJCOPY" ]] || command -v c29objcopy >/dev/null && OBJCOPY="${OBJCOPY:-c29objcopy}"
[[ -n "$OBJCOPY" ]] || die "c29objcopy not found (set TI_CCS_ROOT)"

mkdir -p "$OUT"
BIN="$OUT/ulmk_flash.bin"
FLASHABLE="$OUT/ulmk_flashable.out"
COMBINED="$OUT/ulmk_cert.bin"
CERTPAD="$OUT/C29-cert-pad.bin"

echo "package-seccfg: BANKMODE=$BANKMODE LIFECYCLE=$LIFECYCLE SIGN=$SIGN_MODE"
echo "package-seccfg: $ELF -> $OUT (loadaddr $LOAD_ADDR, cert $CERT_ADDR)"

# Contiguous CPU1 image for the cert size field (SDK also drops `cert`).
# Contiguous CPU1 image for the cert size/hash.  Always drop secondary stubs:
# they are linked *after* resetvector (past the signed image).  Including them
# in the BIN is unnecessary; stripping them from a mid-image hole used to
# leave cert zeros vs flash stub bytes and the boot ROM refused to start.
"$OBJCOPY" -O binary \
	--remove-section=cert \
	--remove-section=.cpu2_stub --remove-section=.cpu3_stub \
	--remove-section=.cpu2_codestart --remove-section=.cpu2_nmivector \
	--remove-section=.cpu3_codestart --remove-section=.cpu3_nmivector \
	--remove-section=.TI.bound:CPU1_Cfg --remove-section=.TI.bound:CPU2_Cfg \
	--remove-section=.TI.bound:CPU3_Cfg --remove-section=.TI.bound:CPU4_Cfg \
	"$ELF" "$BIN"

cp "$ELF" "$FLASHABLE"

# Strip secondary stubs from the flashable ELF unless the caller opts in for
# an SMP flash profile (stubs after resetvector in FLASH_RP0).
if [[ "${ULMK_C29_FLASH_SECONDARY:-0}" != "1" ]]; then
	"$OBJCOPY" \
		--remove-section=.cpu2_stub --remove-section=.cpu3_stub \
		--remove-section=.cpu2_codestart --remove-section=.cpu2_nmivector \
		--remove-section=.cpu3_codestart --remove-section=.cpu3_nmivector \
		"$FLASHABLE" "$FLASHABLE"
fi

# NonMain SECCFG is opt-in: ordinary Main-flash HIL must never erase it.
# ULMK_C29_SECCFG_COMMIT=1 keeps .TI.bound:* and hil-flash-por enables
# FlashNonMainSECCFGEraseToggle (can brick on bad CRC — review first).
if [[ "${ULMK_C29_SECCFG_COMMIT:-0}" != "1" ]]; then
	"$OBJCOPY" \
		--remove-section=.TI.bound:CPU1_Cfg \
		--remove-section=.TI.bound:CPU2_Cfg \
		--remove-section=.TI.bound:CPU3_Cfg \
		--remove-section=.TI.bound:CPU4_Cfg \
		"$FLASHABLE" "$FLASHABLE"
	# c29objcopy --remove-section leaves ghost PT_LOAD @ NonMain; the flash
	# plugin then tries to program 0x10D85xxx and fails unless SECCFG erase
	# is enabled.  Compact those program headers out of the flashable ELF.
	python3 - "$FLASHABLE" <<'PY'
import struct, sys
path = sys.argv[1]
data = bytearray(open(path, "rb").read())
u16 = lambda o: struct.unpack_from("<H", data, o)[0]
u32 = lambda o: struct.unpack_from("<I", data, o)[0]
e_phoff, e_phentsize, e_phnum = u32(28), u16(42), u16(44)
keep = []
for i in range(e_phnum):
	o = e_phoff + i * e_phentsize
	t, vaddr = u32(o), u32(o + 8)
	if t == 1 and 0x10D85000 <= vaddr < 0x10D90000:
		continue
	keep.append(bytes(data[o:o + e_phentsize]))
for i, phdr in enumerate(keep):
	o = e_phoff + i * e_phentsize
	data[o:o + e_phentsize] = phdr
for i in range(len(keep), e_phnum):
	o = e_phoff + i * e_phentsize
	data[o:o + e_phentsize] = b"\0" * e_phentsize
struct.pack_into("<H", data, 44, len(keep))
open(path, "wb").write(data)
print(f"package-seccfg: dropped NonMain PT_LOAD (phnum {e_phnum} -> {len(keep)})")
PY
	echo "package-seccfg: SECCFG stripped (set ULMK_C29_SECCFG_COMMIT=1 to program NonMain)"
else
	echo "package-seccfg: WARNING — SECCFG NonMain COMMIT enabled"
	CHECKER="$SDK_ROOT/tools/misc/seccfgChecker.py"
	if [[ -f "$CHECKER" ]]; then
		(
			cd "$OUT"
			for c in 1 2 3 4; do
				"$OBJCOPY" -O binary \
					--only-section=".TI.bound:CPU${c}_Cfg" \
					"$FLASHABLE" "seccfgCpu${c}.bin"
			done
			python3 "$CHECKER" "$OBJCOPY" "$ELF"
		) || die "seccfgChecker rejected SECCFG"
	fi
fi

if [[ "$SIGN_MODE" == "hsfs" ]]; then
	cp "$DUMMY_CERT" "$CERTPAD"
	"$UPDCERT" "$CERTPAD" "$BIN"
	cat "$CERTPAD" "$BIN" > "$COMBINED"
	"$OBJCOPY" --update-section "cert=$CERTPAD" "$FLASHABLE" "$FLASHABLE"
else
	( cd "$OUT" && python3 "$GEN" \
		--image-bin "$BIN" --core C29 --swrv 1 --loadaddr "$LOAD_ADDR" \
		--sign-key "$SIGN_KEY" --out-image "$COMBINED" \
		--device f29h85x --boot FLASH --img_integ no )
	[[ -f "$OUT/C29-cert-pad.bin" ]] || die "HSSE cert pad missing"
	"$OBJCOPY" --update-section "cert=$OUT/C29-cert-pad.bin" \
		"$FLASHABLE" "$FLASHABLE"
fi

[[ -f "$FLASHABLE" ]] || die "flashable ELF missing"
[[ -f "$COMBINED" ]] || die "combined image missing"

HEX="$OUT/ulmk_cert.hex"
"$OBJCOPY" -I binary -O ihex \
	--set-section-flags .data=alloc,load,contents \
	--change-section-address ".data=$CERT_ADDR" \
	"$COMBINED" "$HEX"

echo "package-seccfg: wrote $(basename "$FLASHABLE") (flash this with DSLite)"
echo "package-seccfg: wrote $(basename "$COMBINED") ($(stat -c%s "$COMBINED") bytes)"
echo "package-seccfg: hex    $HEX (optional flat image)"
