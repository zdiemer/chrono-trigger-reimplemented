cmake_minimum_required(VERSION 3.20)
project(chrono_trigger_recomp C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Only little-endian hosts are built and tested (x86-64 and ARM64 are). File
# formats are written byte by byte, but save states are raw memory images, and
# no big-endian host has checked the rest.
if(CMAKE_C_BYTE_ORDER STREQUAL "BIG_ENDIAN")
  message(WARNING "big-endian host: untested, expect differences from the original")
endif()

option(CT_WERROR "Treat compiler warnings as errors (CI)" OFF)
if(MSVC)
  add_compile_options(/W4)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  if(CT_WERROR)
    add_compile_options(/WX)
  endif()
else()
  add_compile_options(-Wall -Wextra)
  if(CT_WERROR)
    add_compile_options(-Werror)
  endif()
endif()

# Development runtime: interpreter CPU and modeled hardware.
add_library(ct_runtime STATIC
  runtime/cpu/cpu65816.c
  runtime/cpu/cpu_entry.c
  runtime/apu/spc700.c
  runtime/apu/apu.c
  runtime/apu/dsp.c
  runtime/snes/bus.c
  runtime/snes/ppu.c
  runtime/snes/render.c
  runtime/snes/system.c
  runtime/snes/zst.c
  runtime/port/fiber.c
  runtime/port/native.c
  runtime/platform/snes/backend.c
  runtime/platform/direct/audio.c)
target_include_directories(ct_runtime PUBLIC runtime)

# Ported game code, organized by subsystem.
add_library(ct_game STATIC
  game/registry.c
  game/core/display.c
  game/core/input.c
  game/core/memory.c
  game/core/palette.c
  game/core/sound_driver.c
  game/core/tile_animation.c
  game/data/decompress.c
  game/event/animation.c
  game/event/arithmetic.c
  game/event/calls.c
  game/event/dialog.c
  game/event/engine.c
  game/event/facing.c
  game/event/flow.c
  game/event/input.c
  game/event/locations.c
  game/event/memory.c
  game/event/movement.c
  game/event/properties.c
  game/event/screen.c
  game/event/sound.c
  game/event/variables.c
  game/field/frame.c
  game/field/graphics.c
  game/field/interrupts.c
  game/field/layers.c
  game/field/motion.c
  game/field/nmi.c
  game/field/object_update.c
  game/field/objects.c
  game/field/scroll.c
  game/field/sprites.c
  game/field/window_shape.c
  game/menu/frame.c
  game/menu/glyphs.c
  game/menu/objects.c
  game/menu/save_screen.c
  game/menu/sound.c
  game/menu/text.c
  game/menu/vram.c
  game/attract/main_loop.c
  game/scene/bitmap.c
  game/scene/math.c
  game/scene/nmi.c
  game/scene/program.c
  game/scene/sound.c
  game/scene/sprites.c
  game/battle/animation.c
  game/battle/buffers.c
  game/battle/display.c
  game/battle/frame.c
  game/battle/gauges.c
  game/battle/main_loop.c
  game/battle/math.c
  game/battle/menu_window.c
  game/battle/nmi.c
  game/battle/numbers.c
  game/battle/objects.c
  game/battle/scripts.c
  game/battle/setup.c
  game/battle/sound.c
  game/battle/sprites.c
  game/battle/stats.c
  game/battle/tiles.c
  game/battle/timers.c
  game/battle/wait.c
  game/battle/window.c)
target_include_directories(ct_game PUBLIC game)
target_link_libraries(ct_game PUBLIC ct_runtime)

# Trace and movie formats and PNG/WAV capture, shared with the oracle harness.
add_library(ct_trace STATIC oracle/src/trace.c oracle/src/movie.c oracle/src/media.c oracle/src/sram_find.c)
target_include_directories(ct_trace PUBLIC oracle/src)

add_executable(ct_dev dev/ct_dev.c dev/io_log.c dev/misses.c)
target_link_libraries(ct_dev PRIVATE ct_game ct_recomp ct_runtime ct_trace)

add_executable(ct_fuzz dev/ct_fuzz.c)
target_link_libraries(ct_fuzz PRIVATE ct_game ct_runtime)

# The machine as a scriptable server, for authoring input movies from game state
# (tools/movies, docs/MOVIES.md). Interpreter only, like ct_dev --native off.
add_executable(ct_drive dev/ct_drive.c)
target_link_libraries(ct_drive PRIVATE ct_runtime ct_trace)

# CPU conformance runners (test data: tests/cpu/fetch_sst.py, tests/apu/fetch_sst_spc700.py).
add_executable(sst_runner tests/cpu/sst_runner.c runtime/cpu/cpu65816.c runtime/cpu/cpu_entry.c)
add_executable(spc700_runner tests/apu/spc700_runner.c runtime/apu/spc700.c)
add_executable(dsp_test tests/apu/dsp_test.c runtime/apu/dsp.c)
target_include_directories(dsp_test PRIVATE runtime)
add_executable(fiber_test tests/port/fiber_test.c runtime/port/fiber.c)
target_include_directories(fiber_test PRIVATE runtime)

# Instruction translation (tools/recomp, docs/RECOMP.md), with C generated into
# the build tree for every opcode and context: sst_recomp_runner runs it on the
# same test data as sst_runner; recomp_bus_diff binds it to the development
# system's bus and compares it with the interpreter there.
find_package(Python3 3.11 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  set(RECOMP_SST_DIR ${CMAKE_CURRENT_BINARY_DIR}/recomp_sst)
  set(RECOMP_SST_SOURCES ${RECOMP_SST_DIR}/table.c)
  foreach(i 0 1 2 3 4 5 6 7 8 9 a b c d e f)
    list(APPEND RECOMP_SST_SOURCES ${RECOMP_SST_DIR}/ops_${i}.c)
  endforeach()
  file(GLOB RECOMP_TOOL_SOURCES CONFIGURE_DEPENDS tools/recomp/*.py)
  add_custom_command(OUTPUT ${RECOMP_SST_SOURCES}
    COMMAND ${Python3_EXECUTABLE} -m tools.recomp.sst --out ${RECOMP_SST_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${RECOMP_TOOL_SOURCES} tools/disasm/cpu.py
    COMMENT "Translating every opcode for sst_recomp_runner")
  add_executable(sst_recomp_runner tests/cpu/sst_runner.c runtime/cpu/cpu65816.c runtime/cpu/cpu_entry.c ${RECOMP_SST_SOURCES})
  target_compile_definitions(sst_recomp_runner PRIVATE SST_RECOMP)
  target_include_directories(sst_recomp_runner PRIVATE runtime tests/cpu)
  add_executable(recomp_bus_diff tests/cpu/recomp_bus_diff.c ${RECOMP_SST_SOURCES})
  target_compile_definitions(recomp_bus_diff PRIVATE SST_RECOMP_SNES)
  target_include_directories(recomp_bus_diff PRIVATE tests/cpu)
  target_link_libraries(recomp_bus_diff PRIVATE ct_runtime)
else()
  message(STATUS "Python 3.11+ not found: not building sst_recomp_runner and recomp_bus_diff")
endif()

# Blocks generated from the ROM for ct_dev and ct_play (cmake/recomp.cmake).
include(cmake/recomp.cmake)

# Matching source reassembled from the ROM and checked against it (cmake/matching.cmake).
include(cmake/matching.cmake)

if(MINGW)
  target_link_options(ct_dev PRIVATE -static)
endif()

# Player frontend (docs/PLAYING.md). Needs SDL2; skipped when it isn't found.
find_package(SDL2 CONFIG QUIET)
if(TARGET SDL2::SDL2)
  set(CT_SDL2 SDL2::SDL2)
else()
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
  endif()
  if(TARGET PkgConfig::SDL2)
    set(CT_SDL2 PkgConfig::SDL2)
  endif()
endif()

# CT_PLAYER_INTERPRETER: what ct_play does with the 65816 interpreter (docs/PLAYING.md,
# "Without the interpreter"). ON: linked and used as in ct_dev (every --native mode).
# FALLBACK: generated code runs everything; the interpreter is linked but runs only
# instructions nothing was generated for, which are logged to <rom name>.misses.txt.
# OFF: not linked at all; such an instruction stops the game. ct_dev, ct_fuzz and the
# test runners always have the interpreter, the reference they check against.
set(CT_PLAYER_INTERPRETER ON CACHE STRING "ct_play's 65816 interpreter: ON, FALLBACK (misses only, logged) or OFF")
set_property(CACHE CT_PLAYER_INTERPRETER PROPERTY STRINGS ON FALLBACK OFF)
string(TOUPPER "${CT_PLAYER_INTERPRETER}" CT_PLAYER_INTERPRETER_MODE)
if(NOT CT_PLAYER_INTERPRETER_MODE MATCHES "^(ON|FALLBACK|OFF)$")
  message(FATAL_ERROR "CT_PLAYER_INTERPRETER must be ON, FALLBACK or OFF, not ${CT_PLAYER_INTERPRETER}")
endif()

if(CT_SDL2)
  add_executable(ct_play frontend/ct_play.c)
  if(NOT CT_PLAYER_INTERPRETER_MODE STREQUAL "ON")
    # The miss report (dev/misses.h) is the player's log of misses.
    target_sources(ct_play PRIVATE dev/misses.c)
    target_include_directories(ct_play PRIVATE dev)
    target_compile_definitions(ct_play PRIVATE
      CT_PLAYER_NO_INTERPRETER=$<IF:$<STREQUAL:${CT_PLAYER_INTERPRETER_MODE},OFF>,2,1>)
  endif()
  message(STATUS "ct_play: interpreter ${CT_PLAYER_INTERPRETER_MODE} (CT_PLAYER_INTERPRETER)")
  target_link_libraries(ct_play PRIVATE ct_game ct_recomp ct_runtime ct_trace ${CT_SDL2})
  if(MINGW)
    target_link_options(ct_play PRIVATE -static-libgcc)
  endif()
  install(TARGETS ct_play RUNTIME DESTINATION .)
  if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
    install(FILES $<TARGET_RUNTIME_DLLS:ct_play> DESTINATION .)
  endif()
else()
  message(STATUS "SDL2 not found: not building ct_play")
endif()
