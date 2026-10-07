#!/usr/bin/env bash
# Capture console output. Start this BEFORE launching the title and leave it up.
#
#   scripts/logs.sh          UDP netlog on $PS4_LOG_PORT  (primary)
#   scripts/logs.sh klog     GoldHEN klog over TCP 3232   (fallback)
#
# UDP is primary because the console's stdout write BLOCKS the calling thread
# when nothing is draining GoldHEN's klog socket — seconds per line, inside a
# render loop. sceNetSendto is fire-and-forget and cannot stall the title.
# klog stays as the fallback: it is the only channel that shows output from
# before our code runs, and from panics the title never gets to log itself.
#
# There is no usable stdout on this platform — freopen fails — so if you have
# no listener you have no diagnosis.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
need_bundle
[[ "${1:-}" == klog ]] && need_console
mkdir -p "$PS4_LOG_DIR"

# ⚠ KLOG IS SINGLE-CLIENT, AND A SECOND RECEIVER GETS SILENCE RATHER THAN AN
# ERROR. Measured 2026-10-06: two receivers were connected at once (ss showed
# both ESTAB to :3232); the stale one held the stream and the new one captured
# nothing at all — not even the system chatter that normally never stops. That
# reads exactly like a dead console, and cost a run to diagnose.
if [[ "${1:-}" == klog ]]; then
  # `|| true`: a grep that matches nothing returns 1, and under `set -o pipefail`
  # that takes down the whole script. Same trap as preflight's banner read and
  # build.sh's symbol check — third time today, so it is written down here.
  holder=$(ss -tnp 2>/dev/null | grep ":$PS4_KLOG_PORT " | grep -oE 'pid=[0-9]+' | head -1 || true)
  if [[ -n $holder ]]; then
    echo "!! another receiver already holds klog (${holder}). It would starve this one." >&2
    echo "   kill it first:  kill ${holder#pid=}" >&2
    exit 1
  fi
fi

exec "$PS4_SCRIPTS/logs.sh" "$@"
