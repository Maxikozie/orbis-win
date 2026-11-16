#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# The read-only reference checkouts under refs/ (ignored through .git/info/exclude).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REFS="$ROOT/refs"

# name | clone URL | branch
LIST='
ps4-wine                 https://github.com/Maxikozie/ps4-wine.git                  main
prospero-win-maxi        https://github.com/Maxikozie/prospero-win.git              orbis
prospero-win             https://github.com/mpereiraesaa/prospero-win               main
prospero-win-leo         https://github.com/leolobato/prospero-win.git              main
prospero-win-profiles    https://github.com/mpereiraesaa/prospero-win-profiles.git  main
playden-ps5              https://github.com/leolobato/playden-ps5                   main
playden-ps4              https://github.com/plastikman/playden-ps4.git              main
mesa-ps4                 https://github.com/orbis-ports/mesa-ps4                    orbis
OpenOrbis-PS4-Toolchain  https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain       master
orbis-compat             https://github.com/orbis-ports/orbis-compat                master
orbis-porting-kit        https://github.com/orbis-ports/orbis-porting-kit           main
ps4-mesa-docs            https://github.com/orbis-ports/ps4-mesa-docs               main
nx-on-orbis              https://github.com/alechurri/nx-on-orbis                   main
elfldr                   https://github.com/Al-Azif/elfldr                          master
ps4-payload-sdk          https://github.com/Scene-Collective/ps4-payload-sdk        main
ps5-mcp-leo              https://github.com/leolobato/ps5-mcp.git                   main
'

usage() {
    cat <<EOF
usage: scripts/rig/refs.sh [--status] [--copy-from DIR] [--clone] [--update] [--only NAME]

refs/ holds read-only reference checkouts. Nothing here edits a file inside them.

  --status         (default) each ref: local HEAD against its remote branch (git ls-remote)
  --copy-from DIR  reflink-copy missing refs that exist as DIR/<name> (btrfs: instant, no network)
  --clone          clone the refs that are still missing
  --update         fetch each ref and fast-forward its branch; refuses a dirty tree or another branch
  --only NAME      act on one ref
  -h, --help       this text
EOF
}

STATUS=0 COPY_FROM="" CLONE=0 UPDATE=0 ONLY=""
[ $# -eq 0 ] && STATUS=1
while [ $# -gt 0 ]; do
    case "$1" in
        --status) STATUS=1; shift ;;
        --copy-from) COPY_FROM="${2:?--copy-from needs a directory}"; shift 2 ;;
        --clone) CLONE=1; shift ;;
        --update) UPDATE=1; shift ;;
        --only) ONLY="${2:?--only needs a name}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "refs.sh: unknown argument $1" >&2; usage >&2; exit 2 ;;
    esac
done

mkdir -p "$REFS"
rc=0

each() {
    local name url branch
    while read -r name url branch; do
        [ -n "$name" ] || continue
        [ -z "$ONLY" ] || [ "$ONLY" = "$name" ] || continue
        "$@" "$name" "$url" "$branch"
    done <<<"$LIST"
}

do_copy() {
    local name="$1" dir="$REFS/$1"
    [ -e "$dir" ] && return 0
    [ -d "$COPY_FROM/$name/.git" ] || return 0
    echo "copy   $name <- $COPY_FROM/$name"
    command cp -a --reflink=auto "$COPY_FROM/$name" "$dir"
}

do_clone() {
    local name="$1" url="$2" branch="$3" dir="$REFS/$1"
    [ -e "$dir" ] && return 0
    echo "clone  $name ($url $branch)"
    git clone -q --branch "$branch" "$url" "$dir" || { echo "clone  $name FAILED" >&2; rc=1; }
}

do_update() {
    local name="$1" branch="$3" dir="$REFS/$1" cur
    [ -d "$dir/.git" ] || { echo "update $name: missing"; return 0; }
    cur="$(git -C "$dir" rev-parse --abbrev-ref HEAD)"
    if [ "$cur" != "$branch" ]; then echo "update $name: on $cur, not $branch; skipped" >&2; rc=1; return 0; fi
    if [ -n "$(git -C "$dir" status --porcelain)" ]; then echo "update $name: dirty tree; skipped" >&2; rc=1; return 0; fi
    git -C "$dir" fetch -q origin \
        && git -C "$dir" merge -q --ff-only "origin/$branch" \
        && echo "update $name: $(git -C "$dir" log -1 --format='%h %ad %s' --date=short)" \
        || { echo "update $name: FAILED" >&2; rc=1; }
}

do_status() {
    local name="$1" url="$2" branch="$3" dir="$REFS/$1" loc rem state
    if [ ! -d "$dir/.git" ]; then printf '%-24s MISSING\n' "$name"; rc=1; return 0; fi
    loc="$(git -C "$dir" rev-parse HEAD)"
    rem="$(timeout 30 git ls-remote "$url" "refs/heads/$branch" 2>/dev/null | cut -f1 || true)"
    if [ -z "$rem" ]; then state="remote unreachable"; rc=1
    elif [ "$loc" = "$rem" ]; then state="up to date"
    elif git -C "$dir" merge-base --is-ancestor "$loc" "$rem" 2>/dev/null; then state="behind (--update)"; rc=1
    else state="DIFFERS from remote"; rc=1
    fi
    printf '%-24s %-8s %s  %s  %s\n' "$name" "$branch" "${loc:0:7}" "$(git -C "$dir" log -1 --format=%ad --date=short)" "$state"
}

[ -n "$COPY_FROM" ] && each do_copy
[ "$CLONE" = 1 ] && each do_clone
[ "$UPDATE" = 1 ] && each do_update
[ "$STATUS" = 1 ] && each do_status
exit "$rc"
