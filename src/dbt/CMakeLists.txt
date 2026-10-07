# Does prospero-win's portable core cross-compile for the PS4?
#
# src/ is the i386 dynamic binary translator — the hardest piece in that
# repository and, per its own architecture doc, the most portable: it includes
# only <stddef.h>, <stdint.h>, <string.h> and <limits.h>, has no allocator and
# no formatter, and takes memory from an injected backend rather than malloc.
# That was done so a PS5 firmware's wrong libc symbols could not be inherited;
# it is exactly what makes a PS4 target plausible.
#
# ⚠ THIS BUILDS, IT DOES NOT RUN. Compiling says the headers and the ABI agree.
# Whether the translated code is correct on Jaguar is a hardware question, and
# the upstream host suite (make test) is what answers the correctness half.
cmake_minimum_required(VERSION 3.16)
project(pw_dbt C)

if(NOT DEFINED ORBIS_COMPAT_DIR OR NOT PS4)
  message(FATAL_ERROR "configure with -DCMAKE_TOOLCHAIN_FILE=<bundle>/toolchain/orbis-sdk.cmake")
endif()

set(PW "${CMAKE_CURRENT_LIST_DIR}/../../prospero-win")
if(NOT EXISTS "${PW}/src")
  message(FATAL_ERROR "no prospero-win checkout at ${PW}")
endif()

file(GLOB PW_SRC "${PW}/src/*.c")
list(LENGTH PW_SRC PW_N)
message(STATUS "prospero-win portable core: ${PW_N} translation units")

add_library(pw_dbt STATIC ${PW_SRC})
target_include_directories(pw_dbt PUBLIC "${PW}/src" "${PW}/include")

# The upstream build is -Wall -Wextra -Werror; keep that honest rather than
# discovering on hardware that a warning mattered.
target_compile_options(pw_dbt PRIVATE -std=c11 -O2 -Wall -Wextra)

orbis_compat_verify()
