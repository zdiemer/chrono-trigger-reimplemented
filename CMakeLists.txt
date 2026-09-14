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
  runtime/snes/system.c)
target_include_directories(ct_runtime PUBLIC runtime)

# Trace and movie formats, shared with the oracle harness.
add_library(ct_trace STATIC oracle/src/trace.c oracle/src/movie.c)
target_include_directories(ct_trace PUBLIC oracle/src)

add_executable(ct_dev dev/ct_dev.c)
target_link_libraries(ct_dev PRIVATE ct_runtime ct_trace)

# CPU conformance runners (test data: tests/cpu/fetch_sst.py, tests/apu/fetch_sst_spc700.py).
add_executable(sst_runner tests/cpu/sst_runner.c runtime/cpu/cpu65816.c)
add_executable(spc700_runner tests/apu/spc700_runner.c runtime/apu/spc700.c)

if(MINGW)
  target_link_options(ct_dev PRIVATE -static)
endif()
