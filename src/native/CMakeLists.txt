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

add_library(pw_native_ps4 STATIC
  ${PW_SRC}
  "${PW}/native/pw_pad_ps5.c"
  pw_videoout_ps4.c
  pw_pad_ps4.c)
target_include_directories(pw_native_ps4 PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}" "${PW}/src" "${PW}/native" "${PW}/include")
target_compile_options(pw_native_ps4 PRIVATE -std=c11 -O2 -Wall -Wextra)

# ⚠ VideoOut IS NOT ON THE DEFAULT LINK LINE. The toolchain file adds libc,
# libkernel and the C++ runtime; everything else is named by the consumer. The
# SDK ships ~422 import stubs in lib/ and libSceVideoOut.so is the one that
# exports sceVideoOutSubmitFlip / sceVideoOutWaitVblank. PUBLIC so an executable
# linking this archive inherits it — a STATIC library records the dependency but
# cannot satisfy it itself.
target_link_libraries(pw_native_ps4 PUBLIC
  "${OO_PS4_TOOLCHAIN}/lib/libSceVideoOut.so"
  "${OO_PS4_TOOLCHAIN}/lib/libSceSystemService.so"   # HideSplashScreen
  "${OO_PS4_TOOLCHAIN}/lib/libSceUserService.so"     # the signed-in user fallback
  "${OO_PS4_TOOLCHAIN}/lib/libScePad.so")            # DualShock 4
orbis_compat_verify()
