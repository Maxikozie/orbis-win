#!/bin/sh
# Build libfreetype.prx for PS4: the FreeType win32u looks up by dlsym.
#
# build_wine_ps5.sh already compiles libfreetype.a and hands it to configure as
# FREETYPE_LIBS, but nothing can reach it: win32u's freetype.c opens FreeType
# only through dlopen(SONAME_LIBFREETYPE) and then dlsym's each entry point, and
# the archive is not even on win32u's link line. So the library was built and
# then went nowhere, every run reported
#
#     Wine cannot find the FreeType font library.
#
# and the port had no fonts at all. A Windows error box drew its frame and not
# one glyph of the message inside it.
#
# Same shape as scripts/radv-prx-ps4.sh: put the archive behind a PRXDESC1
# descriptor so pw_wine_dl's dlopen("libfreetype.so") finds libfreetype.prx and
# dlsym resolves from the descriptor.
set -eu
# Share the title's libc, as every other PRX here does; see prx-ps4.sh.
: "${PW_SHARED_LIBC:=1}"; export PW_SHARED_LIBC
root=/home/zpeirce/ps4-gladius
bundle=$root/sdk/orbis-sdk-v1
oo=$bundle/sdk
sdk=$root/build/ps4-wine-sdk
pw=$root/prospero-win
prx=$pw/.deps/wine-ps5/prx
CC=$sdk/bin/prospero-clang
export OO_PS4_TOOLCHAIN=$oo

archive=$pw/.deps/wine-ps5/freetype/libfreetype.a
[ -f "$archive" ] || { echo "no libfreetype.a at $archive" >&2; exit 2; }

work=$prx/freetype
mkdir -p "$work/obj" "$prx/sce_module"
log=$prx/libfreetype.link.log

# The entry points win32u resolves, taken from its own source rather than a
# list kept here: freetype.c's LOAD_FUNCPTR lines are the contract, and a name
# added upstream must not silently become a NULL pointer at run time.
symbols=$(grep -oE "LOAD_FUNCPTR\(FT_[A-Za-z_0-9]+\)" \
    "$pw/.deps/wine-ps5/source/dlls/win32u/freetype.c" |
    sed 's/LOAD_FUNCPTR(//;s/)//' | sort -u)
echo "== 1. descriptor ($(echo "$symbols" | wc -l) entry points) =="
# shellcheck disable=SC2086
python3 "$pw/tools/gen_prx_descriptor.py" "$work/obj/libfreetype_desc.c" $symbols
$CC -std=c11 -O2 -Wall -Wextra -Werror -fPIC -I"$pw/wine/ps5" \
    -c "$work/obj/libfreetype_desc.c" -o "$work/obj/libfreetype_desc.o"

echo "== 2. link =="
# whole-archive for the same reason the driver needs it: nothing will dlopen
# the members the linker decided were unreachable, and win32u resolves each
# entry point by name at run time rather than referencing it at link time.
$CC -shared -Wl,-Bsymbolic -Wl,--eh-frame-hdr -Wl,-soname,libfreetype.prx \
    -Wl,--warn-unresolved-symbols -o "$prx/libfreetype.shared.elf" \
    "$work/obj/libfreetype_desc.o" \
    -Wl,--whole-archive "$archive" -Wl,--no-whole-archive > "$log" 2>&1 ||
    { echo "   X link"; grep -E "error|undefined" "$log" | head -8 | sed 's/^/      /'; exit 1; }

echo "== 3. container =="
"$oo/bin/linux/create-fself" -in "$prx/libfreetype.shared.elf" \
    -out "$prx/libfreetype.oelf" -lib "$prx/sce_module/libfreetype.prx" \
    -libname libfreetype.prx -ptype fake \
    -library-path "$prx/stubs:$oo/lib" >> "$log" 2>&1 ||
    { echo "   X create-fself"; tail -4 "$log" | sed 's/^/      /'; exit 1; }
printf '   ok libfreetype  %s\n' "$(du -h "$prx/sce_module/libfreetype.prx" | cut -f1)"
