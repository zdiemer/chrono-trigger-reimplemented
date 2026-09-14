cmake_minimum_required(VERSION 3.20)
project(chrono_trigger_recomp C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
  add_compile_options(/W4)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
endif()

# Development runtime: interpreter CPU and modeled hardware.
add_library(ct_runtime STATIC
  runtime/cpu/cpu65816.c
  runtime/apu/spc700.c
  runtime/apu/apu.c
  runtime/snes/bus.c
  runtime/snes/ppu.c
  runtime/snes/render.c
  runtime/snes/system.c
  runtime/port/fiber.c
  runtime/port/native.c)
target_include_directories(ct_runtime PUBLIC runtime)

# Ported game code, organized by subsystem.
add_library(ct_game STATIC
  game/registry.c
  game/core/display.c
  game/core/input.c
  game/core/palette.c
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
  game/field/interrupts.c
  game/field/motion.c
  game/field/nmi.c
  game/field/objects.c
  game/menu/frame.c
  game/attract/main_loop.c
  game/battle/buffers.c
  game/battle/display.c
  game/battle/frame.c
  game/battle/math.c
  game/battle/nmi.c
  game/battle/objects.c
  game/battle/sprites.c
  game/battle/stats.c
  game/battle/timers.c
  game/battle/wait.c)
target_include_directories(ct_game PUBLIC game)
target_link_libraries(ct_game PUBLIC ct_runtime)

# Trace and movie formats and PNG/WAV capture, shared with the oracle harness.
add_library(ct_trace STATIC oracle/src/trace.c oracle/src/movie.c oracle/src/media.c)
target_include_directories(ct_trace PUBLIC oracle/src)

add_executable(ct_dev dev/ct_dev.c)
target_link_libraries(ct_dev PRIVATE ct_game ct_runtime ct_trace)

add_executable(ct_fuzz dev/ct_fuzz.c)
target_link_libraries(ct_fuzz PRIVATE ct_game ct_runtime)

# CPU conformance runners (test data: tests/cpu/fetch_sst.py, tests/apu/fetch_sst_spc700.py).
add_executable(sst_runner tests/cpu/sst_runner.c runtime/cpu/cpu65816.c)
add_executable(spc700_runner tests/apu/spc700_runner.c runtime/apu/spc700.c)

if(MINGW)
  target_link_options(ct_dev PRIVATE -static)
endif()
