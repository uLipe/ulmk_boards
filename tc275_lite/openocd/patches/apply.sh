#!/usr/bin/env bash
# Apply the TC275 Lite Kit patch series to an aurix-openocd source tree.
set -euo pipefail

PATCH_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:-}"

if [[ -z "$SRC" ]]; then
	echo "usage: $0 /path/to/aurix-openocd" >&2
	exit 1
fi

if [[ ! -f "$SRC/src/target/aurix/aurix.c" ]]; then
	echo "aurix-openocd tree not found under: $SRC" >&2
	exit 1
fi

apply_one() {
	local patch="$1"
	local name

	name="$(basename "$patch")"
	if patch -p1 --dry-run -s -f -i "$patch" >/dev/null 2>&1; then
		patch -p1 --forward --batch -i "$patch"
		echo "applied: ${name}"
	else
		echo "skip (already applied): ${name}"
	fi
}

cd "$SRC"
while IFS= read -r line || [[ -n "$line" ]]; do
	[[ -z "$line" || "$line" =~ ^# ]] && continue
	[[ -f "$PATCH_DIR/$line" ]] || {
		echo "missing patch: $PATCH_DIR/$line" >&2
		exit 1
	}
	apply_one "$PATCH_DIR/$line"
done < "$PATCH_DIR/series"
