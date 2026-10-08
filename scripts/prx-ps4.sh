#!/bin/sh
# Build the Wine PRX modules for PS4.
#
# prospero-win's own PRX stage (tools/build_wine_ps5.sh, link_prx) is skipped on
# PS4 -- it needs ps5-native-app-boilerplate's ps5-native-tool, which does not
# exist for this console. Steps 1-3 below are upstream's, unchanged in intent;
# only the final container step differs: where the PS5 runs
#     ps5-native-tool link --module   +   ps5-native-tool self --sign
# PS4 uses OpenOrbis create-fself -lib, which does both.
#
# The export lists are upstream's, verbatim. Exports come from the generated
# PRXDESC1 descriptor, NOT from the .so's dynamic symbol table -- converting a
# .so directly produces a module that exports libc and nothing prospero-win
# looks up.
set -eu
root=${PW_ROOT:-/home/zpeirce/ps4-gladius/prospero-win}
gladius=/home/zpeirce/ps4-gladius
sdk=$gladius/build/ps4-wine-sdk
oo=$gladius/sdk/orbis-sdk-v1/sdk
work=$root/.deps/wine-ps5
build=$work/build
prx=$work/prx
CC=$sdk/bin/prospero-clang
export OO_PS4_TOOLCHAIN=$oo
mkdir -p "$prx/obj" "$prx/sce_module"

link_objects() {
    python3 - "$work/make.log" "$1" <<'PY'
import shlex, sys
text = open(sys.argv[1], errors="replace").read().replace("\\\n", " ")
lines = [l for l in text.splitlines() if f"-o {sys.argv[2]} " in l]
if lines:
    print(" ".join(a for a in shlex.split(lines[-1])
                   if a.endswith(".o") and not a.startswith("/")))
PY
}

echo "== 1. platform units =="
for unit in pw_wine_prx pw_wine_dl pw_wine_dl_libc pw_wine_compat pw_wine_compat_libc \
        pw_wine_threads pw_wine_threads_libc pw_wine_sink pw_wine_cwd pw_wine_cwd_libc; do
    $CC -DPW_WINE_PRX_BUILD -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC \
        -c "$root/wine/ps5/$unit.c" -o "$prx/obj/$unit.o"
done

echo "== 1b. heap + direct-memory objects =="
# ntdll's allocator and its anonymous memory (patch 0600). link_objects() keeps
# only relative .o paths from make.log, so these never appear there and must be
# named here, exactly as upstream's link_prx takes them as its $3.
mkdir -p "$prx/heap"
heap=""
for unit in pw_wine_heap pw_wine_heap_libc; do
    $CC -std=c11 -O2 -Wall -Wextra -Werror -fPIC -c "$root/wine/ps5/$unit.c" -o "$prx/heap/$unit.o"
    heap="$heap $prx/heap/$unit.o"
done
# pw_wine_dmem is portable; its PS5 half is replaced by this port's PS4 glue,
# which supplies the five __wine_ps5_* entry points over PS4 direct memory.
dmem=""
$CC -std=c11 -O2 -Wall -Wextra -Werror -fPIC -c "$root/wine/ps5/pw_wine_dmem.c" -o "$prx/heap/pw_wine_dmem.o"
$CC -std=c11 -O2 -fPIC -I"$root/wine/ps5" -c "$gladius/src/native/pw_wine_dmem_glue_ps4.c" -o "$prx/heap/pw_wine_dmem_glue_ps4.o"
dmem="$prx/heap/pw_wine_dmem.o $prx/heap/pw_wine_dmem_glue_ps4.o"

echo "== 2. descriptors (upstream's export lists) =="
gen() { python3 "$root/tools/gen_prx_descriptor.py" "$@"; }
gen "$prx/obj/ntdll_desc.c" \
    __wine_main pw_wine_dl_adopt dlopen dlsym dlerror \
    pw_wine_heap_stats pw_wine_heap_malloc pw_wine_heap_free \
    pw_wine_set_present_sink pw_wine_post_input pw_wine_sink_stats \
    pw_wine_present pw_wine_next_input pw_wine_input_fd \
    pw_wine_set_pad pw_wine_rumble pw_wine_pad pw_wine_set_rumble \
    pw_wine_set_audio_sink pw_wine_audio_available pw_wine_audio_output \
    __wine_virtual_stats __wine_virtual_fault_top __wine_ps5_set_output_sink \
    __wine_ps5_memory_stats pw_cwd_set \
    __wine_ps5_set_segv_hook __wine_ps5_set_segv_unresolved_hook \
    pw_wine_set_display_release pw_wine_release_display pw_ps4_setenv \
    --optional-from "$build/dlls/ntdll/ntdll.so" --nm llvm-nm \
    --optional-export __wine_prospero_native_wow64_caps
gen "$prx/obj/win32u_desc.c" __wine_unix_lib_init
gen "$prx/obj/wineserver_desc.c" \
    pw_wineserver_connect pw_wine_thread_register pw_wine_thread_unregister \
    pw_wineserver_call_direct pw_wineserver_try_fast_mutex \
    pw_wineserver_mutex_backend pw_wineserver_sync_backend pw_cwd_share_changes
gen "$prx/obj/wowprospero_desc.c" __wine_unix_call_funcs
for m in winevulkan opengl32 ws2_32 crypt32 dwrite; do
    gen "$prx/obj/${m}_desc.c" __wine_unix_call_funcs __wine_unix_call_wow64_funcs
done
for d in ntdll win32u wineserver wowprospero winevulkan opengl32 ws2_32 crypt32 dwrite; do
    $CC -std=gnu11 -O2 -fPIC -I"$root/wine/ps5" -c "$prx/obj/${d}_desc.c" -o "$prx/obj/${d}_desc.o"
done

stubs=$prx/stubs; rm -rf "$stubs"; mkdir -p "$stubs"

# ⚠ -ptype fake. Without it create-fself stamps a library with the same
# header as an executable (measured: eboot.bin and ntdll.prx were byte-identical
# in their first 8 bytes), and the loader rejects it:
#     load status=-2147352568 stage=1 module=0x80020008    (ENOEXEC)
# after having found the file -- the previous failure was 0x80020002, ENOENT.
echo "== 3. link + 4. container =="
# ntdll: loader, compat, sink, cwd. The thread registry belongs to the server.
ntdll_shims="pw_wine_prx pw_wine_dl pw_wine_dl_libc pw_wine_compat pw_wine_compat_libc \
             pw_wine_sink pw_wine_cwd pw_wine_cwd_libc"
# This port's own: lets the title write into ntdll's environment (see the file).
$CC -std=gnu11 -O2 -fPIC -c "$gladius/src/native/pw_prx_env_ps4.c" -o "$prx/obj/pw_prx_env_ps4.o"
srv_shims="pw_wine_compat pw_wine_compat_libc pw_wine_threads pw_wine_threads_libc \
           pw_wine_cwd pw_wine_cwd_libc"
# A title cannot chdir: ntdll and wineserver each keep their own working
# directory, so their path calls are wrapped (wine/ps5/pw_wine_cwd.h).
cwd_wraps=""
for n in $(grep -o '__wrap_[a-z_]*' "$root/wine/ps5/pw_wine_cwd_libc.c" | sed 's/^__wrap_//' | LC_ALL=C sort -u); do
    cwd_wraps="$cwd_wraps -Wl,--wrap=$n"
done

export PW_PRX_LINK=1
status=0
# Linking against the already-built modules is what records DT_NEEDED; without
# it create-fself has no library to attribute an imported symbol to and fails
# with "missing library for symbol". --as-needed keeps out dependencies a
# module does not actually use.
# create-fself resolves each imported symbol to a providing library, so a module
# must be built after everything it imports: win32u needs ntdll's Rtl*, the five
# unixlibs need ntdll's __wine_dbg_*, opengl32/winevulkan need win32u's.
for step in "ntdll|dlls/ntdll/ntdll.so|$ntdll_shims" \
            "win32u|dlls/win32u/win32u.so|" \
            "wineserver|server/wineserver|$srv_shims" \
            "winevulkan|dlls/winevulkan/winevulkan.so|" \
            "opengl32|dlls/opengl32/opengl32.so|" \
            "ws2_32|dlls/ws2_32/ws2_32.so|" \
            "crypt32|dlls/crypt32/crypt32.so|" \
            "dwrite|dlls/dwrite/dwrite.so|"; do
    name=${step%%|*}; rest=${step#*|}; target=${rest%%|*}; shims=${rest#*|}
    objs=$(link_objects "$target")
    [ -n "$objs" ] || { echo "  ✗ $name: no ELF link in make.log"; status=1; continue; }
    shim_objs=""
    [ "$name" = ntdll ] && shim_objs="$heap $dmem $prx/obj/pw_prx_env_ps4.o"
    for s in $shims; do shim_objs="$shim_objs $prx/obj/$s.o"; done
    log=$prx/$name.link.log
    # shellcheck disable=SC2086
    if (cd "$build" && $CC -shared -Wl,-Bsymbolic -Wl,--eh-frame-hdr \
            -Wl,-soname,"$name.prx" -Wl,--warn-unresolved-symbols $cwd_wraps \
            -o "$prx/$name.shared.elf" $objs $shim_objs "$prx/obj/${name}_desc.o" \
            -L"$stubs" -Wl,--as-needed $(ls "$stubs"/*.so 2>/dev/null)) > "$log" 2>&1
    then
        if "$oo/bin/linux/create-fself" -in "$prx/$name.shared.elf" \
               -out "$prx/$name.oelf" -lib "$prx/sce_module/$name.prx" \
               -libname "$name.prx" -ptype fake -library-path "$stubs:$oo/lib" >> "$log" 2>&1
        then
            # Later modules import from this one. The linker needs lib<name>.so
            # to resolve -l style, and create-fself opens the DT_NEEDED name
            # verbatim -- which is the soname, "<name>.prx" -- so provide both.
            cp "$prx/$name.shared.elf" "$stubs/$name.so"
            cp "$prx/$name.shared.elf" "$stubs/$name.prx"
            echo "  ✓ $name  $(du -h "$prx/sce_module/$name.prx" | cut -f1)"
        else echo "  ✗ $name: create-fself"; tail -3 "$log" | sed 's/^/      /'; status=1; fi
    else
        echo "  ✗ $name: link"; grep -E "error|undefined" "$log" | head -4 | sed 's/^/      /'; status=1
    fi
done
echo "== 5. wowprospero (the i386 DBT as Wine's WoW64 CPU backend) =="
# Built last because it links against ntdll, which the loop above produces.
# Not a Wine make target: tools/build_wowprospero.sh builds it with one
# compiler command over unix.c and prospero-win's DBT, so it is absent from
# make.log and link_objects() cannot see it. Same source list as upstream.
WOW_DBT="$root/src/pw_x86_engine.c $root/src/pw_x86_block.c $root/src/pw_x86_cache.c
         $root/src/pw_x86_hostexec.c $root/src/pw_x86_reencode.c $root/src/pw_x87.c
         $root/src/pw_guest_fp.c $root/src/pw_vm.c $root/src/pw_vm_posix.c"
# shellcheck disable=SC2086
$CC -shared -Wl,-Bsymbolic -Wl,-soname,wowprospero.prx \
    -D__WINESRC__ -DWINE_UNIX_LIB -D_REENTRANT \
    -I"$root/wine/wowprospero" -I"$build/include" -I"$work/source/include" \
    -I"$root/src" -I"$root/include" \
    "$root/wine/wowprospero/unix.c" $WOW_DBT "$prx/obj/wowprospero_desc.o" \
    -L"$stubs" "$stubs/ntdll.so" -o "$prx/wowprospero.shared.elf" \
    > "$prx/wowprospero.link.log" 2>&1 || { echo "  ✗ wowprospero: link"; grep -E "error" "$prx/wowprospero.link.log" | head -3; }

if [ -f "$prx/wowprospero.shared.elf" ]; then
    if "$oo/bin/linux/create-fself" -in "$prx/wowprospero.shared.elf" \
           -out "$prx/wowprospero.oelf" -lib "$prx/sce_module/wowprospero.prx" \
           -libname "wowprospero.prx" -ptype fake -library-path "$stubs:$oo/lib" \
           >> "$prx/wowprospero.link.log" 2>&1
    then echo "  ✓ wowprospero  $(du -h "$prx/sce_module/wowprospero.prx" | cut -f1)"
    else echo "  ✗ wowprospero: create-fself"; tail -3 "$prx/wowprospero.link.log" | sed 's/^/      /'; status=1; fi
fi

exit $status
