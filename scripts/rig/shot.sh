#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# From: Maxikozie/ps4-wine scripts/rig/shot.sh @ fd5642b (GPL-3.0; relicensed LGPL-2.1-or-later by its author)
# shot.sh: grab one frame of the PS4's HDMI output from the USB capture stick,
# or analyse an existing image. Pixel checks are the scriptable part of the
# milestone gates that need a picture (b, e).
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

usage() {
    cat <<USAGE
usage: scripts/rig/shot.sh [--out FILE] [--device /dev/videoN] [--label L]
                           [--pixel X,Y] [--expect-rgb R,G,B] [--tol N]
       scripts/rig/shot.sh --in FILE --pixel X,Y [--expect-rgb R,G,B] [--tol N]
       scripts/rig/shot.sh --list

  --out FILE          where to save the PNG (default build-ps4-logs/<timestamp>-L.png, L = shot)
  --device DEV        capture node; default: PS4_CAPTURE_DEV, else the first V4L2 capture
                      device whose name does not match "$PS4_CAPTURE_EXCLUDE"
  --in FILE           analyse this image instead of capturing
  --pixel X,Y         print "rgb=R,G,B" of that pixel
  --expect-rgb R,G,B  with --pixel: exit 0 if every channel is within --tol (default 40), else 1
  --list              list candidate capture devices
Captures are pinned to MJPEG at ${CAPTURE_SIZE} (the PS4's output size), so --pixel 960,540 is
the frame centre; a capture of any other size is refused (exit 4) rather than compared.
Exit: 0 ok / pixel matched, 1 pixel mismatch, 3 no capture device, 4 capture failed.
USAGE
}

# Pinned, not negotiated: capture sticks default to whatever mode they like (often 640x480 YUYV),
# and the milestone gates name pixels of a 1920x1080 frame.
CAPTURE_FORMAT=mjpeg
CAPTURE_SIZE=1920x1080
CAPTURE_RATE=60
# The MS2109 stick (345f:2109) sends black for ~105 frames (1.7 s) after the stream opens
# (measured 2026-11-01): keep the frame after 2.5 s.
CAPTURE_SKIP=150

OUT=""; DEV="$PS4_CAPTURE_DEV"; IN=""; PIXEL=""; EXPECT=""; TOL=40; LABEL=shot; LIST=0
while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT="$2"; shift 2 ;;
        --device) DEV="$2"; shift 2 ;;
        --in) IN="$2"; shift 2 ;;
        --label) LABEL="$2"; shift 2 ;;
        --pixel) PIXEL="$2"; shift 2 ;;
        --expect-rgb) EXPECT="$2"; shift 2 ;;
        --tol) TOL="$2"; shift 2 ;;
        --list) LIST=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) usage; exit 1 ;;
    esac
done

# List "name: /dev/videoN" for every capture-capable node.
list_devices() {
    need_cmd v4l2-ctl
    local name="" line
    while IFS= read -r line; do
        case "$line" in
            "") ;;
            [[:space:]]*) local node; node="$(echo "$line" | tr -d '[:space:]')"
                case "$node" in /dev/video*)
                    if v4l2-ctl -d "$node" --list-formats 2>/dev/null | grep -qE '^\s+\[[0-9]+\]'; then
                        printf '%s: %s\n' "$name" "$node"
                    fi ;;
                esac ;;
            *) name="${line%%:*}" ;;
        esac
    done < <(v4l2-ctl --list-devices 2>/dev/null || true)
}

if [ "$LIST" = 1 ]; then list_devices; exit 0; fi

if [ -z "$IN" ]; then
    need_cmd ffmpeg
    if [ -z "$DEV" ]; then
        DEV="$(list_devices | { grep -v -- "$PS4_CAPTURE_EXCLUDE" || true; } | head -n1 | sed 's/.*: //')"
        [ -n "$DEV" ] || { log "no capture device found (excluding '$PS4_CAPTURE_EXCLUDE'); plug in the HDMI stick or set PS4_CAPTURE_DEV"; list_devices >&2 || true; exit 3; }
    fi
    [ -e "$DEV" ] || { log "capture device $DEV does not exist"; exit 3; }
    mkdir -p "$LOGS_DIR"
    OUT="${OUT:-$LOGS_DIR/$(timestamp)-$LABEL.png}"
    # Skip the first frames: capture sticks return garbage or black until their pipeline settles.
    if ! ffmpeg -loglevel error -y -f v4l2 -input_format "$CAPTURE_FORMAT" -video_size "$CAPTURE_SIZE" \
            -framerate "$CAPTURE_RATE" -i "$DEV" \
            -vf "select=gte(n\,$CAPTURE_SKIP)" -frames:v 1 -update 1 "$OUT"; then
        log "capture from $DEV ($CAPTURE_FORMAT $CAPTURE_SIZE) failed; v4l2-ctl -d $DEV --list-formats-ext lists what it offers"; exit 4
    fi
    SIZE="$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=s=x:p=0 "$OUT" 2>/dev/null || echo '?x?')"
    log "saved $OUT from $DEV ($CAPTURE_FORMAT $SIZE)"
    [ "$SIZE" = "$CAPTURE_SIZE" ] || { log "capture is $SIZE, not $CAPTURE_SIZE: pixel coordinates would be wrong"; exit 4; }
    IN="$OUT"
fi

if [ -n "$PIXEL" ]; then
    X="${PIXEL%,*}"; Y="${PIXEL#*,}"
    read -r R G B < <(ffmpeg -loglevel error -i "$IN" -vf "crop=1:1:$X:$Y" -f rawvideo -pix_fmt rgb24 - | od -An -tu1 | head -n1)
    [ -n "${B:-}" ] || { log "could not read pixel $PIXEL from $IN"; exit 4; }
    printf 'rgb=%s,%s,%s pixel=%s,%s file=%s\n' "$R" "$G" "$B" "$X" "$Y" "$IN"
    if [ -n "$EXPECT" ]; then
        IFS=, read -r ER EG EB <<< "$EXPECT"
        d() { local v=$(( $1 - $2 )); echo "${v#-}"; }
        if [ "$(d "$R" "$ER")" -le "$TOL" ] && [ "$(d "$G" "$EG")" -le "$TOL" ] && [ "$(d "$B" "$EB")" -le "$TOL" ]; then
            echo "pixel-match expected=$EXPECT tol=$TOL"; exit 0
        else
            echo "pixel-mismatch expected=$EXPECT tol=$TOL"; exit 1
        fi
    fi
fi
