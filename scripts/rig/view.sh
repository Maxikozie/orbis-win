#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# From: Maxikozie/ps4-wine scripts/rig/view.sh @ fd5642b (GPL-3.0; relicensed LGPL-2.1-or-later by its author)
# view.sh: live view of the PS4's HDMI output from the capture stick, for the user.
# The video goes to mpv; the sound goes from the stick's PipeWire source straight to the
# speakers through pw-loopback. mpv's own external audio input lagged the picture by ~0.5 s
# (its capture backlog is never drained), so the two paths are kept apart.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

usage() {
    cat <<USAGE
usage: scripts/rig/view.sh [--device DEV] [--sink NAME] [--no-audio] [--latency MS] [--record FILE [--record-max S]]
       scripts/rig/view.sh --stop-record

  --device DEV   capture node (default PS4_CAPTURE_DEV, else $DEFAULT_DEV)
  --sink NAME    PipeWire sink for the sound (default PS4_AUDIO_SINK, else the default sink;
                 pactl list sinks short lists them)
  --no-audio     video only
  --latency MS   pw-loopback latency (default $LATENCY)
  --record FILE  also write the stick's stream to FILE as it arrives (MJPEG in Matroska, mpv's
                 --stream-record: the one reader of the stick records what it shows, and the view
                 stays as live as without it); for short-lived pictures, a frame is then taken
                 with ffmpeg -ss T -i FILE -frames:v 1 F.png. ~10 MB/s (~0.6 GB a minute): the
                 recording stops by itself after --record-max seconds (default $RECORD_MAX), and
                 earlier with --stop-record; the view goes on either way. With sound, the stick's
                 audio goes to FILE with .wav beside it (pw-record from its PipeWire source,
                 48 kHz S16 stereo), stopped with the recording.
  --record-max S the recording's limit in seconds
  --stop-record  stop the running viewer's recording now (through its mpv IPC socket), keep the view
Keys in mpv: f fullscreen, space pause, s screenshot (PNG into build-ps4-logs/), q quit.
Only one program can read the stick: quit this before shot.sh or check.sh captures.
Exit: mpv's, or 3 when the capture device does not exist.
USAGE
}

DEFAULT_DEV=/dev/v4l/by-id/usb-MACROSILICON_2109-video-index0
DEV="${PS4_CAPTURE_DEV:-$DEFAULT_DEV}"; SINK="${PS4_AUDIO_SINK:-}"; AUDIO=1; LATENCY=20; RECORD=""; RECORD_MAX=300
IPC="${XDG_RUNTIME_DIR:-/tmp}/ps4-view.sock"
AUDIO_PID_FILE="$IPC.audio-record.pid"   # the WAV sidecar's pw-record (phase p5)
# Clears mpv's stream-record through the IPC socket: the file is closed, the view goes on; the
# WAV sidecar's recorder ends with it.
stop_record() {
    if [ -f "$AUDIO_PID_FILE" ]; then kill "$(cat "$AUDIO_PID_FILE")" 2>/dev/null || true; rm -f "$AUDIO_PID_FILE"; fi
    python3 -I -c 'import socket, sys
s = socket.socket(socket.AF_UNIX)
s.connect(sys.argv[1])
s.sendall(b"{\"command\": [\"set_property\", \"stream-record\", \"\"]}\n")
s.recv(4096)
s.close()' "$IPC"
}
while [ $# -gt 0 ]; do
    case "$1" in
        --device) DEV="$2"; shift 2 ;;
        --sink) SINK="$2"; shift 2 ;;
        --no-audio) AUDIO=0; shift ;;
        --latency) LATENCY="$2"; shift 2 ;;
        --record) RECORD="$2"; shift 2 ;;
        --record-max) RECORD_MAX="$2"; shift 2 ;;
        --stop-record) stop_record || { log "no viewer is recording ($IPC)"; exit 1; }; log "recording stopped"; exit 0 ;;
        -h|--help) usage; exit 0 ;;
        *) usage; exit 1 ;;
    esac
done

need_cmd mpv
[ -e "$DEV" ] || { log "capture device $DEV does not exist; plug in the HDMI stick or pass --device"; exit 3; }

LOOP_PID=""; TIMER_PID=""; AREC_PID=""
cleanup() {
    [ -n "$LOOP_PID" ] && kill "$LOOP_PID" 2>/dev/null || true
    [ -n "$AREC_PID" ] && kill "$AREC_PID" 2>/dev/null || true
    rm -f "$AUDIO_PID_FILE"
    [ -n "$TIMER_PID" ] && kill "$TIMER_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

if [ "$AUDIO" = 1 ]; then
    SOURCE="$(pactl list sources short 2>/dev/null | awk '$2 ~ /MACROSILICON_2109/ && $2 !~ /\.monitor$/ { print $2; exit }')"
    if [ -n "$SOURCE" ] && command -v pw-loopback >/dev/null; then
        pw-loopback -n ps4-capture-audio -C "$SOURCE" ${SINK:+-P "$SINK"} -l "$LATENCY" >/dev/null 2>&1 &
        LOOP_PID=$!
    else
        log "no PipeWire source for the stick (or no pw-loopback): video only"
    fi
fi

mkdir -p "$LOGS_DIR"
# The stick's sound beside the recording, as RECORD with .wav (mpv's stream-record holds the video
# only: the view runs --no-audio). A second reader of the PipeWire source, not of the V4L2 node.
if [ -n "$RECORD" ] && [ "$AUDIO" = 1 ] && [ -n "${SOURCE:-}" ] && command -v pw-record >/dev/null; then
    pw-record --target "$SOURCE" --rate 48000 --channels 2 --format s16 "${RECORD%.*}.wav" >/dev/null 2>&1 &
    AREC_PID=$!
    echo "$AREC_PID" > "$AUDIO_PID_FILE"
    log "recording the stick's sound to ${RECORD%.*}.wav"
fi
if [ -n "$RECORD" ]; then
    (sleep "$RECORD_MAX"; stop_record 2>/dev/null && log "recording stopped after ${RECORD_MAX} s (--record-max)") &
    TIMER_PID=$!
fi
# Latency on top of the stick's own: gpu-next keeps up to three frames queued in a FIFO swapchain
# by default (~50 ms at 60 Hz); one queued frame, presented in mailbox mode (the newest frame
# replaces a waiting one, no tearing), shows each capture frame as soon as the compositor can.
mpv "av://v4l2:$DEV" \
    --demuxer-lavf-o=video_size=1920x1080,input_format=mjpeg,framerate=60 \
    --profile=low-latency --untimed --no-audio --title=PS4 \
    --swapchain-depth=1 --vulkan-swap-mode=mailbox \
    --screenshot-format=png --screenshot-directory="$LOGS_DIR" --input-ipc-server="$IPC" \
    ${RECORD:+--stream-record="$RECORD"}
