#!/usr/bin/env bash
# Build the prospero-win title itself for PS4: the launcher and DBT from src/,
# the portable half of native/, this port's platform layer, linked against the
# Wine PRX modules that scripts/prx-ps4.sh produces.
#
# Upstream builds this with tools/build_native.sh against
# .deps/ps5-native-app-boilerplate, which is PS5-only; this is the PS4
# equivalent, using our own eboot + .pkg pipeline.
#
# ⚠ Run scripts/prx-ps4.sh first: the title links against prx/stubs/ntdll.so and
# the package carries the eight modules in sce_module/.
set -euo pipefail
ROOT=/home/zpeirce/ps4-gladius
PW=$ROOT/prospero-win
BUNDLE=$ROOT/sdk/orbis-sdk-v1
CC=$ROOT/build/ps4-wine-sdk/bin/prospero-clang
PRX=$PW/.deps/wine-ps5/prx
OBJ=$ROOT/build/wine-app/obj
TITLE_ID=GLDS10005
TITLE="Prospero Wine"
export OO_PS4_TOOLCHAIN=$BUNDLE/sdk
PKG=0; NEO=0
for a in "$@"; do case $a in --pkg) PKG=1;; --neo) NEO=1;; esac; done
say(){ printf '== %s\n' "$*"; }

rm -rf "$OBJ"; mkdir -p "$OBJ"
INC="-I$PW -I$PW/include -I$PW/src -I$PW/native -I$ROOT/src/native"
# ⚠ ps5log is a single-header library whose network backend is chosen by what
# is included BEFORE it (ps5log.h guards every hook with #ifndef). Nothing in
# the title includes ps5log_ps5_net.h, so it compiled against POSIX
# socket/connect/fcntl -- which on Orbis are not sceNet, so the channel opened
# a socket that could never connect and then disabled itself in silence.
# Measured 2026-10-07: ps5log.o imported plain socket/connect/fcntl.
# Force-include the sceNet adapters so the hooks resolve to sceNet* instead.
PS5LOG_NET="-include $PW/native/ps5log/ps5log_ps5_net.h"
CFL="-std=gnu11 -O2 -fPIC $INC $PS5LOG_NET"

say "compile: portable core (DBT + launcher) and the portable half of native/"
for f in "$PW"/src/*.c "$PW"/native/wine64_main.c "$PW"/native/pw_data_mount.c \
         "$PW"/native/pw_diagnostics.c "$PW"/native/pw_wine_display.c \
         "$PW"/native/pw_wine_library.c "$PW"/native/pw_wine_prefix.c "$PW"/native/ps5log/*.c; do
  [ -f "$f" ] || continue
  $CC $CFL -c "$f" -o "$OBJ/$(echo "${f#$PW/}" | tr '/' '_' | sed 's/\.c$/.o/')"
done

say "compile: upstream platform cores, PS5 vtables guarded out"
$CC $CFL -DPW_PAD_PS5_HOST_TEST   -c "$PW/native/pw_pad_ps5.c"   -o "$OBJ/core_pad.o"
$CC $CFL -DPW_AUDIO_PS5_HOST_TEST -c "$PW/native/pw_audio_ps5.c" -o "$OBJ/core_audio.o"
$CC $CFL -DPW_HID_PS5_HOST_TEST   -c "$PW/native/pw_hid_ps5.c"   -o "$OBJ/core_hid.o"
# Those builds keep platform_ops as a stub returning PW_ERR_UNSUPPORTED, so the
# symbol still exists and would collide. Rename it aside and let the binding
# file below supply the real vtable.
llvm-objcopy --redefine-sym pw_pad_ps5_platform_ops=pw_pad_ps5_platform_ops_unused     "$OBJ/core_pad.o"
llvm-objcopy --redefine-sym pw_audio_ps5_platform_ops=pw_audio_ps5_platform_ops_unused "$OBJ/core_audio.o"

# The PRX loader belongs to the TITLE, not to a module: it is what loads the
# modules, so it cannot be imported from one.
say "compile: the PRX loader"
$CC $CFL -I"$PW/wine/ps5" -c "$PW/wine/ps5/pw_wine_prx.c" -o "$OBJ/pw_wine_prx.o"

say "compile: this port's platform layer"
for f in "$ROOT"/src/native/pw_videoout_ps4.c "$ROOT"/src/native/pw_pad_ps4.c \
         "$ROOT"/src/native/pw_audio_ps4.c "$ROOT"/src/native/pw_platform_bind_ps4.c; do
  $CC $CFL -c "$f" -o "$OBJ/ps4_$(basename "$f" .c).o"
done

say "link"
# ⚠ DO NOT LINK AGAINST ntdll. The title resolves Wine's entry points at RUNTIME
# through its own PRX loader (src/pw_wine_start.c: pw_prx_lookup(descriptor,
# "__wine_main")), so a link-time dependency is wrong in both directions:
#
#   - the console does not auto-load /app0/sce_module/*.prx for a DT_NEEDED on
#     a non-Sony module, so the title died at load with
#     0xa0020101 PRX_NOT_RESOLVED_FUNCTION having loaded none of our nine;
#   - every PRX carries orbis-compat and libc++ whole-archive, so linking the
#     stub silently bound the title's 212 C++ runtime imports (_ZNSt3__1...,
#     _Unwind_*) to ntdll.prx instead of to the SDK's own runtime.
#
# Measured on hardware 2026-10-07. The title must depend on Sony libraries only.
$CC -o "$OBJ/../wine_app.elf" "$OBJ"/*.o \
    -lSceNet -lSceAudioOut -lSceSystemService -lSceVideoOut -lScePad -lSceUserService
printf '   elf %s\n' "$(du -h "$OBJ/../wine_app.elf" | cut -f1)"

say "eboot"
"$BUNDLE/sdk/bin/linux/create-fself" -in "$OBJ/../wine_app.elf" \
    -out "$OBJ/../wine_app.oelf" -eboot "$OBJ/../eboot.bin" >/dev/null
printf '   eboot.bin %s\n' "$(du -h "$OBJ/../eboot.bin" | cut -f1)"

if (( PKG )); then
  say "package"
  OUT=$ROOT/pkgout/wine-app; mkdir -p "$OUT"
  EXTRA=()
  for m in libc.prx libSceFios2.prx; do
    [ -f "$ROOT/sce_module/$m" ] || { echo "missing sce_module/$m — see sce_module/README.md" >&2; exit 1; }
    EXTRA+=(--extra "$ROOT/sce_module/$m:sce_module/$m")
  done
  # ⚠ dev.conf must be in the PACKAGE, at /app0/dev.conf. ps5log reads its
  # destination in main() BEFORE pw_hid_ps5_preload, which wine64_main.c marks
  # as running "before the /data grant changes the title's credentials" -- so
  # /data/homebrew/dev.conf is not yet readable and the title logs nowhere.
  # Measured 2026-10-07: a copy at /data/homebrew alone produced total silence.
  # The file is gitignored; it holds this workstation's address.
  [ -f "$ROOT/build/devconf/dev.conf" ] && EXTRA+=(--extra "$ROOT/build/devconf/dev.conf:dev.conf")

  # ⚠ The Wine modules go to win/wine/lib/wine/x86_64-unix, NOT sce_module.
  # wine64_main.c:64 PW_WINE64_RUNTIME "/win/wine/lib/wine/x86_64-unix", searched
  # under /app0 and the sandbox app0. Measured 2026-10-07: with them in
  # sce_module the title found the library and listed the game, then failed to
  # start it with
  #     runtime not at /app0/win/wine/lib/wine/x86_64-unix
  #     load status=-2147352574 stage=1 module=0x80020002   (ENOENT)
  # sce_module keeps Sony's libc.prx and libSceFios2.prx, which the loader does
  # require there.
  for m in "$PRX"/sce_module/*.prx; do
    EXTRA+=(--extra "$m:win/wine/lib/wine/x86_64-unix/$(basename "$m")")
    # ⚠ AND in sce_module. The title loads ntdll by explicit path, but once Wine
    # is running its own dlopen goes through wine/ps5/pw_wine_dl_libc.c, which
    # searches WINE_PRX_DIR and falls back to "/app0/sce_module". Nothing sets
    # that variable, so server_init_process could not find wineserver.prx and
    # Wine stopped there. Shipping both copies costs ~8 MB and needs no patch.
    EXTRA+=(--extra "$m:sce_module/$(basename "$m")")
  done

  # Wine's NLS data. It resolves the directory relative to the module path --
  #   /app0/win/wine/lib/wine/../../share/wine/nls -> /app0/win/share/wine/nls
  # and without it init_environment reports
  #   err:environ:read_nls_file failed to load .../locale.nls
  NLS=$PW/.deps/wine-runtime/nls
  if [ -d "$NLS" ]; then
    for f in "$NLS"/*.nls; do
      EXTRA+=(--extra "$f:win/share/wine/nls/$(basename "$f")")
    done
    printf '   nls: %s files\n' "$(ls "$NLS"/*.nls 2>/dev/null | wc -l)"
  fi
  printf '   modules: %s\n' "$(ls "$PRX"/sce_module/ | tr '\n' ' ')"
  "$BUNDLE/orbis-compat/scripts/ps4/make-pkg.sh" --eboot "$OBJ/../eboot.bin" --out-dir "$OUT" \
      --title-id "$TITLE_ID" --title "$TITLE" "${EXTRA[@]}"
  (( NEO )) && "$ROOT/scripts/set-neo.sh" "$OUT"
  ls -la "$OUT"/*.pkg | sed 's/^/   /'
fi
