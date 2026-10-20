#!/usr/bin/env bash
# GoldHEN may close its single-client klog connection after flushing the
# buffered log. Reconnect serially and append every snapshot to one file so a
# closed TCP stream does not silently leave later runs without klog capture.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
need_bundle
need_console
mkdir -p "$PS4_LOG_DIR"
OUT="${PS4_LOG_DIR}/ps4-klog-$(date +%Y%m%d-%H%M%S)-watch.log"
RECEIVER="$PS4_SCRIPTS/log-receiver.py"
echo "klog reconnect watcher appending to $OUT"
while true; do
  python3 "$RECEIVER" klog --host "$PS4_IP" --port "$PS4_KLOG_PORT" --out "$OUT" || true
  sleep 10
done
