# Sourced by every script here. Resolves the repo, the bundle and the console.
# shellcheck shell=bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
BUNDLE="$ROOT/sdk/orbis-sdk-v1"
PS4_SCRIPTS="$BUNDLE/orbis-compat/scripts/ps4"

# config.env is gitignored and holds the console address. Absent is fine for
# anything that does not touch the console.
# shellcheck source=/dev/null
[[ -f "$ROOT/config.env" ]] && source "$ROOT/config.env"

export ORBIS_CONSOLE="${ORBIS_CONSOLE:-}"
export ORBIS_FTP_PORT="${ORBIS_FTP_PORT:-2121}"
export PS4_IP="${ORBIS_CONSOLE:-}"
export PS4_LOG_PORT="${PS4_LOG_PORT:-18194}"
export PS4_KLOG_PORT="${PS4_KLOG_PORT:-3232}"
export PS4_LOG_DIR="${PS4_LOG_DIR:-$ROOT/build-ps4-logs}"

die() { echo "!! $*" >&2; exit 1; }
say() { echo "== $*"; }

need_bundle() {
  [[ -f "$BUNDLE/BUNDLE.txt" ]] ||
    die "no SDK bundle at $BUNDLE — run scripts/fetch-sdk.sh first"
}

need_console() {
  [[ -n "$ORBIS_CONSOLE" ]] ||
    die "ORBIS_CONSOLE is not set — copy config.env.example to config.env and put your PS4's IP in it"
}
