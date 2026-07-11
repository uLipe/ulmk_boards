#!/usr/bin/env bash
# tc275_lite/scripts/aurix-env.sh — resolve ~/.local/aurix/env.sh (works under sudo).

resolve_aurix_prefix() {
	local home

	if [[ -n "${ULMK_AURIX_PREFIX:-}" && -f "${ULMK_AURIX_PREFIX}/env.sh" ]]; then
		printf '%s\n' "${ULMK_AURIX_PREFIX}"
		return 0
	fi
	if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != root ]]; then
		home="$(getent passwd "${SUDO_USER}" 2>/dev/null | cut -d: -f6)"
		if [[ -n "${home}" && -f "${home}/.local/aurix/env.sh" ]]; then
			printf '%s\n' "${home}/.local/aurix"
			return 0
		fi
	fi
	if [[ -f "${HOME}/.local/aurix/env.sh" ]]; then
		printf '%s\n' "${HOME}/.local/aurix"
		return 0
	fi
	return 1
}

source_aurix_env() {
	local prefix

	prefix="$(resolve_aurix_prefix)" || return 1
	# shellcheck source=/dev/null
	source "${prefix}/env.sh"
}
