#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# From: Maxikozie/ps4-wine scripts/ps4/common.sh @ fd5642b (GPL-3.0; relicensed LGPL-2.1-or-later by its author),
# the part the capture scripts use. Shared settings for scripts/rig/*.sh. Source it; do not run it.
# Every value can be overridden from the environment; the console address comes from config.env (ORBIS_CONSOLE),
# like scripts/common.sh.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
# shellcheck source=/dev/null
[ -f "$REPO_ROOT/config.env" ] && . "$REPO_ROOT/config.env"

PS4_HOST="${PS4_HOST:-${ORBIS_CONSOLE:-192.168.8.184}}"
PS4_FTP_PORT="${PS4_FTP_PORT:-${ORBIS_FTP_PORT:-2121}}"
# Logs and screenshots go where scripts/common.sh puts the console logs (gitignored by build-*/).
LOGS_DIR="${LOGS_DIR:-${PS4_LOG_DIR:-$REPO_ROOT/build-ps4-logs}}"

# Capture stick: set PS4_CAPTURE_DEV to a /dev/videoN to skip auto-detection.
PS4_CAPTURE_DEV="${PS4_CAPTURE_DEV:-}"
PS4_CAPTURE_EXCLUDE="${PS4_CAPTURE_EXCLUDE:-HP True Vision}"   # the laptop webcam

log()  { printf '%s %s\n' "$(date +%H:%M:%S)" "$*" >&2; }
die()  { log "error: $*"; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }
need_cmd() { have "$1" || die "missing command: $1"; }

# Short commit of the repo (or of DIR), plus +dirty.
build_id() {
    local dir="${1:-$REPO_ROOT}" id
    id="$(git -C "$dir" rev-parse --short=7 HEAD 2>/dev/null || echo nogit)"
    if [ "$id" != nogit ] && ! git -C "$dir" diff --quiet HEAD -- 2>/dev/null; then id="${id}+dirty"; fi
    printf '%s' "$id"
}

timestamp() { date +%Y%m%d-%H%M%S; }
