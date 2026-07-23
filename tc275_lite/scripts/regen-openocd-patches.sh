#!/usr/bin/env bash
# Regenerate openocd/patches/*.patch from a patched aurix-openocd clone.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PATCH_DIR="${ROOT}/openocd/patches"
SRC="${1:-${ULMK_AURIX_SRC:-$HOME/.local/aurix/src/aurix-openocd}}"

if [[ ! -d "$SRC/.git" ]]; then
	echo "aurix-openocd git tree required: $SRC" >&2
	exit 1
fi

mkdir -p "${PATCH_DIR}/overlays"

cd "$SRC"
if ! git diff --quiet HEAD -- \
	src/flash/nor/tc3xx.c \
	src/target/aurix/aurix.c \
	src/target/aurix/aurix.h \
	src/jtag/drivers/tas_client/tas_client.c \
	src/jtag/drivers/tas_client/tas_protocol.c; then
	:
else
	echo "no local changes vs HEAD — nothing to export" >&2
	exit 1
fi

git diff HEAD -- src/flash/nor/tc3xx.c \
	> "${PATCH_DIR}/0001-flash-nor-tc27x-tc2xx-support.patch"
git diff HEAD -- src/target/aurix/aurix.c src/target/aurix/aurix.h \
	> "${PATCH_DIR}/0002-target-aurix-tc2xx-debug-step.patch"
git diff HEAD -- src/jtag/drivers/tas_client/tas_client.c \
	src/jtag/drivers/tas_client/tas_protocol.c \
	> "${PATCH_DIR}/0003-jtag-tas-client-sole-target.patch"

cp src/target/aurix/aurix.c "${PATCH_DIR}/overlays/aurix.c"
cp src/target/aurix/aurix.h "${PATCH_DIR}/overlays/aurix.h"
cp src/flash/nor/tc3xx.c "${PATCH_DIR}/overlays/tc3xx.c"

echo "regenerated patches in ${PATCH_DIR}"
wc -l "${PATCH_DIR}"/*.patch
