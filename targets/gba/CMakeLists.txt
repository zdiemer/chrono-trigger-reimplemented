# ct_gba: the game on the GBA, headless (docs/PLATFORM.md, "The GBA build,
# as built"). Its own project, configured with the GBA's toolchain:
#
#   cmake -S targets/gba -B build/gba -G Ninja -DCMAKE_TOOLCHAIN_FILE=targets/gba/toolchain.cmake
#   cmake --build build/gba
#
# builds build/gba/ct_gba.bin, the program alone (code, constants and the
# data's initial values: no cartridge data). targets/gba/pack.py appends a
# cartridge's resources and a movie to it and writes the .gba.
cmake_minimum_required(VERSION 3.20)
project(ct_gba C ASM)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")

get_filename_component(CT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(CT_WERROR "Treat compiler warnings as errors (CI)" OFF)
add_compile_options(-Wall -Wextra)
if(CT_WERROR)
  add_compile_options(-Werror)
endif()

include(${CT_ROOT}/cmake/game_sources.cmake)
list(TRANSFORM CT_GAME_SOURCES PREPEND "${CT_ROOT}/")

# The game, its state laid out as the original's work RAM (CT_FLAT_STATE),
# the native build's frame and sound services, and the GBA's own: the start,
# the fibers' switch, the memory, the video with no picture and the driver.
add_executable(ct_gba
  ${CT_GAME_SOURCES}
  ${CT_ROOT}/runtime/port/vars_flat.c ${CT_ROOT}/runtime/port/vars_flat_host.c ${CT_ROOT}/runtime/port/state_host.c ${CT_ROOT}/runtime/port/rom_host.c
  ${CT_ROOT}/runtime/port/rom_ram.c ${CT_ROOT}/runtime/port/time_rates.c
  ${CT_ROOT}/runtime/platform/native/timing.c ${CT_ROOT}/runtime/platform/native/input.c
  ${CT_ROOT}/runtime/platform/native/audio.c ${CT_ROOT}/runtime/platform/native/save.c
  ${CT_ROOT}/runtime/platform/voices.c
  ${CT_ROOT}/frontend/sound_native.c ${CT_ROOT}/frontend/movie.c ${CT_ROOT}/frontend/checksum.c
  start.s switch.s fiber.c system.c silent_mixer.c video.c main.c)
target_compile_definitions(ct_gba PRIVATE CT_LEAN CT_FLAT_STATE SOUND_NATIVE_AUDIO_FRAMES=16
  SOUND_NATIVE_MIXER="silent_mixer.h" VID_HOST_NO_WIDER_VIEW)
target_include_directories(ct_gba PRIVATE ${CT_ROOT}/game ${CT_ROOT}/runtime ${CT_ROOT}/frontend
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_options(ct_gba PRIVATE -T ${CMAKE_CURRENT_SOURCE_DIR}/gba.ld -Wl,-Map=ct_gba.map)
set_target_properties(ct_gba PROPERTIES SUFFIX ".elf" LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gba.ld)
add_custom_command(TARGET ct_gba POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary ct_gba.elf ct_gba.bin
  COMMAND ${CMAKE_SIZE} -A ct_gba.elf
  BYPRODUCTS ct_gba.bin)
