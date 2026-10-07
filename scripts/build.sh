#!/usr/bin/env bash
# Build one of the bundle's two worked examples, optionally package and deploy it.
#
#   scripts/build.sh hello               layout only — touches no Mesa
#   scripts/build.sh triangle --pkg      the graphics stack, as an installable .pkg
#   scripts/build.sh triangle --pkg --deploy
#
# The split between the two is the point of having both. hello answers "is the
# bundle laid out correctly", with nothing else able to fail; triangle answers
# "does the graphics stack come up and reach the screen". One example mixing
# both reports a driver problem as a layout problem, and the reverse.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
need_bundle

# ⚠ The bundle warns "gate=unproven" on every configure even though BUNDLE.txt
# says gate=pass. orbis-sdk.cmake tests `MATCHES "gate=unproven"` against the
# WHOLE manifest, and the comment three lines above the value contains that
# exact string. Upstream bug, cosmetic, filtered here so a real warning stands out.
filter_gate_warning() {
  awk '/CMake Warning at .*orbis-sdk\.cmake:47/ {skip=1; next}
       skip && (/^Call Stack/ || /CMakeSystem\.cmake/ || /TryCompile/ || /^[[:space:]]*$/ || /^[[:space:]]+\//) {next}
       {skip=0; print}'
}

TARGET="${1:-}"; shift || true
case "$TARGET" in
  hello)    TITLE_ID=GLDS10000; TITLE="Gladius Hello";    SRC="$BUNDLE/examples/hello" ;;
  triangle) TITLE_ID=GLDS10001; TITLE="Gladius Triangle"; SRC="$BUNDLE/examples/triangle" ;;
  probe)    TITLE_ID=GLDS10002; TITLE="Gladius Probe";    SRC="$ROOT/src/probe" ;;
  present)  TITLE_ID=GLDS10003; TITLE="Prospero Presenter"; SRC="$ROOT/src/present-test" ;;
  *) die "usage: build.sh {hello|triangle|probe|present} [--pkg] [--deploy] [--neo] [--dev-mesa]" ;;
esac

PKG=0; DEPLOY=0; NEO=0; DEVMESA=0
for a in "$@"; do
  case "$a" in
    --pkg)    PKG=1 ;;
    --deploy) PKG=1; DEPLOY=1 ;;
    --neo)    NEO=1 ;;
    --dev-mesa) DEVMESA=1 ;;
    *) die "unknown option: $a" ;;
  esac
done

BUILD="$ROOT/build/$TARGET"
# shellcheck source=/dev/null
source "$BUNDLE/env.sh" >/dev/null

# ⚠ ORBIS_KIT_DIR. examples/triangle wants a directory holding vkloader/, and
# defaults to the bundle root, which has none — the example was written to sit
# inside an orbis-porting-kit checkout. The bundle carries the same vkloader at
# orbis-compat/vkloader, so point it there. Without this the configure fails
# with "holds no vkloader/".
EXTRA=()
[[ $TARGET == triangle || $TARGET == probe ]] &&
  EXTRA+=(-DORBIS_KIT_DIR="$BUNDLE/orbis-compat")

# --dev-mesa: link against the locally built mesa-ps4 instead of the bundle's
# prebuilt archive, for the Gladius work.
#
# ⚠ orbis-compat MOVES WITH IT, and that is the whole point of the bundle's
# pairing gate: Mesa compiled against one include/ tree and linked against
# another is a struct-layout bug that builds cleanly and fails at run time. The
# local Mesa was built against the local orbis-compat (HEAD), so both are
# overridden together or neither is.
if (( DEVMESA )); then
  DEV="$ROOT/mesa-build"
  [[ -f "$DEV/mesa-ps4/build-orbis/src/amd/vulkan/libvulkan_radeon.a" ]] ||
    die "no locally built RADV — run mesa-build/mesa-ps4/ps4/build.sh first"
  [[ -f "$DEV/orbis-compat/build/liborbis-compat.a" ]] ||
    die "no locally built orbis-compat — run mesa-build/orbis-compat/build.sh first"
  # ⚠ cmake/ LEFT orbis-compat FOR THE KIT ON 2026-09-18, but the bundle's
  # orbis-sdk.cmake still does include("${ORBIS_COMPAT_DIR}/cmake/..."). The
  # symlink lets ORBIS_COMPAT_DIR point at the HEAD overlay — which is the one
  # this Mesa was compiled against — without rewriting the bundle's toolchain.
  [[ -e "$DEV/orbis-compat/cmake" ]] ||
    ln -s "$DEV/orbis-porting-kit/cmake" "$DEV/orbis-compat/cmake"
  say "dev mesa: $DEV/mesa-ps4/build-orbis"
  EXTRA+=(-DORBIS_MESA_SRC="$DEV/mesa-ps4"
          -DORBIS_MESA_BUILD="$DEV/mesa-ps4/build-orbis"
          -DORBIS_COMPAT_DIR="$DEV/orbis-compat"
          -DORBIS_KIT_DIR="$DEV/orbis-porting-kit")
  BUILD="$ROOT/build/$TARGET-dev"
fi

say "configure $TARGET"
cmake -S "$SRC" -B "$BUILD" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$BUNDLE/toolchain/orbis-sdk.cmake" "${EXTRA[@]}" \
  2>&1 | filter_gate_warning

say "build"
cmake --build "$BUILD"

[[ -f "$BUILD/eboot.bin" ]] || die "no eboot.bin — create-fself did not run"

# The checks the toolchain file names as the ones that catch a bad link.
#
# ⚠ nm output is captured ONCE into a variable rather than piped into `grep -q`.
# Under `set -o pipefail`, grep -q exits on the first match, llvm-nm takes
# SIGPIPE, and the pipeline reports failure on a check that actually PASSED.
say "checking the image"
syms=$(llvm-nm "$BUILD/$TARGET")
entry=$(llvm-readelf -h "$BUILD/$TARGET" | sed -n 's/.*Entry point address: *//p')
start=0x$(printf '%s\n' "$syms" | sed -n 's/^0*\([0-9a-f]*\) [Tt] _start$/\1/p' | head -1)

[[ ${entry,,} == "${start,,}" ]] || die "e_entry $entry is not _start $start"
printf '%s\n' "$syms" | grep -F _sceProcessParam >/dev/null ||
  die "no _sceProcessParam — the process header is missing and the console will refuse this"

# Every PT_LOAD must be 0x4000-aligned. awk here is mawk on Debian, which has
# no strtonum, so the hex is converted in the shell.
bad=0
while read -r v; do
  (( (16#${v#0x}) % 16384 == 0 )) || { echo "!! unaligned PT_LOAD $v" >&2; bad=1; }
done < <(llvm-readelf -lW "$BUILD/$TARGET" | awk '/LOAD/{print $3}')
(( bad == 0 )) || die "PT_LOAD alignment"

printf '   entry %s == _start, _sceProcessParam present, PT_LOADs 0x4000-aligned\n' "$entry"
printf '   eboot.bin %s\n' "$(du -h "$BUILD/eboot.bin" | cut -f1)"
[[ $TARGET == triangle || $TARGET == probe ]] &&
  printf '   RADV/ACO symbols linked in: %s\n' "$(printf '%s\n' "$syms" | grep -cE ' (radv_|ac_drm_|aco_)')"

if (( PKG )); then
  say "package"
  OUT="$ROOT/pkgout/$TARGET"; mkdir -p "$OUT"

  # ⚠ sce_module/ MUST BE POPULATED OR THE TITLE NEVER REACHES main().
  # make-pkg.sh declares an empty sce_module directory in the gp4 and puts
  # nothing in it. The console refuses the process before it runs:
  #
  #   # exception: 0xa0020102 (PRX_SCE_MODULE_LOAD_ERROR)
  #   # === Lack of a .prx file in /app0/sce_module is detected!!! ===
  #
  # These are Sony firmware modules. The SDK cannot ship them and neither can
  # this repository — see sce_module/README.md for taking them off your own
  # console. They are gitignored.
  EXTRA_FILES=()
  for m in libc.prx libSceFios2.prx; do
    [[ -f "$ROOT/sce_module/$m" ]] ||
      die "missing sce_module/$m — see sce_module/README.md. Without it the title crashes with PRX_SCE_MODULE_LOAD_ERROR before main()."
    EXTRA_FILES+=(--extra "$ROOT/sce_module/$m:sce_module/$m")
  done

  "$PS4_SCRIPTS/make-pkg.sh" --eboot "$BUILD/eboot.bin" --out-dir "$OUT" \
      --title-id "$TITLE_ID" --title "$TITLE" "${EXTRA_FILES[@]}"

  # ⚠ NEO MODE. make-pkg.sh writes ATTRIBUTE=0, and a PS4 Pro runs any title
  # without ATTRIBUTE bit 24 (0x00800000) as a base PS4: 18 CUs, device id
  # 0x9920, Liverpool. Measured on hardware 2026-10-06 — see docs/FINDINGS.md.
  #
  # make-pkg.sh takes no --attribute, so the staged SFO is edited afterwards and
  # the package rebuilt from the gp4 make-pkg.sh already wrote. Reusing its gp4
  # rather than composing our own keeps the file list in one place.
  if (( NEO )); then
    say "neo mode: ATTRIBUTE=0x00800000"
    STAGE="$OUT/pkg-stage"
    PKGTOOL="$BUNDLE/sdk/bin/linux/PkgTool.Core"
    export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
    ossl="${XDG_CACHE_HOME:-$HOME/.cache}/orbis-ports/openssl11/lib"
    [[ -d $ossl ]] && export LD_LIBRARY_PATH="$ossl${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

    "$PKGTOOL" sfo_setentry "$STAGE/sce_sys/param.sfo" ATTRIBUTE \
        --type Integer --maxsize 4 --value 8388608 > /dev/null
    # ⚠ READ IT BACK. PkgTool.Core has already been seen to fail and exit 0
    # ("No usable version of libssl was found"), so a silent no-op here would
    # produce a package that looks Neo-enabled and is not.
    "$PKGTOOL" sfo_listentries "$STAGE/sce_sys/param.sfo" 2>/dev/null |
      grep -i attribute || true
    ( cd "$STAGE" && "$PKGTOOL" pkg_build pkg.gp4 . > pkg-build-neo.log 2>&1 ) ||
      { cat "$STAGE/pkg-build-neo.log" >&2; die "neo repackage failed"; }
    cp -f "$STAGE"/*.pkg "$OUT"/ 2>/dev/null || true
    say "repackaged with neo attribute"
  fi
fi

if (( DEPLOY )); then
  need_console
  say "deploy to $ORBIS_CONSOLE"
  # ⚠ RESOLVE THE SYMLINK. make-pkg.sh leaves <TITLE_ID>.pkg as a link to the
  # real IV0000-...pkg, and deploy.sh sizes what it is given with `stat -c%s` —
  # which reports 40 bytes for the link. Its whole verification is "compare what
  # arrived against what was sent", so a 40-byte local size makes that check
  # meaningless on exactly the step it exists to protect.
  pkg=$(readlink -f "$ROOT/pkgout/$TARGET/$TITLE_ID.pkg")
  "$PS4_SCRIPTS/deploy.sh" --pkg "$pkg" --name "$TARGET" --host "$ORBIS_CONSOLE"
fi
