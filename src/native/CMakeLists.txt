# prospero-win's presenter, ported to the PS4. Builds the portable core and the
# PS4 platform layer into one archive.
cmake_minimum_required(VERSION 3.16)
project(pw_native_ps4 C)

if(NOT DEFINED ORBIS_COMPAT_DIR OR NOT PS4)
  message(FATAL_ERROR "configure with -DCMAKE_TOOLCHAIN_FILE=<bundle>/toolchain/orbis-sdk.cmake")
endif()

set(PW "${CMAKE_CURRENT_LIST_DIR}/../../prospero-win")
if(NOT EXISTS "${PW}/src") 
  message(FATAL_ERROR "no prospero-win checkout at ${PW}")
endif()

# The portable half, unmodified, straight from upstream.
file(GLOB PW_SRC "${PW}/src/*.c")
# ⚠ THE PAD CORE IS UPSTREAM'S, COMPILED UNMODIFIED. native/pw_pad_ps5.c
# reaches the platform only through a vtable, and guards its PS5 filling of
# that vtable behind PW_PAD_PS5_HOST_TEST. Defining it leaves the batching,
# generation tracking, edge detection and analog centring — and drops the eight
# PS5 forwards, which src/native/pw_pad_ps4.c then replaces.
#
# The macro is named for host testing and we are building for a console. It is
# still the right seam: it means "someone else supplies the platform", which is
# exactly our case. Named here rather than silently, because a reader finding
# HOST_TEST in a PS4 build deserves the reason.
set_source_files_properties("${PW}/native/pw_pad_ps5.c"
  PROPERTIES COMPILE_DEFINITIONS PW_PAD_PS5_HOST_TEST)
# Audio uses the same seam, for the same reason.
set_source_files_properties("${PW}/native/pw_audio_ps5.c"
  PROPERTIES COMPILE_DEFINITIONS PW_AUDIO_PS5_HOST_TEST)

add_library(pw_native_ps4 STATIC
  ${PW_SRC}
  "${PW}/native/pw_pad_ps5.c"
  "${PW}/native/pw_audio_ps5.c"
  # The module loader: pw_wine_prx.c is pure parsing, pw_wine_dl.c reaches the
  # kernel only through PwWineDlOps. Neither needs a guard — upstream kept the
  # Sony calls in a separate *_libc.c companion, which we simply do not compile.
  "${PW}/wine/ps5/pw_wine_prx.c"
  "${PW}/wine/ps5/pw_wine_dl.c"
  "${PW}/wine/ps5/pw_wine_dmem.c"
  # ⚠ NOTHING TO PORT. pw_wine_heap.c uses only pthread, string.h and
  # sys/mman.h — no Sony calls at all — so it compiles for the PS4 as-is.
  # Its *_libc.c companion is deliberately NOT built: that one interposes
  # malloc/free/realloc globally, and a test should exercise the heap through
  # its own API before every allocation in the process depends on it.
  "${PW}/wine/ps5/pw_wine_heap.c"
  # Also pure POSIX; its *_libc.c companion includes <sys/thr.h>, which the
  # PS4 SDK does not have, so src/native/pw_wine_threads_ps4.c replaces it.
  "${PW}/wine/ps5/pw_wine_threads.c"
  # The working-directory shim. Both halves are pure POSIX, so neither needed
  # porting — but the libc half only does anything if the linker is told to
  # divert the 28 calls it wraps, below.
  "${PW}/wine/ps5/pw_wine_cwd.c"
  "${PW}/wine/ps5/pw_wine_cwd_libc.c"
  pw_videoout_ps4.c
  pw_pad_ps4.c
  pw_audio_ps4.c
  pw_wine_dl_ps4.c
  pw_wine_dmem_ps4.c
  pw_wine_threads_ps4.c)
target_include_directories(pw_native_ps4 PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}" "${PW}/src" "${PW}/native" "${PW}/wine/ps5" "${PW}/include")
target_compile_options(pw_native_ps4 PRIVATE -std=c11 -O2 -Wall -Wextra)

# ⚠ VideoOut IS NOT ON THE DEFAULT LINK LINE. The toolchain file adds libc,
# libkernel and the C++ runtime; everything else is named by the consumer. The
# SDK ships ~422 import stubs in lib/ and libSceVideoOut.so is the one that
# exports sceVideoOutSubmitFlip / sceVideoOutWaitVblank. PUBLIC so an executable
# linking this archive inherits it — a STATIC library records the dependency but
# cannot satisfy it itself.
# ⚠ WITHOUT THESE THE SHIM IS INERT. pw_wine_cwd_libc.c defines __wrap_open and
# friends and calls __real_open; lld only creates that pairing when told to, per
# symbol. A build missing them links cleanly, runs, and resolves no relative
# path — the failure is silence, not an error.
#
# MEASURED on this console 2026-10-07, which is why the shim is needed at all:
#   chdir("/data")  -> EPERM   (same as the PS5)
#   getcwd()        -> ENOSYS  (the PS5 faults instead)
#   open("data/pkg")-> ENOENT  (nothing to resolve against)
#   symlink()       -> ENOSYS  (the PS5 kills the process instead)
target_link_libraries(pw_native_ps4 PUBLIC
  "-Wl,--wrap=access"
  "-Wl,--wrap=chdir"
  "-Wl,--wrap=chmod"
  "-Wl,--wrap=close"
  "-Wl,--wrap=dup"
  "-Wl,--wrap=fchdir"
  "-Wl,--wrap=fopen"
  "-Wl,--wrap=fstatat"
  "-Wl,--wrap=getcwd"
  "-Wl,--wrap=link"
  "-Wl,--wrap=lstat"
  "-Wl,--wrap=mkdir"
  "-Wl,--wrap=mkdirat"
  "-Wl,--wrap=open"
  "-Wl,--wrap=openat"
  "-Wl,--wrap=opendir"
  "-Wl,--wrap=readlink"
  "-Wl,--wrap=realpath"
  "-Wl,--wrap=rename"
  "-Wl,--wrap=renameat"
  "-Wl,--wrap=rmdir"
  "-Wl,--wrap=stat"
  "-Wl,--wrap=statfs"
  "-Wl,--wrap=symlink"
  "-Wl,--wrap=symlinkat"
  "-Wl,--wrap=truncate"
  "-Wl,--wrap=unlink"
  "-Wl,--wrap=unlinkat"
  "-Wl,--wrap=utimes"

  "${OO_PS4_TOOLCHAIN}/lib/libSceVideoOut.so"
  "${OO_PS4_TOOLCHAIN}/lib/libSceSystemService.so"   # HideSplashScreen
  "${OO_PS4_TOOLCHAIN}/lib/libSceUserService.so"     # the signed-in user fallback
  "${OO_PS4_TOOLCHAIN}/lib/libScePad.so"             # DualShock 4
  "${OO_PS4_TOOLCHAIN}/lib/libSceAudioOut.so")       # the main audio port
orbis_compat_verify()
