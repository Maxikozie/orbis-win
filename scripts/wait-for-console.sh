#!/usr/bin/env bash
# Poll until the console answers, then run preflight. For leaving up while the
# PS4 is still off.
#
#   scripts/wait-for-console.sh [interval-seconds]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
need_console
interval="${1:-30}"

say "waiting for $ORBIS_CONSOLE (every ${interval}s, Ctrl-C to stop)"
until ping -c1 -W2 "$ORBIS_CONSOLE" >/dev/null 2>&1; do
  sleep "$interval"
done
echo
say "$ORBIS_CONSOLE answered at $(date '+%H:%M:%S')"
echo
exec "$ROOT/scripts/preflight.sh"
