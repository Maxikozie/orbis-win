# The stage 2 instrument. Needs Mesa (for RADV) but no shader compiler: it
# creates no device, compiles no pipeline and draws nothing.
cmake_minimum_required(VERSION 3.16)
project(gladius_probe C)

if(NOT DEFINED ORBIS_COMPAT_DIR OR NOT PS4)
  message(FATAL_ERROR
    "configure with -DCMAKE_TOOLCHAIN_FILE=<bundle>/toolchain/orbis-sdk.cmake")
endif()

if(NOT ORBIS_MESA_SRC)
  if(DEFINED ENV{ORBIS_MESA_SRC})
    set(ORBIS_MESA_SRC "$ENV{ORBIS_MESA_SRC}")
  else()
    message(FATAL_ERROR "ORBIS_MESA_SRC is not set; source the bundle's env.sh")
  endif()
endif()

add_executable(probe probe.c)

# Same vkloader the triangle uses: this console has no Vulkan loader and cannot
# have one. The eboot is statically linked, there is no libvulkan.so to dlopen
# and no icd.d to read; libvulkan_radeon.a exports three ICD symbols and nothing
# named vkSomething, so the shim defines the entry points and forwards each one.
if(NOT ORBIS_KIT_DIR)
  set(ORBIS_KIT_DIR "${ORBIS_COMPAT_DIR}")
endif()
add_subdirectory("${ORBIS_KIT_DIR}/vkloader" ps4-vkloader)
target_link_libraries(probe PRIVATE ps4-vkloader)

orbis_compat_verify()

if(COMMAND ps4_create_eboot)
  ps4_create_eboot(probe)
endif()
