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
add_library(pw_native_ps4 STATIC ${PW_SRC} pw_videoout_ps4.c)
target_include_directories(pw_native_ps4 PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}" "${PW}/src" "${PW}/include")
target_compile_options(pw_native_ps4 PRIVATE -std=c11 -O2 -Wall -Wextra)

# ⚠ VideoOut IS NOT ON THE DEFAULT LINK LINE. The toolchain file adds libc,
# libkernel and the C++ runtime; everything else is named by the consumer. The
# SDK ships ~422 import stubs in lib/ and libSceVideoOut.so is the one that
# exports sceVideoOutSubmitFlip / sceVideoOutWaitVblank. PUBLIC so an executable
# linking this archive inherits it — a STATIC library records the dependency but
# cannot satisfy it itself.
target_link_libraries(pw_native_ps4 PUBLIC "${OO_PS4_TOOLCHAIN}/lib/libSceVideoOut.so")
orbis_compat_verify()
