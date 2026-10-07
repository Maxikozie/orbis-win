#!/usr/bin/env bash
# Put a package on the console, without lftp.
#
#   scripts/deploy.sh {hello|triangle|probe}
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
  *) die "usage: deploy.sh {hello|triangle|probe|present}" ;;
esac

# readlink -f: make-pkg.sh leaves <TITLE_ID>.pkg as a symlink, and sizing the
# link instead of the file reports 40 bytes.
pkg=$(readlink -f "$ROOT/pkgout/$TARGET/$TITLE_ID.pkg" 2>/dev/null) ||
  die "no package for $TARGET — run: ./scripts/build.sh $TARGET --pkg"
[[ -f $pkg ]] || die "no package for $TARGET — run: ./scripts/build.sh $TARGET --pkg"

say "uploading $TARGET to $ORBIS_CONSOLE:$ORBIS_FTP_PORT"
exec python3 "$ROOT/scripts/ftp-put.py" \
  "$ORBIS_CONSOLE" "$ORBIS_FTP_PORT" "$pkg" \
  "/data/pkg/$TARGET.pkg" ${ORBIS_FTP_USER:-} ${ORBIS_FTP_PASS:-}
