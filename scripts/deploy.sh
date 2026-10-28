#!/usr/bin/env bash
# Put a package on the console, without lftp.
#
#   scripts/deploy.sh {hello|triangle|probe|present|fault|wine-app|wine-app-base|lowbaseprobe|libkernelprobe|modprobe|statprobe|dupprobe|threadprobe|mapprobe|vkmapprobe|gfxhang|gfxhang-base}
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
  triangle-neo) TITLE_ID=GLDS10018 ;;
  gfxrepeat-neo-solid) TITLE_ID=GLDS10019 ;;
  gfxrepeat-neo-vbo) TITLE_ID=GLDS10020 ;;
  gfxrepeat-neo-index3) TITLE_ID=GLDS10021 ;;
  gfxrepeat-neo-lowidx) TITLE_ID=GLDS10022 ;;
  gfxrepeat-neo-index300) TITLE_ID=GLDS10023 ;;
  gfxrepeat-neo-index1266) TITLE_ID=GLDS10024 ;;
  gfxrepeat-neo-index783) TITLE_ID=GLDS10025 ;;
  gfxrepeat-neo-index541) TITLE_ID=GLDS10026 ;;
  gfxrepeat-neo-index662) TITLE_ID=GLDS10027 ;;
  gfxrepeat-neo-index723) TITLE_ID=GLDS10028 ;;
  gfxrepeat-neo-index753) TITLE_ID=GLDS10029 ;;
  gfxrepeat-neo-index768) TITLE_ID=GLDS10030 ;;
  gfxrepeat-neo-index776) TITLE_ID=GLDS10031 ;;
  gfxrepeat-neo-index772) TITLE_ID=GLDS10032 ;;
  gfxrepeat-neo-index770) TITLE_ID=GLDS10033 ;;
  gfxrepeat-neo-index769) TITLE_ID=GLDS10034 ;;
  gfxrepeat-neo-points257) TITLE_ID=GLDS10035 ;;
  gfxrepeat-neo-points256) TITLE_ID=GLDS10036 ;;
  gfxrepeat-base-points257) TITLE_ID=GLDS10037 ;;
  gfxrepeat-neo-wd-eop-points257) TITLE_ID=GLDS10038 ;;
  gfxrepeat-neo-no-ia-eoi-points257) TITLE_ID=GLDS10039 ;;
  gfxrepeat-neo-wd-eop-index769) TITLE_ID=GLDS10040 ;;
  probe)    TITLE_ID=GLDS10002 ;;
  present)  TITLE_ID=GLDS10003 ;;
  fault)    TITLE_ID=GLDS10004 ;;
  wine-app) TITLE_ID=GLDS10005 ;;
  wine-app-base) TITLE_ID=GLDS10041 ;;
  lowbaseprobe) TITLE_ID=GLDS10042 ;;
  libkernelprobe) TITLE_ID=ORBW00030 ;;
  controller-map) TITLE_ID=GLDS10048 ;;
  modprobe) TITLE_ID=GLDS10006 ;;
  statprobe) TITLE_ID=GLDS10007 ;;
  dupprobe) TITLE_ID=GLDS10008 ;;
  threadprobe) TITLE_ID=GLDS10009 ;;
  vkmapprobe) TITLE_ID=GLDS10011 ;;
  mapprobe) TITLE_ID=GLDS10010 ;;
  gfxhang) TITLE_ID=GLDS10012 ;;
  gfxhang-base) TITLE_ID=GLDS10013 ;;
  gfxonce) TITLE_ID=GLDS10014 ;;
  gfxonce-base) TITLE_ID=GLDS10015 ;;
  gfxrepeat-base) TITLE_ID=GLDS10016 ;;
  gfxrepeat-neo) TITLE_ID=GLDS10017 ;;
  *) die "usage: deploy.sh {hello|triangle|triangle-neo|probe|present|fault|wine-app|wine-app-base|lowbaseprobe|libkernelprobe|controller-map|modprobe|statprobe|dupprobe|threadprobe|mapprobe|vkmapprobe|gfxhang|gfxhang-base|gfxonce|gfxonce-base|gfxrepeat-base|gfxrepeat-neo|gfxrepeat-neo-solid|gfxrepeat-neo-vbo|gfxrepeat-neo-index3|gfxrepeat-neo-lowidx|gfxrepeat-neo-index300|gfxrepeat-neo-index1266|gfxrepeat-neo-index783|gfxrepeat-neo-index541|gfxrepeat-neo-index662|gfxrepeat-neo-index723|gfxrepeat-neo-index753}" ;;
esac

# readlink -f: make-pkg.sh leaves <TITLE_ID>.pkg as a symlink, and sizing the
# link instead of the file reports 40 bytes.
PKG_DIR="$ROOT/pkgout/$TARGET"
if [[ $TARGET == libkernelprobe ]]; then
  PKG_DIR="$ROOT/pkgout/orbis-native-companion-033"
fi
if [[ $TARGET == wine-app-base ]]; then
  PKG_DIR="$ROOT/pkgout/wine-app-variants/GLDS10041-base"
fi
pkg=$(readlink -f "$PKG_DIR/$TITLE_ID.pkg" 2>/dev/null) ||
  die "no package for $TARGET — run: ./scripts/build.sh $TARGET --pkg"
[[ -f $pkg ]] || die "no package for $TARGET — run: ./scripts/build.sh $TARGET --pkg"

# The launcher app must use the relocatable handoff: the ordinary low-address
# eboot collides with non-relocatable PE32 games such as Oblivion. Refuse to
# deploy an older or hand-staged package that silently lacks the high-address
# Wine PRX.
if [[ $TARGET == wine-app || $TARGET == wine-app-base ]]; then
  reloc_prx="$PKG_DIR/pkg-stage/sce_module/relocated_wine_app.prx"
  [[ -s $reloc_prx ]] || die "package stage lacks relocated_wine_app.prx — rebuild with ./scripts/build-app-ps4.sh --pkg"
fi

# ⚠ Refuse to upload a package older than what it is built from. deploy.sh
# only sends an existing .pkg, so forgetting to repackage sends a stale build
# and the console reports a failure that was already fixed. That cost two
# launches on 2026-10-07, chasing a segment-alignment bug that was corrected
# in the modules but never packaged.
if [[ $TARGET == wine-app || $TARGET == wine-app-base ]]; then
  newest=$(find "$ROOT/prospero-win/.deps/wine-ps5/prx/sce_module" \
                "$ROOT/prospero-win/.deps/wine-host/install/usr/lib/wine" \
                "$ROOT/src/native" -type f -newer "$pkg" -print -quit 2>/dev/null || true)
  if [[ -n $newest ]]; then
    if [[ $TARGET == wine-app ]]; then
      hint='./scripts/build-app-ps4.sh --pkg'
    else
      hint='TITLE_ID=GLDS10041 TITLE="Prospero Wine Base" PKG_OUT="$PWD/pkgout/wine-app-variants/GLDS10041-base" ./scripts/build-app-ps4.sh --pkg'
    fi
    die "package is older than $newest — rebuild it with: $hint"
  fi
fi

say "uploading $TARGET to $ORBIS_CONSOLE:$ORBIS_FTP_PORT"
exec python3 "$ROOT/scripts/ftp-put.py" \
  "$ORBIS_CONSOLE" "$ORBIS_FTP_PORT" "$pkg" \
  "/data/pkg/$TARGET.pkg" ${ORBIS_FTP_USER:-} ${ORBIS_FTP_PASS:-}
