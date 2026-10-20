cmake_minimum_required(VERSION 3.16)
project(gladius_gfx_hang_probe C)

option(GFXHANG_SINGLE_SHOT "Submit one indexed draw in one frame, then idle" OFF)
option(GFXHANG_REPEAT_ONE_DRAW "Submit one indexed draw per frame for the normal run" OFF)
option(GFXHANG_SOLID_FRAGMENT "Use a constant-color fragment shader" OFF)
option(GFXHANG_UNINDEXED_DRAW "Use vkCmdDraw instead of indexed drawing" OFF)
option(GFXHANG_LOW_INDEX_VALUES "Keep all generated indices in the range 0..2" OFF)
option(GFXHANG_POINT_LIST "Use point-list primitive topology" OFF)
option(GFXHANG_FORCE_WD_SWITCH_ON_EOP "Force RADV WD_SWITCH_ON_EOP diagnostic on Neo" OFF)
option(GFXHANG_DISABLE_IA_SWITCH_ON_EOI "Disable RADV IA_SWITCH_ON_EOI diagnostic on Neo" OFF)
set(GFXHANG_DRAW_INDEX_COUNT 2232 CACHE STRING "Number of indices in each indexed draw")

if(NOT DEFINED ORBIS_COMPAT_DIR OR NOT PS4)
  message(FATAL_ERROR "configure with -DCMAKE_TOOLCHAIN_FILE=<bundle>/toolchain/orbis-sdk.cmake")
endif()
if(NOT ORBIS_MESA_SRC)
  if(DEFINED ENV{ORBIS_MESA_SRC})
    set(ORBIS_MESA_SRC "$ENV{ORBIS_MESA_SRC}")
  else()
    message(FATAL_ERROR "ORBIS_MESA_SRC is not set; source the bundle's env.sh")
  endif()
endif()
find_program(GLSLANG NAMES glslangValidator glslang REQUIRED)

foreach(stage vert)
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/hang_${stage}_spv.h"
    COMMAND "${GLSLANG}" -V --target-env vulkan1.1
            "${CMAKE_CURRENT_LIST_DIR}/hang.${stage}"
            --vn "hang_${stage}_spv" -o "${CMAKE_CURRENT_BINARY_DIR}/hang_${stage}_spv.h"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/hang.${stage}"
    COMMENT "glslang: hang.${stage}")
endforeach()

if(GFXHANG_SOLID_FRAGMENT)
  set(FRAG_SOURCE "${CMAKE_CURRENT_LIST_DIR}/hang_solid.frag")
else()
  set(FRAG_SOURCE "${CMAKE_CURRENT_LIST_DIR}/hang.frag")
endif()
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/hang_frag_spv.h"
  COMMAND "${GLSLANG}" -V --target-env vulkan1.1
          "${FRAG_SOURCE}" --vn "hang_frag_spv"
          -o "${CMAKE_CURRENT_BINARY_DIR}/hang_frag_spv.h"
  DEPENDS "${FRAG_SOURCE}"
  COMMENT "glslang: ${FRAG_SOURCE}")

add_executable(gfxhang main.c
  "${CMAKE_CURRENT_BINARY_DIR}/hang_vert_spv.h"
  "${CMAKE_CURRENT_BINARY_DIR}/hang_frag_spv.h")
target_include_directories(gfxhang PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if(GFXHANG_SINGLE_SHOT)
  target_compile_definitions(gfxhang PRIVATE GFXHANG_SINGLE_SHOT=1)
endif()
if(GFXHANG_REPEAT_ONE_DRAW)
  target_compile_definitions(gfxhang PRIVATE GFXHANG_REPEAT_ONE_DRAW=1)
endif()
if(GFXHANG_SOLID_FRAGMENT)
  target_compile_definitions(gfxhang PRIVATE GFXHANG_SOLID_FRAGMENT=1)
endif()
if(GFXHANG_UNINDEXED_DRAW)
  target_compile_definitions(gfxhang PRIVATE GFXHANG_UNINDEXED_DRAW=1)
endif()
if(GFXHANG_LOW_INDEX_VALUES)
  target_compile_definitions(gfxhang PRIVATE GFXHANG_LOW_INDEX_VALUES=1)
endif()
if(GFXHANG_POINT_LIST)
  target_compile_definitions(gfxhang PRIVATE GFXHANG_POINT_LIST=1)
endif()
if(GFXHANG_FORCE_WD_SWITCH_ON_EOP)
  target_compile_definitions(gfxhang PRIVATE GFXHANG_FORCE_WD_SWITCH_ON_EOP=1)
endif()
if(GFXHANG_DISABLE_IA_SWITCH_ON_EOI)
  target_compile_definitions(gfxhang PRIVATE GFXHANG_DISABLE_IA_SWITCH_ON_EOI=1)
endif()
target_compile_definitions(gfxhang PRIVATE GFXHANG_DRAW_INDEX_COUNT=${GFXHANG_DRAW_INDEX_COUNT})
if(NOT ORBIS_KIT_DIR)
  set(ORBIS_KIT_DIR "${ORBIS_COMPAT_DIR}")
endif()
add_subdirectory("${ORBIS_KIT_DIR}/vkloader" ps4-vkloader)
target_link_libraries(gfxhang PRIVATE ps4-vkloader)
orbis_compat_verify()
if(COMMAND ps4_create_eboot)
  ps4_create_eboot(gfxhang)
endif()
