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
exec "$PS4_SCRIPTS/logs.sh" "$@"
