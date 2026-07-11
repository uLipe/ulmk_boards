#!/usr/bin/env bash
# tc275_lite/scripts/start-tas.sh — start Infineon TAS server (before openocd).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
source "${SCRIPT_DIR}/aurix-env.sh"

TAS_PORT="${ULMK_TAS_PORT:-24817}"
WAIT_SECS="${ULMK_TAS_WAIT_SECS:-15}"
VERBOSE=1

usage() {
	cat <<EOF
usage: $(basename "$0") [options]

Start Infineon tas_server for AURIX flash/debug.

Options:
  -v, --verbose   Show pid and port check (default)
  -q, --quiet     Only print errors
  -h, --help      This help
EOF
}

log() {
	[[ "$VERBOSE" -eq 1 ]] && echo "$@"
}

warn() {
	echo "warning: $*" >&2
}

tas_port_open() {
	local port="$1"

	if command -v ss >/dev/null 2>&1; then
		ss -tlnH "sport = :${port}" 2>/dev/null | grep -q .
	elif command -v netstat >/dev/null 2>&1; then
		netstat -tln 2>/dev/null | grep -q ":${port} "
	else
		return 1
	fi
}

tas_pid() {
	pgrep -f "$(basename "${ULMK_TAS_SERVER}")" 2>/dev/null | head -1 || true
}

report_tas_alive() {
	local pid="${1:-}"

	[[ -n "$pid" ]] || pid="$(tas_pid)"
	if [[ -z "$pid" ]]; then
		warn "tas_server process not found"
		return 1
	fi
	if tas_port_open "${TAS_PORT}"; then
		log "tas_server alive — pid ${pid}, listening on tcp/${TAS_PORT}"
	else
		log "tas_server running — pid ${pid} (port ${TAS_PORT} not visible yet)"
	fi
	return 0
}

wait_for_tas() {
	local pid="$1"
	local i max

	max=$((WAIT_SECS * 2))
	for ((i = 0; i < max; i++)); do
		if ! kill -0 "$pid" 2>/dev/null; then
			wait "$pid" 2>/dev/null || true
			echo "error: tas_server exited during startup" >&2
			echo "       check libftd2xx (glibc), USB cable, and udev rules" >&2
			return 1
		fi
		if tas_port_open "${TAS_PORT}"; then
			log "tas_server alive — pid ${pid}, listening on tcp/${TAS_PORT}"
			log "leave this terminal open; Ctrl+C to stop"
			return 0
		fi
		sleep 0.5
	done

	if kill -0 "$pid" 2>/dev/null; then
		warn "tas_server pid ${pid} running but tcp/${TAS_PORT} not seen — continuing anyway"
		log "leave this terminal open; Ctrl+C to stop"
		return 0
	fi
	return 1
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	-v|--verbose) VERBOSE=1; shift ;;
	-q|--quiet)   VERBOSE=0; shift ;;
	-h|--help)    usage; exit 0 ;;
	*)            echo "unknown option: $1" >&2; usage >&2; exit 1 ;;
	esac
done

if ! source_aurix_env; then
	echo "env.sh not found — run: ${SCRIPT_DIR}/install-host-tools.sh --help" >&2
	exit 1
fi

if [[ -z "${ULMK_TAS_SERVER:-}" || ! -x "${ULMK_TAS_SERVER}" ]]; then
	echo "tas_server not configured in env.sh" >&2
	echo "re-run installer with: --tas-archive /path/to/DAS_*_linux_x64.deb" >&2
	exit 1
fi

if [[ "$(id -u)" -eq 0 && -n "${SUDO_USER:-}" && "${SUDO_USER}" != root ]]; then
	warn "running as root — prefer: ${SCRIPT_DIR}/start-tas.sh (udev rules)"
fi

existing="$(tas_pid)"
if [[ -n "$existing" ]]; then
	log "tas_server already running (${ULMK_TAS_SERVER})"
	report_tas_alive "$existing"
	exit 0
fi

log "starting: ${ULMK_TAS_SERVER}"
"${ULMK_TAS_SERVER}" &
tas_pid_val=$!

if [[ "$VERBOSE" -eq 1 ]]; then
	log "waiting for tas_server (pid ${tas_pid_val}, port ${TAS_PORT})..."
	wait_for_tas "$tas_pid_val"
else
	wait "$tas_pid_val"
fi

wait "$tas_pid_val"
