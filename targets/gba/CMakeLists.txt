# ct_gba: the game on the GBA, headless (docs/PLATFORM.md, "The GBA build,
# as built"). Its own project, configured with the GBA's toolchain:
#
#   cmake -S targets/gba -B build/gba -G Ninja -DCMAKE_TOOLCHAIN_FILE=targets/gba/toolchain/toolchain.cmake
#   cmake --build build/gba
#
# builds build/gba/ct_gba.bin, the program alone (code, constants and the
# data's initial values: no cartridge data). targets/gba/host/pack.py appends a
# cartridge's resources to it and writes the .gba.
cmake_minimum_required(VERSION 3.20)
project(ct_gba C ASM)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
# Optimized for speed and across files (docs/PLATFORM.md, "The GBA build, as
# built", measures -O2, -O3 and -Os with and without it).
set(CMAKE_C_FLAGS_RELEASE "-O3")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

get_filename_component(CT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(CT_WERROR "Treat compiler warnings as errors (CI)" OFF)
add_compile_options(-Wall -Wextra)
if(CT_WERROR)
  add_compile_options(-Werror)
endif()

include(${CT_ROOT}/cmake/game_sources.cmake)
list(TRANSFORM CT_GAME_SOURCES PREPEND "${CT_ROOT}/")

# The game, its state laid out as the original's work RAM (CT_FLAT_STATE),
# the native build's frame and sound services, and the GBA's own
# (targets/gba/README.md): the start, the driver and its system, and the
# services' backends.
add_executable(ct_gba
  ${CT_GAME_SOURCES}
  ${CT_ROOT}/runtime/port/vars_flat.c ${CT_ROOT}/runtime/port/vars_flat_host.c ${CT_ROOT}/runtime/port/state_host.c ${CT_ROOT}/runtime/port/rom_host.c
  ${CT_ROOT}/runtime/port/rom_ram.c
  ${CT_ROOT}/runtime/platform/native/timing.c ${CT_ROOT}/runtime/platform/native/input.c
  ${CT_ROOT}/runtime/platform/native/audio.c ${CT_ROOT}/runtime/platform/native/save.c
  ${CT_ROOT}/runtime/platform/voices.c
  ${CT_ROOT}/frontend/sound_native.c ${CT_ROOT}/frontend/checksum.c
  toolchain/start.s system/switch.s system/fiber.c system/frames.c system/interrupts.c system/nested.s system/system.c system/main.c
  system/copies.s system/divide.s services/units.s system/drive.c system/keypad.c services/cartridge_save.c services/mixer.c services/mix.s services/direct_sound.c services/video.c)
# The C library's system calls are called only from the library, which
# optimizing across files doesn't see: compiled apart, they stay.
set_source_files_properties(system/system.c PROPERTIES COMPILE_OPTIONS -fno-lto)
# The state's accesses run from fast RAM
# (toolchain/fast_code.ld), where ARM's 32-bit instructions take no longer to fetch
# than Thumb's; their few cold functions run as ARM from the cartridge.
set_source_files_properties(${CT_ROOT}/runtime/port/state_host.c ${CT_ROOT}/runtime/port/vars_flat.c
  PROPERTIES COMPILE_OPTIONS -marm)
# Work RAM from $7F:B000 on in the cartridge's SRAM (services/cartridge_save.h).
target_compile_definitions(ct_gba PRIVATE CT_LEAN CT_UNTIMED CT_FLAT_STATE VARS_FLAT_COLD_FROM=0x1B000u SOUND_NATIVE_AUDIO_FRAMES=1
  SOUND_NATIVE_MIXER="services/mixer.h" SOUND_NATIVE_MIXER_PLAYS VID_HOST_NO_WIDER_VIEW)
target_include_directories(ct_gba PRIVATE ${CT_ROOT}/game ${CT_ROOT}/runtime ${CT_ROOT}/frontend
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_options(ct_gba PRIVATE -T ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/gba.ld -L${CMAKE_CURRENT_SOURCE_DIR}/toolchain -Wl,-Map=ct_gba.map)
set_target_properties(ct_gba PROPERTIES SUFFIX ".elf"
  LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/toolchain/gba.ld;${CMAKE_CURRENT_SOURCE_DIR}/toolchain/fast_code.ld")
add_custom_command(TARGET ct_gba POST_BUILD
  COMMAND ${CMAKE_COMMAND} -DLIST=${CMAKE_CURRENT_SOURCE_DIR}/toolchain/fast_code.ld -DMAP=ct_gba.map
    -DROOT=${CT_ROOT} -P ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/check_fast_code.cmake
  COMMAND ${CMAKE_OBJCOPY} -O binary ct_gba.elf ct_gba.bin
  COMMAND ${CMAKE_SIZE} -A ct_gba.elf
  BYPRODUCTS ct_gba.bin)
