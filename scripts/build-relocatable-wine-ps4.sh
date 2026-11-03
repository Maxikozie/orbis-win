#!/usr/bin/env bash
# Build a separate Wine title whose main code runs from a high-address PRX.
# The tiny eboot hands off first; the PRX keeps the eboot mapped through Sony
# setup and releases it immediately before the selected Windows process starts.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
PW="${PW_ROOT:-$ROOT/prospero-win}"
BUNDLE="$ROOT/sdk/orbis-sdk-v1"
CC="$ROOT/build/ps4-wine-sdk/bin/prospero-clang"
OO="$BUNDLE/sdk"
TITLE_ID="${TITLE_ID:-GLDS10044}"
TITLE="${TITLE:-Prospero Wine Low-Base Test}"
OUT="${PKG_OUT:-$ROOT/pkgout/wine-app-relocatable}"
BASE_OUT="${PKG_BASE_OUT:-$ROOT/pkgout/wine-app-relocatable-base}"
BUILD="${PW_BUILD_ROOT:-$ROOT/build/wine-app-relocatable}"
APP_OBJ="${PW_APP_OBJ:-$ROOT/build/wine-app/obj}"
PRX="$PW/.deps/wine-ps5/prx"
PW_WINE64_WAIT_WATCHDOG="${PW_WINE64_WAIT_WATCHDOG:-0}"
PW_WINE64_SERVER_DIRECT_OFF="${PW_WINE64_SERVER_DIRECT_OFF:-0}"
PW_WINE64_HEAP_FLEXIBLE="${PW_WINE64_HEAP_FLEXIBLE:-0}"
PS4_KERNELBASE32_DLL="${PS4_KERNELBASE32_DLL:-}"
PS4_IPHLPAPI32_DLL="${PS4_IPHLPAPI32_DLL:-}"
PW_SPLIT_RUNTIME="${PW_SPLIT_RUNTIME:-0}"
NEO=0
for a in "$@"; do
  case "$a" in --neo) NEO=1 ;; esac
done
case "$PW_WINE64_WAIT_WATCHDOG" in
  0|1) ;;
  *) echo "PW_WINE64_WAIT_WATCHDOG must be 0 or 1" >&2; exit 2 ;;
esac
case "$PW_WINE64_SERVER_DIRECT_OFF" in
  0|1) ;;
  *) echo "PW_WINE64_SERVER_DIRECT_OFF must be 0 or 1" >&2; exit 2 ;;
esac
case "$PW_WINE64_HEAP_FLEXIBLE" in
  0|1) ;;
  *) echo "PW_WINE64_HEAP_FLEXIBLE must be 0 or 1" >&2; exit 2 ;;
esac
case "$PW_SPLIT_RUNTIME" in
  0|1) ;;
  *) echo "PW_SPLIT_RUNTIME must be 0 or 1" >&2; exit 2 ;;
esac
export OO_PS4_TOOLCHAIN="$OO"

[[ -x "$CC" ]] || { echo "missing PS4 compiler: $CC" >&2; exit 1; }
[[ -f "$ROOT/sce_module/libc.prx" && -f "$ROOT/sce_module/libSceFios2.prx" ]] || {
  echo "missing required Sony modules in sce_module/" >&2; exit 1;
}

echo "== build the normal app objects and complete runtime package stage"
APP_ARGS=(--pkg --relocatable-stage)
(( NEO )) && APP_ARGS+=(--neo)
TITLE_ID="$TITLE_ID" TITLE="$TITLE" PKG_OUT="$BASE_OUT" \
PW_ROOT="$PW" PW_APP_OBJ="$APP_OBJ" \
PW_WINE64_WAIT_WATCHDOG="$PW_WINE64_WAIT_WATCHDOG" \
PW_WINE64_SERVER_DIRECT_OFF="$PW_WINE64_SERVER_DIRECT_OFF" \
PW_WINE64_HEAP_FLEXIBLE="$PW_WINE64_HEAP_FLEXIBLE" \
PS4_KERNELBASE32_DLL="$PS4_KERNELBASE32_DLL" \
PS4_IPHLPAPI32_DLL="$PS4_IPHLPAPI32_DLL" \
PS4_NTDLL32_DLL="${PS4_NTDLL32_DLL:-}" \
  "$ROOT/scripts/build-app-ps4.sh" "${APP_ARGS[@]}"

echo "== build the high-address Wine PRX"
PRX_BUILD="$BUILD/prx"
mkdir -p "$PRX_BUILD"
"$CC" -std=gnu11 -O2 -fPIC -DPW_RELOCATABLE_WINE \
  -DPW_WINE64_WAIT_WATCHDOG="$PW_WINE64_WAIT_WATCHDOG" \
  -DPW_WINE64_SERVER_DIRECT_OFF="$PW_WINE64_SERVER_DIRECT_OFF" \
  -DPW_WINE64_HEAP_FLEXIBLE="$PW_WINE64_HEAP_FLEXIBLE" \
  -I"$PW" -I"$PW/include" -I"$PW/src" -I"$PW/native" -I"$ROOT/src/native" \
  -include "$PW/native/ps5log/ps5log_ps5_net.h" \
  -c "$PW/native/wine64_main.c" -o "$PRX_BUILD/native_wine64_main_reloc.o"
"$CC" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
  -c "$ROOT/src/reloc-wine-app/app_entry.c" -o "$PRX_BUILD/app_entry.o"
python3 "$PW/tools/gen_prx_descriptor.py" "$PRX_BUILD/descriptor.c" \
  pw_relocated_wine_start
"$CC" -std=gnu11 -O2 -fPIC -I"$PW/wine/ps5" \
  -c "$PRX_BUILD/descriptor.c" -o "$PRX_BUILD/descriptor.o"
APP_OBJECTS=()
for object in "$APP_OBJ"/*.o; do
  # The SDK's libwineshim.a supplies these three pure descriptor helpers too;
  # retain the archive copy so the PRX does not define duplicate symbols.
  [[ ${object##*/} == pw_wine_prx.o || ${object##*/} == native_wine64_main.o ]] && continue
  APP_OBJECTS+=("$object")
done
APP_OBJECTS+=("$PRX_BUILD/native_wine64_main_reloc.o")
"$CC" -shared -Wl,-Bsymbolic -Wl,-soname,relocated_wine_app.prx \
  -Wl,--warn-unresolved-symbols -o "$PRX_BUILD/app.shared.elf" \
  "${APP_OBJECTS[@]}" "$PRX_BUILD/app_entry.o" "$PRX_BUILD/descriptor.o" \
  -lSceNet -lSceNetCtl -lSceAudioOut -lSceSystemService -lSceSysmodule -lSceGnmDriver \
  -lSceVideoOut -lScePad -lSceUserService
"$OO/bin/linux/create-fself" -in "$PRX_BUILD/app.shared.elf" \
  -out "$PRX_BUILD/app.oelf" -lib "$PRX_BUILD/relocated_wine_app.prx" \
  -libname relocated_wine_app.prx -ptype fake \
  -library-path "$PRX_BUILD:$ROOT/build/ps4-wine-sdk:$OO/lib" \
  >"$PRX_BUILD/create-fself.log" 2>&1 || {
    cat "$PRX_BUILD/create-fself.log" >&2
    echo "could not convert Wine app into a PRX" >&2
    exit 1
  }

echo "== build the low-address handoff eboot"
mkdir -p "$BUILD"
python3 "$ROOT/scripts/gen-libkernel-symbol-list.py" \
  "$OO/include/orbis/libkernel.h" "$BUILD/libkernel-symbols.inc"
"$CC" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
  -I"$PW/wine/ps5" -I"$BUILD" "$ROOT/src/reloc-wine-app/stub.c" \
  -o "$BUILD/stub.elf" -lSceNet -lSceAudioOut -lSceSystemService \
  -lSceSysmodule -lSceGnmDriver -lSceVideoOut -lScePad -lSceUserService
"$OO/bin/linux/create-fself" -in "$BUILD/stub.elf" \
  -out "$BUILD/stub.oelf" -eboot "$BUILD/eboot.bin" >/dev/null

echo "== package with a distinct title ID"
STAGE="$BASE_OUT/pkg-stage"
EXTRA=(--extra "$PRX_BUILD/relocated_wine_app.prx:sce_module/relocated_wine_app.prx")
if (( PW_SPLIT_RUNTIME )); then
  [[ -s "$STAGE/win/wine/lib/wine/x86_64-unix/ntdll.prx" ]] || {
    echo "split runtime source stage is missing ntdll.prx: $STAGE" >&2; exit 1;
  }
  [[ -f "$STAGE/win/wine/lib/wine/x86_64-windows/wow64native.dll" ]] || {
    echo "split runtime source stage is missing wow64native.dll: $STAGE" >&2; exit 1;
  }
  echo "== split runtime: package app and profiles; leave Wine modules/data in base stage for /data upload"
fi
while IFS= read -r -d '' file; do
  rel="${file#"$STAGE"/}"
  case "$rel" in
    eboot.bin|pkg.gp4|sce_sys/param.sfo|pkg-build.log|pkg-build-neo.log|build.log|*.pkg) continue ;;
  esac
  if (( PW_SPLIT_RUNTIME )); then
    case "$rel" in win/wine/*) continue ;; esac
    case "$rel" in
      sce_module/*.prx)
        module="${rel##*/}"
        case "$module" in libc.prx|libSceFios2.prx) ;; *) continue ;; esac
        ;;
    esac
  fi
  EXTRA+=(--extra "$file:$rel")
done < <(find "$STAGE" -type f -print0)
"$BUNDLE/orbis-compat/scripts/ps4/make-pkg.sh" \
  --eboot "$BUILD/eboot.bin" --out-dir "$OUT" \
  --title-id "$TITLE_ID" --title "$TITLE" "${EXTRA[@]}"
if (( NEO )); then
  "$ROOT/scripts/set-neo.sh" "$OUT"
fi

PKG_FILE=$(find "$OUT" -maxdepth 1 -type f -name '*.pkg' -print -quit)
[[ -n "$PKG_FILE" && -s "$PKG_FILE" ]] || { echo "package was not created" >&2; exit 1; }
echo "== verify built image types and package contents"
llvm-readelf -h "$PRX_BUILD/app.shared.elf" | grep -F 'Type:                              DYN'
llvm-readelf -h "$BUILD/stub.elf" | grep -F 'Type:                              DYN'
test -s "$OUT/pkg-stage/sce_module/relocated_wine_app.prx"
if (( PW_SPLIT_RUNTIME )); then
  if find "$OUT/pkg-stage/win/wine" -type f -print -quit 2>/dev/null | grep -q .; then
    echo "split package unexpectedly contains Wine runtime files" >&2; exit 1;
  fi
  for module in libc.prx libSceFios2.prx relocated_wine_app.prx; do
    test -s "$OUT/pkg-stage/sce_module/$module"
  done
fi
echo "pkg: $PKG_FILE ($(du -h "$PKG_FILE" | cut -f1))"
sha256sum "$PKG_FILE"
