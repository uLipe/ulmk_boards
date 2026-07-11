#!/usr/bin/env bash
# tc275_lite/scripts/install-host-tools.sh
#
# Install host-side tools for flashing/debugging the AURIX TC275 Lite Kit:
#   - libftd2xx (FTDI, required by Infineon TAS)
#   - Infineon TAS / tas_server (Linux — DAS GUI is Windows-only)
#   - aurix-openocd (go2sh fork, --enable-tas-client)
#   - tricore-elf-gcc + gdb (NoMore201 release, same family as ulmk container)
#
# Default install prefix: ~/.local/aurix  (no sudo for the toolchain itself)
#
# Quick start:
#   1. (optional) libftd2xx from ftdichip.com — often needs glibc 2.38+ on recent
#      builds (1.4.34/1.4.35). Skip if you pass --tas-archive: the TAS .deb
#      bundles libftd2xx 1.4.27 which matches older Ubuntu (22.04).
#        https://ftdichip.com/drivers/d2xx-drivers/
#   2. Download TAS for Linux from Infineon (browser; package may be named DAS_*):
#        https://www.infineon.com/design-resources/platforms/aurix-software-tools/aurix-tools/tas
#      e.g. DAS_8.3.0_linux_x64.deb or legacy DAS_v8_*_Linux.tar.gz
#   3. Run:
#        ./install-host-tools.sh \
#          --tas-archive ~/Downloads/DAS_8.3.0_linux_x64.deb
#      or with manual FTDI (overridden by TAS bundle when present):
#        ./install-host-tools.sh \
#          --ftdi-archive ~/Downloads/libftd2xx-linux-x86_64-1.4.35.tgz \
#          --tas-archive ~/Downloads/DAS_8.3.0_linux_x64.deb
#   3. source ~/.local/aurix/env.sh
#   4. ./start-tas.sh           # before flash/debug (no sudo if udev rules installed)
#   5. ./flash.sh build/.../ulmk.elf

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOARD_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

PREFIX="${HOME}/.local/aurix"
TAS_ARCHIVE="${TAS_ARCHIVE:-}"
FTDI_ARCHIVE="${FTDI_ARCHIVE:-}"
SKIP_APT=0
SKIP_FTD2=0
SKIP_TAS=0
SKIP_OPENOCD=0
SKIP_TOOLCHAIN=0
SKIP_UDEV=0
JOBS="$(nproc 2>/dev/null || echo 4)"
DRY_RUN=0
QUIET=0

FTDI_DRIVERS_PAGE="https://ftdichip.com/drivers/d2xx-drivers/"
FTDI_DOWNLOAD_URLS=(
	"https://ftdichip.com/wp-content/uploads/2026/07/libftd2xx-linux-x86_64-1.4.35.tgz"
	"https://ftdichip.com/wp-content/uploads/2025/11/libftd2xx-linux-x86_64-1.4.34.tgz"
	"https://ftdichip.com/wp-content/uploads/2025/03/libftd2xx-linux-x86_64-1.4.33.tgz"
	"https://ftdichip.com/wp-content/uploads/2025/02/libftd2xx-linux-x86_64-1.4.32.tgz"
)
FTDI_USER_AGENT="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"

OPENOCD_REPO="https://github.com/go2sh/aurix-openocd.git"
OPENOCD_BRANCH="aurix_support"

TC_GCC_VERSION="13.4.1"
TC_GCC_URL="https://github.com/NoMore201/tricore-gcc-toolchain/releases/download/${TC_GCC_VERSION}/tricore-gcc-${TC_GCC_VERSION}-linux.tar.gz"

TAS_PAGE="https://www.infineon.com/design-resources/platforms/aurix-software-tools/aurix-tools/tas"
TAS_PKG_ID="com.ifx.tb.tool.infineontoolaccesssockettas"
TAS_VERSIONS=(8.3.0)
TAS_ARTIFACT_PREFIXES=(DAS TAS)
TAS_HOSTING_API="https://softwaretools-hosting.infineon.com/api/packages/${TAS_PKG_ID}"
TAS_INSTALL_DIR="${PREFIX}/tas"

STEP=0
STEP_TOTAL=7
STEP_T0=0

log()  { printf '\n\033[1;32m==>\033[0m %s\n' "$*"; }
step() {
	STEP=$((STEP + 1))
	STEP_T0=$SECONDS
	printf '\n\033[1;36m[%d/%d]\033[0m %s\n' "$STEP" "$STEP_TOTAL" "$*"
}
detail() { printf '    · %s\n' "$*"; }
warn()   { printf '\033[1;33mwarning:\033[0m %s\n' "$*" >&2; }
die()    { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

step_done() {
	local elapsed=$((SECONDS - STEP_T0))
	detail "done (${elapsed}s)"
}

run() {
	if [[ "$DRY_RUN" -eq 1 ]]; then
		printf '    \033[2m[dry-run]\033[0m %s\n' "$*"
	else
		if [[ "$QUIET" -eq 1 ]]; then
			"$@"
		else
			printf '    \033[2m$\033[0m %s\n' "$*"
			"$@"
		fi
	fi
}

human_size() {
	local f="$1"
	if [[ -f "$f" ]]; then
		ls -lh "$f" | awk '{print $5}'
	else
		echo "?"
	fi
}

find_tas_server() {
	local d tas

	for d in "${TAS_INSTALL_DIR}" "${PREFIX}/das"; do
		[[ -d "$d" ]] || continue
		tas="$(find "${d}" -type f -name tas_server 2>/dev/null | head -1)"
		if [[ -n "$tas" ]]; then
			printf '%s\n' "$tas"
			return 0
		fi
	done
	return 1
}

install_ftdi_from_archive() {
	local archive="$1"
	local tmp ftdi_root

	[[ -f "$archive" ]] || die "FTDI archive not found: $archive"
	detail "archive: ${archive} ($(human_size "$archive"))"
	if [[ "$DRY_RUN" -eq 1 ]]; then
		detail "would extract to: ${PREFIX}/lib/"
		return 0
	fi
	tmp="$(mktemp -d)"
	tar -xf "$archive" -C "${tmp}"
	ftdi_root="$(find "${tmp}" -name libftd2xx.so -printf '%h\n' | head -1)"
	[[ -n "$ftdi_root" ]] || die "libftd2xx.so not found in FTDI archive: $archive"
	detail "found library in: ${ftdi_root}"
	cp -a "${ftdi_root}/libftd2xx.so"* "${PREFIX}/lib/"
	cp -a "${ftdi_root}/libftd2xx.a" "${PREFIX}/lib/" 2>/dev/null || true
	rm -rf "${tmp}"
	detail "installed: ${PREFIX}/lib/libftd2xx.so ($(human_size "${PREFIX}/lib/libftd2xx.so"))"
	if ! libftd2xx_loadable "${PREFIX}/lib/libftd2xx.so"; then
		warn "this libftd2xx build needs a newer glibc than this host"
		warn "install TAS (.deb) and re-run — the TAS bundle ships libftd2xx 1.4.27"
	fi
}

libftd2xx_loadable() {
	local so="$1"

	[[ -e "$so" ]] || return 1
	if ldd "$so" >/dev/null 2>&1; then
		return 0
	fi
	return 1
}

install_ftdi_from_tas_tree() {
	local tas_tree="$1"
	local bundle tmp ftdi_dir verso

	[[ -d "$tas_tree" ]] || return 1
	bundle="$(find "${tas_tree}" \( -name 'libftd2xx-linux-x86_64-*.tgz' \
		-o -name 'libftd2xx-x86_64-*.tgz' \) -type f 2>/dev/null | head -1)"
	[[ -n "$bundle" ]] || return 1

	if [[ "$DRY_RUN" -eq 1 ]]; then
		detail "would install TAS-bundled FTDI from: ${bundle}"
		return 0
	fi

	detail "preferring TAS-bundled FTDI (older glibc): ${bundle}"
	tmp="$(mktemp -d)"
	tar -xf "$bundle" -C "${tmp}"
	ftdi_dir="$(find "${tmp}" -name 'libftd2xx.so*' ! -name '*.a' -printf '%h\n' 2>/dev/null | head -1)"
	[[ -n "$ftdi_dir" ]] || ftdi_dir="${tmp}/release/build"
	[[ -d "$ftdi_dir" ]] || {
		rm -rf "${tmp}"
		return 1
	}
	mkdir -p "${PREFIX}/lib"
	rm -f "${PREFIX}/lib/libftd2xx.so" "${PREFIX}/lib/libftd2xx.so."*
	cp -a "${ftdi_dir}"/libftd2xx.so* "${PREFIX}/lib/" 2>/dev/null || true
	if [[ ! -e "${PREFIX}/lib/libftd2xx.so" ]]; then
		verso="$(find "${PREFIX}/lib" -maxdepth 1 -name 'libftd2xx.so.*' | head -1)"
		[[ -n "$verso" ]] && ln -sf "$(basename "$verso")" "${PREFIX}/lib/libftd2xx.so"
	fi
	rm -rf "${tmp}"
	[[ -e "${PREFIX}/lib/libftd2xx.so" || -e "${PREFIX}/lib/libftd2xx.so."* ]] || return 1
	detail "installed: ${PREFIX}/lib/libftd2xx.so (from TAS bundle)"
	return 0
}

ensure_tas_bundled_ftdi() {
	local tas_tree="${1:-${TAS_INSTALL_DIR}}"

	if [[ ! -d "$tas_tree" ]]; then
		return 1
	fi
	install_ftdi_from_tas_tree "$tas_tree"
}

patch_aurix_openocd_series() {
	local src="$1"
	local apply="${BOARD_DIR}/openocd/patches/apply.sh"

	[[ -x "$apply" ]] || return 0
	[[ -d "$src" ]] || return 0
	"$apply" "$src"
}

aurix_patches_current() {
	local src="$1"

	[[ -f "${src}/src/target/aurix/aurix.c" ]] || return 1
	grep -q 'tricore_insn_length_at' "${src}/src/target/aurix/aurix.c" && \
	grep -q 'sole TAS target' "${src}/src/jtag/drivers/tas_client/tas_client.c" && \
	grep -q 'TC2XX_HF_STATUS' "${src}/src/flash/nor/tc3xx.c"
}

openocd_scripts_ok() {
	[[ -f "${PREFIX}/share/openocd/scripts/interface/tas_client.cfg" ]]
}

try_download_ftdi() {
	local dest="$1"
	local url dl_cmd=""

	if command -v curl >/dev/null 2>&1; then
		dl_cmd="curl"
	elif command -v wget >/dev/null 2>&1; then
		dl_cmd="wget"
	else
		die "need curl or wget to download libftd2xx"
	fi

	for url in "${FTDI_DOWNLOAD_URLS[@]}"; do
		detail "trying: ${url}"
		if [[ "$dl_cmd" == "curl" ]]; then
			if curl -fsSL -A "${FTDI_USER_AGENT}" -o "${dest}" "${url}"; then
				detail "download ok"
				return 0
			fi
		elif wget -q --user-agent="${FTDI_USER_AGENT}" -O "${dest}" "${url}"; then
			detail "download ok"
			return 0
		fi
		rm -f "${dest}"
	done
	return 1
}

ftdi_manual_download_help() {
	cat >&2 <<EOF

FTDI blocks automated downloads (HTTP 403 / Cloudflare challenge).

Download libftd2xx in a browser (any recent x64 build, e.g. 1.4.35):
  ${FTDI_DRIVERS_PAGE}

Recent FTDI builds (1.4.34+) often require glibc 2.38+. On Ubuntu 22.04 prefer
skipping --ftdi-archive and using the libftd2xx 1.4.27 inside the TAS .deb.

Then re-run:
  $0 --ftdi-archive /path/to/libftd2xx-linux-x86_64-*.tgz

Or set FTDI_ARCHIVE=/path/to/archive.tgz in the environment.
EOF
}

tas_hosting_url() {
	local ver="$1" artifact="$2" api="${3:-0}"

	if [[ "$api" -eq 1 ]]; then
		printf '%s/versions/%s/artifacts/%s/download\n' \
			"${TAS_HOSTING_API}" "$ver" "$artifact"
	else
		printf 'https://softwaretools-hosting.infineon.com/packages/%s/versions/%s/artifacts/%s/download\n' \
			"${TAS_PKG_ID}" "$ver" "$artifact"
	fi
}

tas_archive_looks_valid() {
	local f="$1"

	case "$f" in
	*.deb)
		file -b "$f" | grep -q 'Debian binary package'
		;;
	*.tar.gz|*.tgz|*.tar)
		tar -tf "$f" >/dev/null 2>&1
		;;
	*)
		return 1
		;;
	esac
}

extract_tas_archive() {
	local archive="$1" dest="$2"
	local ardir data

	case "$archive" in
	*.deb)
		if command -v dpkg-deb >/dev/null 2>&1; then
			dpkg-deb -x "$archive" "$dest"
			return 0
		fi
		ardir="$(mktemp -d)"
		( cd "$ardir" && ar x "$archive" )
		for data in "${ardir}"/data.tar*; do
			[[ -f "$data" ]] || continue
			tar -xf "$data" -C "$dest"
			rm -rf "$ardir"
			return 0
		done
		rm -rf "$ardir"
		die "cannot unpack .deb — install dpkg or use a .tar.gz bundle"
		;;
	*.tar.gz|*.tgz|*.tar)
		tar -xf "$archive" -C "$dest"
		;;
	*)
		die "unsupported TAS archive: $archive (.deb, .tar.gz, .tgz)"
		;;
	esac
}

try_download_tas() {
	local dest="$1"
	local ver prefix artifact url

	if ! command -v curl >/dev/null 2>&1; then
		return 1
	fi

	for ver in "${TAS_VERSIONS[@]}"; do
		for prefix in "${TAS_ARTIFACT_PREFIXES[@]}"; do
			for artifact in \
				"${prefix}_${ver}_linux_x64.deb" \
				"${prefix}_v${ver//./_}_Linux.tar.gz"; do
				url="$(tas_hosting_url "$ver" "$artifact" 1)"
				detail "trying: ${url}"
				if curl -fsSL -o "${dest}" "${url}" 2>/dev/null && \
					tas_archive_looks_valid "${dest}"; then
					detail "download ok (${artifact})"
					return 0
				fi
				rm -f "${dest}"
			done
		done
	done
	return 1
}

tas_manual_download_help() {
	local ver="${TAS_VERSIONS[0]}"

	cat >&2 <<EOF

Automatic TAS download often fails (Infineon API returns 401 without login).

Download in a browser (package is TAS but may be named DAS_*):
  ${TAS_PAGE}
  or direct:
  $(tas_hosting_url "$ver" "DAS_${ver}_linux_x64.deb" 0)

Then re-run:
  $0 --tas-archive /path/to/DAS_${ver}_linux_x64.deb

Accepted names: DAS_* or TAS_* (.deb or .tar.gz). Legacy: DAS_v8_*_Linux.tar.gz
EOF
}

write_env_sh() {
	local tas="${1:-}"
	local env_file="${PREFIX}/env.sh"

	if [[ -z "$tas" ]]; then
		tas="$(find_tas_server || true)"
	fi

	if [[ "$DRY_RUN" -eq 1 ]]; then
		detail "would write: ${env_file}"
		[[ -n "$tas" ]] && detail "  ULMK_TAS_SERVER=${tas}" || \
			detail "  ULMK_TAS_SERVER=<unset until TAS installed>"
		return 0
	fi

	cat >"${env_file}" <<ENV
# Generated by install-host-tools.sh — source before flash/debug.
export ULMK_AURIX_PREFIX="${PREFIX}"
export PATH="${PREFIX}/bin:\${PATH}"
export LD_LIBRARY_PATH="${PREFIX}/lib:\${LD_LIBRARY_PATH:-}"
export ULMK_TAS_SERVER="${tas}"
ENV
	detail "wrote ${env_file}"
	[[ -n "$tas" ]] && detail "  ULMK_TAS_SERVER=${tas}" || \
		detail "  ULMK_TAS_SERVER unset (pass --tas-archive to install TAS)"
}

ensure_start_tas() {
	local helper="${SCRIPT_DIR}/start-tas.sh"

	if [[ ! -f "$helper" ]]; then
		die "missing ${helper} — re-checkout ulmk_boards/tc275_lite"
	fi
	if [[ "$DRY_RUN" -eq 0 ]]; then
		chmod +x "$helper"
	fi
	detail "start-tas.sh: ${helper}"
}

print_banner() {
	cat <<EOF

┌──────────────────────────────────────────────────────────────┐
│  ulmk — TC275 Lite Kit host tools installer                  │
└──────────────────────────────────────────────────────────────┘

  prefix:     ${PREFIX}
  jobs:       ${JOBS}
  ftdi:       ${FTDI_ARCHIVE:-<auto (often blocked)>}
  tas:        ${TAS_ARCHIVE:-<not provided>}
  skip apt:   ${SKIP_APT}  ftdi: ${SKIP_FTD2}  tas: ${SKIP_TAS}
  skip ocd:   ${SKIP_OPENOCD}  toolchain: ${SKIP_TOOLCHAIN}  udev: ${SKIP_UDEV}
  dry-run:    ${DRY_RUN}

EOF
}

usage() {
	cat <<EOF
usage: $(basename "$0") [options]

Install host tools for TC275 Lite Kit flash/debug (Linux x86_64).

Options:
  --prefix DIR          Install root (default: ~/.local/aurix)
  --ftdi-archive FILE   optional; libftd2xx .tgz (overridden by TAS bundle)
  --tas-archive FILE    Infineon TAS archive (.deb or .tar.gz; often named DAS_*)
  --skip-apt            Do not apt-get install build dependencies
  --skip-ftdi           Skip libftd2xx download
  --skip-tas            Skip TAS extraction (already under PREFIX)
  --skip-openocd        Skip aurix-openocd build
  --skip-toolchain      Skip tricore-elf-gcc download
  --skip-udev           Do not install USB udev rules (tas_server may need root)
  --jobs N              Parallel make jobs (default: ${JOBS})
  --quiet               Less output (hide shell commands)
  --dry-run             Print planned steps only
  -h, --help            This help

After install:
  source ${PREFIX}/env.sh
  ${BOARD_DIR}/scripts/start-tas.sh
  ${BOARD_DIR}/scripts/flash.sh <ulmk.elf>

libftd2xx (optional — TAS .deb bundles a compatible copy):
  ${FTDI_DRIVERS_PAGE}

TAS (tas_server) — Infineon names many Linux packages DAS_* even though they
contain only TAS. Download in a browser if auto-download fails:
  ${TAS_PAGE}
  $(tas_hosting_url "${TAS_VERSIONS[0]}" "DAS_${TAS_VERSIONS[0]}_linux_x64.deb" 0)

On Linux only TAS runs — not the Windows DAS GUI.
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	--prefix)       PREFIX="$2"; shift 2 ;;
	--ftdi-archive) FTDI_ARCHIVE="$2"; shift 2 ;;
	--tas-archive)  TAS_ARCHIVE="$2"; shift 2 ;;
	--das-archive)
		warn "--das-archive is deprecated; use --tas-archive (Linux has TAS, not DAS)"
		TAS_ARCHIVE="$2"; shift 2
		;;
	--skip-apt)     SKIP_APT=1; shift ;;
	--skip-ftdi)    SKIP_FTD2=1; shift ;;
	--skip-tas)     SKIP_TAS=1; shift ;;
	--skip-das)
		warn "--skip-das is deprecated; use --skip-tas"
		SKIP_TAS=1; shift
		;;
	--skip-openocd) SKIP_OPENOCD=1; shift ;;
	--skip-toolchain) SKIP_TOOLCHAIN=1; shift ;;
	--skip-udev)    SKIP_UDEV=1; shift ;;
	--jobs)         JOBS="$2"; shift 2 ;;
	--quiet)        QUIET=1; shift ;;
	--dry-run)      DRY_RUN=1; shift ;;
	-h|--help)      usage; exit 0 ;;
	*)              die "unknown option: $1 (try --help)" ;;
	esac
done

TAS_INSTALL_DIR="${PREFIX}/tas"

print_banner
log "creating install tree under ${PREFIX}"
detail "mkdir -p src build bin lib share"
mkdir -p "${PREFIX}/"{src,build,bin,lib,share}
write_env_sh ""
ensure_start_tas
# ── apt build deps ───────────────────────────────────────────────────────────
if [[ "$SKIP_APT" -eq 0 ]] && command -v apt-get >/dev/null 2>&1; then
	step "Installing apt build dependencies"
	detail "packages: build-essential git wget curl autoconf automake libtool"
	detail "            pkg-config texinfo libusb-1.0-0-dev libhidapi-dev dpkg"
	run sudo apt-get update
	run sudo apt-get install -y --no-install-recommends \
		build-essential git wget curl ca-certificates \
		autoconf automake libtool pkg-config texinfo \
		libusb-1.0-0-dev libhidapi-dev dpkg
	step_done
elif [[ "$SKIP_APT" -eq 1 ]]; then
	step "Skipping apt dependencies (--skip-apt)"
	step_done
else
	step "Skipping apt (not a Debian/Ubuntu host)"
	step_done
fi

# ── libftd2xx ────────────────────────────────────────────────────────────────
step "Installing libftd2xx (FTDI driver for TAS)"
if [[ "$SKIP_FTD2" -eq 1 ]]; then
	detail "skipped (--skip-ftdi)"
elif [[ -f "${PREFIX}/lib/libftd2xx.so" ]]; then
	detail "already installed: ${PREFIX}/lib/libftd2xx.so"
	detail "size: $(human_size "${PREFIX}/lib/libftd2xx.so")"
elif [[ -n "$FTDI_ARCHIVE" ]]; then
	install_ftdi_from_archive "$FTDI_ARCHIVE"
elif [[ "$DRY_RUN" -eq 1 ]]; then
	detail "would try FTDI download or use TAS-bundled copy"
	detail "pass --ftdi-archive only if needed; TAS .deb overrides with 1.4.27"
else
	tmp="$(mktemp -d)"
	trap 'rm -rf "${tmp}"' EXIT
	if try_download_ftdi "${tmp}/ftdi.tgz"; then
		detail "archive size: $(human_size "${tmp}/ftdi.tgz")"
		install_ftdi_from_archive "${tmp}/ftdi.tgz"
	else
		warn "no libftd2xx yet — TAS (.deb) bundles libftd2xx 1.4.27"
		detail "manual FTDI 1.4.34/1.4.35 may need glibc 2.38+ on this host"
	fi
	trap - EXIT
	rm -rf "${tmp}"
fi
step_done

# ── Infineon TAS (tas_server) ────────────────────────────────────────────────
install_tas() {
	local archive="$1"
	local dest="${TAS_INSTALL_DIR}"
	local tas base

	[[ -f "$archive" ]] || die "TAS archive not found: $archive"
	tas_archive_looks_valid "$archive" || \
		die "file does not look like a TAS archive: $archive"
	base="$(basename "$archive")"
	detail "archive: ${archive} ($(human_size "$archive"))"
	case "$base" in
	DAS_*)
		detail "note: DAS_* filename is Infineon naming — contents are TAS/tas_server"
		;;
	TAS_*)
		;;
	*)
		detail "note: accepting archive (will search for tas_server inside)"
		;;
	esac
	if [[ "$DRY_RUN" -eq 1 ]]; then
		detail "would extract to: ${dest}"
		return 0
	fi
	detail "extracting (may take a moment)..."
	rm -rf "${dest}"
	mkdir -p "${dest}"
	extract_tas_archive "$archive" "${dest}"
	tas="$(find "${dest}" -type f -name tas_server 2>/dev/null | head -1)"
	[[ -n "$tas" ]] || die "tas_server not found inside TAS archive: $archive"
	chmod +x "$tas"
	detail "tas_server: ${tas}"
	detail "tas tree: $(find "${dest}" -type f 2>/dev/null | wc -l) files under ${dest}"
	ensure_tas_bundled_ftdi "${dest}" || true
}

step "Installing Infineon TAS (tas_server)"
if [[ "$SKIP_TAS" -eq 1 ]]; then
	detail "skipped (--skip-tas)"
elif [[ -n "$TAS_ARCHIVE" ]]; then
	install_tas "$TAS_ARCHIVE"
elif find_tas_server >/dev/null 2>&1; then
	tas="$(find_tas_server)"
	detail "reusing existing install"
	detail "tas_server: ${tas}"
elif [[ "$DRY_RUN" -eq 1 ]]; then
	detail "would try Infineon download (DAS_* and TAS_* artifact names)"
	detail "or pass: --tas-archive /path/to/DAS_*_linux_x64.deb"
	detail "would install to: ${TAS_INSTALL_DIR}/"
else
	tmp="$(mktemp -d)"
	trap 'rm -rf "${tmp}"' EXIT
	if try_download_tas "${tmp}/tas.pkg"; then
		detail "archive size: $(human_size "${tmp}/tas.pkg")"
		install_tas "${tmp}/tas.pkg"
	else
		tas_manual_download_help
		die "TAS download failed"
	fi
	trap - EXIT
	rm -rf "${tmp}"
fi
step_done

# ── aurix-openocd ────────────────────────────────────────────────────────────
step "Building aurix-openocd (TAS client + FTDI)"
if [[ "$SKIP_OPENOCD" -eq 1 ]]; then
	detail "skipped (--skip-openocd)"
elif [[ -x "${PREFIX}/bin/openocd" ]] && openocd_scripts_ok; then
	src="${PREFIX}/src/aurix-openocd"
	if [[ -d "${src}/.git" ]] && ! aurix_patches_current "${src}"; then
		detail "aurix-openocd patch series changed — applying and rebuilding"
		patch_aurix_openocd_series "${src}"
		make -C "${PREFIX}/build/aurix-openocd" -j"${JOBS}"
		make -C "${PREFIX}/build/aurix-openocd" install
		detail "openocd rebuilt after patch series update"
	else
		detail "already installed: ${PREFIX}/bin/openocd"
		detail "version: $("${PREFIX}/bin/openocd" --version 2>/dev/null | head -1 || echo unknown)"
		detail "scripts: ${PREFIX}/share/openocd/scripts"
	fi
elif [[ "$DRY_RUN" -eq 1 ]]; then
	detail "would clone: ${OPENOCD_REPO} (branch ${OPENOCD_BRANCH})"
	detail "would configure: --enable-tas-client --enable-ftdi --prefix=${PREFIX}"
	detail "would build with: make -j${JOBS}"
else
	src="${PREFIX}/src/aurix-openocd"
	bld="${PREFIX}/build/aurix-openocd"
	if [[ ! -d "${src}/.git" ]]; then
		detail "cloning ${OPENOCD_REPO}"
		detail "branch: ${OPENOCD_BRANCH}"
		git clone --depth=1 --branch "${OPENOCD_BRANCH}" \
			"${OPENOCD_REPO}" "${src}"
	else
		detail "updating existing clone at ${src}"
		git -C "${src}" fetch --depth=1 origin "${OPENOCD_BRANCH}"
		git -C "${src}" checkout "${OPENOCD_BRANCH}"
		git -C "${src}" pull --ff-only origin "${OPENOCD_BRANCH}" || true
		detail "HEAD: $(git -C "${src}" rev-parse --short HEAD 2>/dev/null || echo ?)"
	fi
	patch_aurix_openocd_series "${src}"
	rm -rf "${bld}"
	mkdir -p "${bld}"
	if [[ ! -x "${src}/configure" ]]; then
		detail "running bootstrap in ${src}"
		( cd "${src}" && ./bootstrap )
	else
		detail "reusing ${src}/configure"
	fi
	detail "running configure in ${bld}"
	( cd "${bld}" && "${src}/configure" \
		--prefix="${PREFIX}" \
		--enable-tas-client \
		--enable-ftdi \
		--disable-werror )
	detail "compiling (make -j${JOBS}) — usually the slowest step..."
	make -C "${bld}" -j"${JOBS}"
	detail "installing to ${PREFIX}/bin"
	make -C "${bld}" install
	detail "openocd: ${PREFIX}/bin/openocd"
	detail "version: $("${PREFIX}/bin/openocd" --version 2>/dev/null | head -1 || echo unknown)"
	openocd_scripts_ok || die "openocd scripts missing after install"
	detail "scripts: ${PREFIX}/share/openocd/scripts"
fi
step_done

# ── tricore-elf-gcc (host GDB for debug) ───────────────────────────────────
step "Installing tricore-elf-gcc + gdb (host debug)"
if [[ "$SKIP_TOOLCHAIN" -eq 1 ]]; then
	detail "skipped (--skip-toolchain)"
elif [[ -x "${PREFIX}/bin/tricore-elf-gcc" ]]; then
	detail "already installed: ${PREFIX}/bin/tricore-elf-gcc"
	detail "gcc: $("${PREFIX}/bin/tricore-elf-gcc" --version 2>/dev/null | head -1 || echo unknown)"
elif [[ "$DRY_RUN" -eq 1 ]]; then
	detail "would download: ${TC_GCC_URL}"
	detail "would install binaries to: ${PREFIX}/bin/"
else
	detail "download: ${TC_GCC_URL}"
	tmp="$(mktemp -d)"
	wget --progress=dot:giga -O "${tmp}/tricore-gcc.tar.gz" "${TC_GCC_URL}"
	detail "archive size: $(human_size "${tmp}/tricore-gcc.tar.gz")"
	detail "extracting..."
	mkdir -p "${tmp}/extract"
	tar -xf "${tmp}/tricore-gcc.tar.gz" -C "${tmp}/extract"
	gcc_bin="$(find "${tmp}/extract" -name tricore-elf-gcc -type f | head -1)"
	[[ -n "$gcc_bin" ]] || die "tricore-elf-gcc not found in toolchain archive"
	bindir="$(dirname "$gcc_bin")"
	detail "toolchain bindir: ${bindir}"
	detail "copying $(find "${bindir}" -maxdepth 1 -type f 2>/dev/null | wc -l) binaries..."
	cp -a "${bindir}/." "${PREFIX}/bin/"
	rm -rf "${tmp}"
	detail "tricore-elf-gcc: ${PREFIX}/bin/tricore-elf-gcc"
	detail "tricore-elf-gdb:  ${PREFIX}/bin/tricore-elf-gdb"
	detail "gcc version: $("${PREFIX}/bin/tricore-elf-gcc" --version 2>/dev/null | head -1 || echo unknown)"
fi
step_done

# ── udev (USB access for tas_server without root) ───────────────────────────
step "Installing USB udev rules"
if [[ "$SKIP_UDEV" -eq 1 ]]; then
	detail "skipped (--skip-udev)"
	warn "tas_server may require root without udev rules"
elif ! command -v apt-get >/dev/null 2>&1; then
	detail "skipped (not Debian/Ubuntu)"
	warn "install udev rules manually for vendor 058b (Infineon) and 0403 (FTDI)"
else
	udev_file="/etc/udev/rules.d/99-ulmk-aurix-tas.rules"
	detail "writing ${udev_file} (Infineon 058b + FTDI 0403)"
	if [[ "$DRY_RUN" -eq 1 ]]; then
		detail "[dry-run] would install udev rules and plugdev membership"
	else
		sudo tee "$udev_file" >/dev/null <<'UDEV'
# AURIX Lite Kit onboard DAP — allow tas_server without root
SUBSYSTEM=="usb", ATTR{idVendor}=="058b", MODE="0666", GROUP="plugdev", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="0403", MODE="0666", GROUP="plugdev", TAG+="uaccess"
UDEV
		sudo udevadm control --reload-rules
		sudo udevadm trigger
		detail "udev rules loaded"
		if ! getent group plugdev >/dev/null 2>&1; then
			sudo groupadd plugdev
			detail "created group plugdev"
		fi
		if ! id -nG "${USER}" | grep -qw plugdev; then
			sudo usermod -aG plugdev "${USER}"
			warn "added ${USER} to plugdev — log out/in or: newgrp plugdev"
		else
			detail "user ${USER} is in group plugdev"
		fi
	fi
fi
step_done

# ── env.sh refresh (tas_server path may have changed) ───────────────────────
step "Refreshing environment helpers"
ensure_tas_bundled_ftdi || true
TAS_SERVER="$(find_tas_server || true)"
write_env_sh "${TAS_SERVER}"
ensure_start_tas
step_done

# ── summary ──────────────────────────────────────────────────────────────────
TOTAL_ELAPSED=$((SECONDS))
log "install finished in ${TOTAL_ELAPSED}s"

printf '\n  %-22s %s\n' "Component" "Status"
printf '  %-22s %s\n' "────────" "──────"
status_file() { [[ -e "$1" ]] && echo "OK  $1" || echo "—   missing"; }
printf '  %-22s %s\n' "libftd2xx" "$(status_file "${PREFIX}/lib/libftd2xx.so")"
printf '  %-22s %s\n' "tas_server" "$(status_file "${TAS_SERVER:-${TAS_INSTALL_DIR}/none}")"
printf '  %-22s %s\n' "openocd" "$(status_file "${PREFIX}/bin/openocd")"
printf '  %-22s %s\n' "tricore-elf-gcc" "$(status_file "${PREFIX}/bin/tricore-elf-gcc")"
printf '  %-22s %s\n' "tricore-elf-gdb" "$(status_file "${PREFIX}/bin/tricore-elf-gdb")"
printf '  %-22s %s\n' "env.sh" "$(status_file "${PREFIX}/env.sh")"
printf '  %-22s %s\n' "start-tas.sh" "$(status_file "${SCRIPT_DIR}/start-tas.sh")"

cat <<EOF

Next steps:

  source ${PREFIX}/env.sh

  # kit on USB3 — start TAS (no sudo after udev rules + plugdev)
  ${BOARD_DIR}/scripts/start-tas.sh

  # flash firmware built in ulmk container
  ${BOARD_DIR}/scripts/flash.sh /path/to/ulmk.elf

  # or debug
  ${BOARD_DIR}/scripts/debug.sh
  tricore-elf-gdb -ex 'target remote :3333' /path/to/ulmk.elf

  # serial userspace console: USB COM port, 115200 8N1

EOF

if [[ -z "$TAS_SERVER" ]]; then
	warn "TAS not installed — pass --tas-archive to complete the setup."
fi

if [[ "$DRY_RUN" -eq 1 ]]; then
	warn "dry-run only; nothing was installed."
fi
