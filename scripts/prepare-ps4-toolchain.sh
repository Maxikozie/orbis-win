#!/usr/bin/env bash
# Create the PS4 support objects and archives used by the Wine title and PRXs.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
PW="${PW_ROOT:-$ROOT/prospero-win}"
BUNDLE="$ROOT/sdk/orbis-sdk-v1"
OUT="$ROOT/build/ps4-wine-sdk"
OBJ="$OUT/objects"
CC="$OUT/bin/prospero-clang"

[[ -f "$BUNDLE/BUNDLE.txt" ]] || { echo "run scripts/fetch-sdk.sh first" >&2; exit 1; }
[[ -f "$PW/wine/ps5/pw_wine_dmem.c" ]] || { echo "missing patched prospero-win at $PW" >&2; exit 1; }
mkdir -p "$OBJ" "$OUT/bin"
"$ROOT/scripts/sync-toolchain.sh"

INC=( -I"$PW" -I"$PW/include" -I"$PW/src" -I"$PW/native" \
      -I"$PW/wine/ps5" -I"$ROOT/src/native" )
compile() {
  local name=$1 source=$2
  shift 2
  "$CC" -DPW_WINE_PRX_BUILD -std=gnu11 -O2 -fPIC "${INC[@]}" "$@" \
    -c "$source" -o "$OBJ/$name.o"
}

compile pw_wine_cwd_libc "$PW/wine/ps5/pw_wine_cwd_libc.c"
compile pw_wine_dl "$PW/wine/ps5/pw_wine_dl.c"
compile pw_wine_dl_ps4 "$ROOT/src/native/pw_wine_dl_ps4.c"
compile pw_wine_dmem "$PW/wine/ps5/pw_wine_dmem.c"
compile pw_wine_heap "$PW/wine/ps5/pw_wine_heap.c"
compile pw_wine_prx "$PW/wine/ps5/pw_wine_prx.c"
compile pw_wine_threads_ps4 "$ROOT/src/native/pw_wine_threads_ps4.c"
compile stubs "$ROOT/src/freebsd-compat/stubs.c"
compile pw_wine_cwd "$PW/wine/ps5/pw_wine_cwd.c"
compile pw_wine_dmem_glue_ps4 "$ROOT/src/native/pw_wine_dmem_glue_ps4.c"
compile pw_wine_heap_libc "$PW/wine/ps5/pw_wine_heap_libc.c"
compile pw_wine_threads "$PW/wine/ps5/pw_wine_threads.c"

llvm-ar rcs "$OUT/libwineshim.a" \
  "$OBJ/pw_wine_cwd_libc.o" "$OBJ/pw_wine_dl.o" "$OBJ/pw_wine_dl_ps4.o" \
  "$OBJ/pw_wine_dmem.o" "$OBJ/pw_wine_heap.o" "$OBJ/pw_wine_prx.o" \
  "$OBJ/pw_wine_threads_ps4.o" "$OBJ/stubs.o" "$OBJ/pw_wine_cwd.o" \
  "$OBJ/pw_wine_dmem_glue_ps4.o" "$OBJ/pw_wine_heap_libc.o" "$OBJ/pw_wine_threads.o"
llvm-ar rcs "$OUT/libps4stubs.a" "$OBJ/stubs.o" "$OBJ/pw_wine_dmem_glue_ps4.o"

# Wine uses 16-bit WCHAR on this target. The paired bundle's compatibility
# archive contains the SDK-compatible routines; preserve that exact archive.
cp "$BUNDLE/orbis-compat/build/liborbis-compat.a" "$OUT/liborbis-compat-16bit-wchar.a"
# ⚠ The section must be writable. The bundle's orbis-compat CRT (MIT, since
# orbis-compat 7bed896) declares the parameter block const, so its section is
# flagged A only. With src/freebsd-compat/prx_module_param.ld (ALIGN(0x4000),
# INSERT BEFORE .data) lld then gives it a read-only page of its own between
# RELRO and .data, create-fself starts the RW segment at it, and prx-ps4.sh's
# bss-pad check finds no oelf segment for the ELF's last RW one: "ntdll: end of
# .bss still unmapped after the .bss pad" (measured 2026-11-17). OpenOrbis's
# sdk/lib/crtlib.o has the same 0x18 bytes flagged WA, and so does every PRX of
# release v0.0.7 (the parameter block opens the RW segment). Keep the paired
# bundle's bytes and give them OpenOrbis's flags.
llvm-objcopy --only-section=.data.sce_module_param \
  --set-section-flags .data.sce_module_param=alloc,load,contents,data \
  "$BUNDLE/orbis-compat/build/crt/crtlib.o" "$OUT/module_param.o"
[[ -s "$OUT/module_param.o" ]] || { echo "failed to extract module parameter section" >&2; exit 1; }
# Read the headers once and search the text: `llvm-readelf | grep -q` under
# pipefail fails when grep exits on its first match while readelf is still writing.
mp_sections=$(llvm-readelf -S "$OUT/module_param.o") || {
  echo "llvm-readelf failed on $OUT/module_param.o" >&2; exit 1; }
grep -Eq '\.data\.sce_module_param +PROGBITS +[0-9a-f]+ [0-9a-f]+ [0-9a-f]+ [0-9a-f]+ +WA ' <<<"$mp_sections" ||
  { echo "module_param.o: .data.sce_module_param is not WA" >&2; exit 1; }
echo "PS4 toolchain support ready: $OUT"
