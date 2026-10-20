cmake_minimum_required(VERSION 3.16)
project(gladius_lowbase_probe C)

if(NOT DEFINED ORBIS_COMPAT_DIR OR NOT PS4)
  message(FATAL_ERROR "configure with the bundle's orbis-sdk.cmake toolchain")
endif()

add_executable(lowbaseprobe main.c)
target_compile_options(lowbaseprobe PRIVATE -Wall -Wextra -Werror)
orbis_compat_verify()
if(COMMAND ps4_create_eboot)
  ps4_create_eboot(lowbaseprobe)
endif()
