# Fault-delivery and ucontext-layout probe. No Vulkan, no Mesa, no shaders:
# it mmaps two pages, faults on each, and prints what caught them.
cmake_minimum_required(VERSION 3.16)
project(gladius_fault_probe C)

if(NOT DEFINED ORBIS_COMPAT_DIR OR NOT PS4)
  message(FATAL_ERROR
    "configure with -DCMAKE_TOOLCHAIN_FILE=<bundle>/toolchain/orbis-sdk.cmake")
endif()

add_executable(fault fault_probe.c)

orbis_compat_verify()

if(COMMAND ps4_create_eboot)
  ps4_create_eboot(fault)
endif()
