#!/usr/bin/env bash
# Put a package on the console, without lftp.
#
#   scripts/deploy.sh {hello|triangle|probe|present|fault|wine-app|modprobe|statprobe|dupprobe|threadprobe|mapprobe|vkmapprobe}
#
# The bundle's own deploy.sh drives lftp, and lftp's login sequence kills
# GoldHEN's FTP server — see docs/FINDINGS.md. scripts/ftp-put.py sends only
# what STOR requires and attempts no login at all.
#
# ⚠ /data/pkg AND NOWHERE ELSE. This console installs packages only from there.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
need_console

TARGET="${1:-}"
case "$TARGET" in
  hello)    TITLE_ID=GLDS10000 ;;
  triangle) TITLE_ID=GLDS10001 ;;
  probe)    TITLE_ID=GLDS10002 ;;
  present)  TITLE_ID=GLDS10003 ;;
  fault)    TITLE_ID=GLDS10004 ;;
  wine-app) TITLE_ID=GLDS10005 ;;
  modprobe) TITLE_ID=GLDS10006 ;;
  statprobe) TITLE_ID=GLDS10007 ;;
  dupprobe) TITLE_ID=GLDS10008 ;;
  threadprobe) TITLE_ID=GLDS10009 ;;
  vkmapprobe) TITLE_ID=GLDS10011 ;;
  mapprobe) TITLE_ID=GLDS10010 ;;
  *) die "usage: deploy.sh {hello|triangle|probe|present|fault|wine-app|modprobe|statprobe|dupprobe|threadprobe|mapprobe|vkmapprobe}" ;;
esac

# readlink -f: make-pkg.sh leaves <TITLE_ID>.pkg as a symlink, and sizing the
# link instead of the file reports 40 bytes.
pkg=$(readlink -f "$ROOT/pkgout/$TARGET/$TITLE_ID.pkg" 2>/dev/null) ||
  die "no package for $TARGET — run: ./scripts/build.sh $TARGET --pkg"
[[ -f $pkg ]] || die "no package for $TARGET — run: ./scripts/build.sh $TARGET --pkg"

# ⚠ Refuse to upload a package older than what it is built from. deploy.sh
# only sends an existing .pkg, so forgetting to repackage sends a stale build
# and the console reports a failure that was already fixed. That cost two
# launches on 2026-10-07, chasing a segment-alignment bug that was corrected
# in the modules but never packaged.
if [[ $TARGET == wine-app ]]; then
  newest=$(find "$ROOT/prospero-win/.deps/wine-ps5/prx/sce_module" "$ROOT/src/native" \
                -type f -newer "$pkg" -print -quit 2>/dev/null || true)
  [[ -z $newest ]] || die "package is older than $newest — run ./scripts/build-app-ps4.sh --pkg --neo first"
fi

say "uploading $TARGET to $ORBIS_CONSOLE:$ORBIS_FTP_PORT"
exec python3 "$ROOT/scripts/ftp-put.py" \
  "$ORBIS_CONSOLE" "$ORBIS_FTP_PORT" "$pkg" \
  "/data/pkg/$TARGET.pkg" ${ORBIS_FTP_USER:-} ${ORBIS_FTP_PASS:-}
