cmake_minimum_required(VERSION 3.20)
project(chrono_trigger_recomp C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Only little-endian hosts are built and tested (x86-64 and ARM64 are). File
# formats, save states included, are written byte by byte, but no big-endian
# host has checked the rest.
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
  runtime/snes/state.c
  runtime/snes/zst.c
  runtime/port/fiber.c
  runtime/port/native.c
  runtime/port/scaffold_reads.c
  runtime/platform/snes/backend.c
  runtime/platform/direct/audio.c
  runtime/platform/observe.c
  runtime/platform/voices_dsp.c
  runtime/platform/snes/decode.c
  runtime/platform/snes/encodings.c
  runtime/platform/native/render.c)
target_include_directories(ct_runtime PUBLIC runtime PRIVATE game)

# The game's state interface (game/vars.h, game/state.h, game/cpu_io.h,
# game/rom.h) is game/'s, and runtime/port/vars_ram.c and state_io.c are the
# backing that satisfies it against emulated work RAM -- the seam the platform
# services use, for state rather than hardware. It is built twice: ct_vars_checked
# counts every accessor called with an index past what re/ram.toml declares
# (CT_ACCESSOR_CHECKS; docs/DISASSEMBLY.md, "The accessors"), for ct_dev, ct_fuzz
# and the tests; ct_vars has no check at all, for ct_play and ct_drive. Every
# executable that links ct_game links one of them.
foreach(vars ct_vars ct_vars_checked)
  add_library(${vars} STATIC runtime/port/vars_ram.c runtime/port/state_io.c)
  target_include_directories(${vars} PRIVATE game)
  target_link_libraries(${vars} PUBLIC ct_runtime)
endforeach()
target_compile_definitions(ct_vars_checked PRIVATE CT_ACCESSOR_CHECKS)

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
  game/event/bridge.c
  game/event/calls.c
  game/event/chests.c
  game/event/contact.c
  game/event/dialog.c
  game/event/engine.c
  game/event/entries.c
  game/event/facing.c
  game/event/flow.c
  game/event/input.c
  game/event/leaves.c
  game/event/locations.c
  game/event/memory.c
  game/event/movement.c
  game/event/party.c
  game/event/properties.c
  game/event/screen.c
  game/event/sound.c
  game/event/story.c
  game/event/variables.c
  game/field/animation.c
  game/field/bridge.c
  game/field/display.c
  game/field/frame.c
  game/field/graphics.c
  game/field/interrupts.c
  game/field/layers.c
  game/field/line_effects.c
  game/field/leaves.c
  game/field/motion.c
  game/field/object.c
  game/field/nmi.c
  game/field/object_update.c
  game/field/object_view.c
  game/field/party_trail.c
  game/field/window_corners.c
  game/field/scroll.c
  game/field/tile_copy.c
  game/field/walk.c
  game/field/sprites.c
  game/field/window_shape.c
  game/menu/bridge.c
  game/menu/cursor.c
  game/menu/display.c
  game/menu/epoch.c
  game/menu/frame.c
  game/menu/glyphs.c
  game/menu/line_effects.c
  game/menu/load_screen.c
  game/menu/map.c
  game/menu/object_commands.c
  game/menu/object_script.c
  game/menu/objects.c
  game/menu/opening.c
  game/menu/save_screen.c
  game/menu/script.c
  game/menu/script_bridge.c
  game/menu/sound.c
  game/menu/sprites.c
  game/menu/text.c
  game/menu/top_layer.c
  game/menu/vram.c
  game/menu/wave.c
  game/menu/windows.c
  game/sound/driver.c
  game/sound/sequencer.c
  game/sound/songs.c
  game/sound/voices.c
  game/attract/main_loop.c
  game/scene/boot.c
  game/scene/circle.c
  game/scene/frame.c
  game/scene/guards.c
  game/scene/math.c
  game/scene/race.c
  game/scene/race_matrix.c
  game/scene/scaler.c
  game/scene/screen.c
  game/scene/script.c
  game/scene/sound.c
  game/scene/sprites.c
  game/scene/title.c
  game/battle/animation.c
  game/battle/arith.c
  game/battle/battlefield.c
  game/battle/bridge.c
  game/battle/buttons.c
  game/battle/combatants.c
  game/battle/damage.c
  game/battle/digits.c
  game/battle/display.c
  game/battle/effect_animation.c
  game/battle/enemy_actions.c
  game/battle/enemy_conditions.c
  game/battle/enemy_scripts.c
  game/battle/enemy_turn.c
  game/battle/effect_paths.c
  game/battle/frame.c
  game/battle/main_loop.c
  game/battle/menu_window.c
  game/battle/nmi.c
  game/battle/numbers.c
  game/battle/objects.c
  game/battle/positions.c
  game/battle/random.c
  game/battle/reset.c
  game/battle/screen_beam.c
  game/battle/screen_gradient.c
  game/battle/screen_graphics.c
  game/battle/screen_motion.c
  game/battle/screen_palette.c
  game/battle/screen_setup.c
  game/battle/screen_window.c
  game/battle/scripts.c
  game/battle/scroll.c
  game/battle/setup.c
  game/battle/setup_enemies.c
  game/battle/setup_names.c
  game/battle/setup_party.c
  game/battle/setup_reset.c
  game/battle/setup_screen.c
  game/battle/sound.c
  game/battle/sprites.c
  game/battle/status.c
  game/battle/targets.c
  game/battle/draw_objects.c
  game/battle/object_motion.c
  game/battle/tiles.c
  game/battle/uploads.c
  game/battle/timers.c
  game/battle/wait.c
  game/battle/window.c
  game/battle/window_leaves.c)
target_include_directories(ct_game PUBLIC game)
target_link_libraries(ct_game PUBLIC ct_runtime)

# The ports' scaffolding (game/original_effects.h): what they write only because
# the original does and shadow mode or the original's still-generated code
# looks. -DCT_ORIGINAL_EFFECTS=OFF builds without it, which is the shape of a
# native build and is not expected to pass the gate. Either way the game's code
# is also compiled once without it (ct_game_native_shape, objects only), so a
# port whose logic leans on its scaffolding fails to build.
option(CT_ORIGINAL_EFFECTS "Reproduce the original's effects the gate compares (scaffolding)" ON)
if(NOT CT_ORIGINAL_EFFECTS)
  target_compile_definitions(ct_game PRIVATE CT_WITHOUT_ORIGINAL_EFFECTS)
endif()
get_target_property(CT_GAME_SOURCES ct_game SOURCES)
add_library(ct_game_native_shape OBJECT ${CT_GAME_SOURCES})
target_compile_definitions(ct_game_native_shape PRIVATE CT_WITHOUT_ORIGINAL_EFFECTS)
target_link_libraries(ct_game_native_shape PRIVATE ct_game)

# Trace and movie formats and PNG/WAV capture, shared with the oracle harness.
add_library(ct_trace STATIC oracle/src/trace.c oracle/src/movie.c oracle/src/media.c oracle/src/sram_find.c)
target_include_directories(ct_trace PUBLIC oracle/src)

add_executable(ct_dev dev/ct_dev.c dev/as_port.c dev/event_reads.c dev/io_log.c dev/misses.c dev/px_check.c dev/range_report.c
  dev/sound_compare.c dev/sound_lockstep.c dev/sound_state.c dev/vid_check.c frontend/natives.c)
target_include_directories(ct_dev PRIVATE frontend)
target_link_libraries(ct_dev PRIVATE ct_game ct_vars_checked ct_recomp ct_runtime ct_trace)

add_executable(ct_fuzz dev/ct_fuzz.c dev/fuzz.c dev/range_report.c)
target_link_libraries(ct_fuzz PRIVATE ct_game ct_vars_checked ct_runtime)

# The C sound driver against the original on random command streams
# (docs/SOUND.md, "Validating the port").
add_executable(ct_sound_fuzz dev/ct_sound_fuzz.c dev/sound_lockstep.c dev/sound_compare.c dev/sound_state.c
  frontend/sound_host.c)
target_include_directories(ct_sound_fuzz PRIVATE frontend)
target_link_libraries(ct_sound_fuzz PRIVATE ct_game ct_vars_checked ct_runtime)

# The machine as a scriptable server, for authoring input movies from game state
# and testing the shipping mode with the routes that do (tools/movies,
# docs/MOVIES.md). The interpreter by default, like ct_dev --native off; --native
# and --recomp choose native code the way ct_play does (frontend/natives.c).
add_executable(ct_drive dev/ct_drive.c frontend/natives.c)
target_include_directories(ct_drive PRIVATE frontend)
target_link_libraries(ct_drive PRIVATE ct_game ct_vars ct_recomp ct_runtime ct_trace)

# CPU conformance runners (test data: tests/cpu/fetch_sst.py, tests/apu/fetch_sst_spc700.py).
add_executable(sst_runner tests/cpu/sst_runner.c runtime/cpu/cpu65816.c runtime/cpu/cpu_entry.c)
add_executable(spc700_runner tests/apu/spc700_runner.c runtime/apu/spc700.c)
add_executable(dsp_test tests/apu/dsp_test.c runtime/apu/dsp.c)
target_include_directories(dsp_test PRIVATE runtime)
add_executable(fiber_test tests/port/fiber_test.c runtime/port/fiber.c)
target_include_directories(fiber_test PRIVATE runtime)
add_executable(state_test tests/port/state_test.c)
target_link_libraries(state_test PRIVATE ct_runtime)
add_executable(shadow_test tests/port/shadow_test.c)
target_link_libraries(shadow_test PRIVATE ct_runtime)
# How the shipping mode pays a port's time around interrupts (runtime/port/native.c).
add_executable(debt_test tests/port/debt_test.c)
target_link_libraries(debt_test PRIVATE ct_runtime)
# The fuzzer's core (dev/fuzz.c) on made-up routines and ports.
add_executable(fuzz_test tests/port/fuzz_test.c dev/fuzz.c)
target_include_directories(fuzz_test PRIVATE dev)
target_link_libraries(fuzz_test PRIVATE ct_runtime)
# Time attribution (native_time_as) in shadow mode, on a made-up program.
add_executable(time_parts_test tests/port/time_parts_test.c)
target_link_libraries(time_parts_test PRIVATE ct_runtime)
# The C sound driver on a made-up cartridge, its voices as S-DSP registers, and
# the lockstep harness's comparison (docs/SOUND.md).
add_executable(sound_test tests/port/sound_test.c dev/sound_compare.c)
target_include_directories(sound_test PRIVATE dev)
target_link_libraries(sound_test PRIVATE ct_game ct_vars_checked ct_runtime)
# The accessors generated from re/ram.toml, and the page kinds' D mapping.
add_executable(vars_test tests/port/vars_test.c)
target_include_directories(vars_test PRIVATE game)
target_link_libraries(vars_test PRIVATE ct_vars_checked ct_runtime)

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
  add_executable(ct_play frontend/ct_play.c frontend/player.c frontend/natives.c frontend/audio_fill.c frontend/window.c
    frontend/headless.c frontend/files.c frontend/bindings.c frontend/sound_host.c)
  if(NOT CT_PLAYER_INTERPRETER_MODE STREQUAL "ON")
    # The miss report (dev/misses.h) is the player's log of misses.
    target_sources(ct_play PRIVATE dev/misses.c)
    target_include_directories(ct_play PRIVATE dev)
    target_compile_definitions(ct_play PRIVATE
      CT_PLAYER_NO_INTERPRETER=$<IF:$<STREQUAL:${CT_PLAYER_INTERPRETER_MODE},OFF>,2,1>)
  endif()
  message(STATUS "ct_play: interpreter ${CT_PLAYER_INTERPRETER_MODE} (CT_PLAYER_INTERPRETER)")
  target_link_libraries(ct_play PRIVATE ct_game ct_vars ct_recomp ct_runtime ct_trace ${CT_SDL2})
  if(MINGW)
    target_link_options(ct_play PRIVATE -static-libgcc)
  endif()
  # The window driver's bindings on a virtual game controller.
  add_executable(bindings_test tests/frontend/bindings_test.c frontend/bindings.c)
  target_include_directories(bindings_test PRIVATE frontend oracle/src)
  target_link_libraries(bindings_test PRIVATE ${CT_SDL2})
  install(TARGETS ct_play RUNTIME DESTINATION .)
  if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
    install(FILES $<TARGET_RUNTIME_DLLS:ct_play> DESTINATION .)
  endif()
else()
  message(STATUS "SDL2 not found: not building ct_play")
endif()
