cmake_minimum_required(VERSION 3.20)
project(chrono_trigger_reimplemented C)

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

# The lean build: what a small target needs of the program and nothing else.
# It leaves out the overlay crossings' tally, the port's own checks and the
# timing report, optimizes across files as a small target's build would (so
# the state's accessors inline into the game), and plays as the normal build
# does (tools/lean_check.py; docs/PLATFORM.md, "Memory, stacks and per-frame
# work against small targets"). -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
# keeps it to one file at a time.
option(CT_LEAN "Leave out the program's own diagnostics, as a small target would" OFF)
if(CT_LEAN)
  add_compile_definitions(CT_LEAN)
  if(NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT CT_IPO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ${CT_IPO})
  endif()
endif()

# The state as the original lays out its work RAM, a byte for each address
# (runtime/port/vars_flat.c), in place of the typed records: what a target
# whose memory is the original's size keeps (the GBA's, targets/gba/), built
# natively to check that it plays the same (tools/lean_check.py).
option(CT_FLAT_STATE "Keep the game's state as the original's work RAM" OFF)
if(CT_FLAT_STATE)
  add_compile_definitions(CT_FLAT_STATE)
endif()
set(CT_STATE_SOURCES runtime/port/vars_ram.c runtime/port/vars_host.c runtime/port/vars_flat.c
  runtime/port/vars_flat_host.c)

find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
include(CTest)

add_library(ct_media STATIC frontend/movie.c frontend/media.c frontend/checksum.c)
target_include_directories(ct_media PUBLIC frontend)
add_library(ct_platform_native STATIC
  runtime/platform/native/timing.c runtime/platform/native/input.c
  runtime/platform/native/video.c runtime/platform/native/audio.c runtime/platform/native/save.c
  runtime/platform/observe.c runtime/platform/voices.c
  runtime/platform/native/mixer.c runtime/platform/native/snapshot.c runtime/port/fiber.c runtime/port/arena.c)
target_include_directories(ct_platform_native PUBLIC runtime)
add_library(ct_state_host STATIC ${CT_STATE_SOURCES}
  runtime/port/state_host.c runtime/port/rom_host.c runtime/port/rom_ram.c runtime/port/time_rates.c)
target_include_directories(ct_state_host PUBLIC game)
target_link_libraries(ct_state_host PUBLIC ct_platform_native)

# The game calls native state and platform services directly.
include(cmake/game_sources.cmake)
add_library(ct_game STATIC ${CT_GAME_SOURCES})
target_link_libraries(ct_game PUBLIC ct_state_host)
# The script languages as a build without the scripts' text compiles them (the
# 65816's): the commands' names and measures left out (game/script_text.h).
get_target_property(CT_GAME_SOURCES ct_game SOURCES)
list(FILTER CT_GAME_SOURCES INCLUDE REGEX "^game/(event/|battle/script_|battle/numbers|battle/enemy_|menu/(object_)?script)")
add_library(ct_languages_without_text OBJECT ${CT_GAME_SOURCES})
target_compile_definitions(ct_languages_without_text PRIVATE CT_SCRIPT_TEXT=0)
target_link_libraries(ct_languages_without_text PRIVATE ct_state_host)
add_executable(ct_native frontend/ct_native.c frontend/measure.c frontend/route_driver.c frontend/snapshot.c
  frontend/presentation.c frontend/storage.c frontend/asset_files.c frontend/track.c frontend/sram_find.c
  frontend/file_store.c frontend/assets.c frontend/decoded.c frontend/palettes.c frontend/fonts.c
  frontend/lz.c frontend/packed.c frontend/tiles.c frontend/maps.c frontend/scripts.c frontend/text.c
  frontend/commands.c frontend/battle_language.c frontend/menu_language.c frontend/enemy_scripts.c
  frontend/records.c frontend/sound_native.c
  runtime/platform/native/render.c)
target_include_directories(ct_native PRIVATE frontend)
target_link_libraries(ct_native PRIVATE ct_game ct_media)

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
if(CT_SDL2)
  target_sources(ct_native PRIVATE frontend/window.c frontend/audio_fill.c frontend/bindings.c frontend/bindings_config.c)
  target_compile_definitions(ct_native PRIVATE CT_HAVE_SDL)
  target_link_libraries(ct_native PRIVATE ${CT_SDL2})
else()
  message(STATUS "SDL2 not found: ct_native is headless")
endif()
if(MINGW)
  target_link_options(ct_native PRIVATE -static-libgcc)
endif()
# A snapshot file holds the fibers' stacks, whose return addresses and
# pointers name the program's code and statics: loaded in another run, they
# hold only where the image is where it was (frontend/snapshot.h). Elsewhere a
# snapshot file loads only in the run that wrote it.
if(MINGW)
  target_link_options(ct_native PRIVATE -Wl,--disable-dynamicbase,--disable-high-entropy-va)
endif()
install(TARGETS ct_native RUNTIME DESTINATION .)
if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
  install(FILES $<TARGET_RUNTIME_DLLS:ct_native> DESTINATION .)
endif()

if(BUILD_TESTING)
  add_executable(fiber_test tests/port/fiber_test.c runtime/port/fiber.c runtime/port/arena.c)
  target_include_directories(fiber_test PRIVATE runtime)
  add_executable(route_driver_test tests/frontend/route_driver_test.c frontend/route_driver.c)
  target_include_directories(route_driver_test PRIVATE frontend)
  target_link_libraries(route_driver_test PRIVATE ct_media)
  add_executable(file_store_test tests/frontend/file_store_test.c)
  add_executable(timing_test tests/port/timing_test.c)
  target_link_libraries(timing_test PRIVATE ct_platform_native)
  add_executable(video_test tests/port/video_test.c runtime/platform/native/render.c)
  target_link_libraries(video_test PRIVATE ct_platform_native)
  add_executable(tasks_test tests/port/tasks_test.c game/core/tasks.c)
  target_include_directories(tasks_test PRIVATE game)
  target_link_libraries(tasks_test PRIVATE ct_platform_native)
  add_executable(native_time_test tests/port/native_time_test.c runtime/port/state_host.c ${CT_STATE_SOURCES}
  runtime/port/arena.c)
  target_include_directories(native_time_test PRIVATE game runtime)
  add_executable(program_test tests/port/program_test.c game/core/program.c game/core/sprite_table.c)
  target_link_libraries(program_test PRIVATE ct_state_host)
  # vars_host looks inside the typed records, which the flat state hasn't.
  set(CT_VARS_HOST_TEST "")
  if(NOT CT_FLAT_STATE)
    set(CT_VARS_HOST_TEST vars_host)
    add_executable(vars_host_test tests/port/vars_host_test.c game/menu/saves.c game/data/asset_catalogue.c)
    # Preserve bounds diagnostics when an indexed variable moves into a struct.
    add_library(ct_state_host_checked STATIC ${CT_STATE_SOURCES}
      runtime/port/state_host.c runtime/port/rom_host.c runtime/port/rom_ram.c runtime/port/time_rates.c)
    target_compile_definitions(ct_state_host_checked PRIVATE CT_ACCESSOR_CHECKS)
    target_include_directories(ct_state_host_checked PUBLIC game)
    target_link_libraries(ct_state_host_checked PUBLIC ct_platform_native)
    target_link_libraries(vars_host_test PRIVATE ct_state_host_checked)
  endif()
  add_executable(unpack_time_test tests/port/unpack_time_test.c game/data/decompress.c)
  target_link_libraries(unpack_time_test PRIVATE ct_state_host)
  add_executable(assets_test tests/frontend/assets_test.c frontend/assets.c game/data/assets.c game/data/asset_catalogue.c)
  target_include_directories(assets_test PRIVATE frontend game runtime)
  target_link_libraries(assets_test PRIVATE ct_platform_native)
  add_executable(palettes_test tests/frontend/palettes_test.c frontend/decoded.c frontend/palettes.c frontend/assets.c
    game/data/asset_catalogue.c)
  target_include_directories(palettes_test PRIVATE frontend game)
  add_executable(fonts_test tests/frontend/fonts_test.c frontend/decoded.c frontend/fonts.c frontend/assets.c
    game/data/asset_catalogue.c)
  target_include_directories(fonts_test PRIVATE frontend game)
  add_executable(lz_test tests/frontend/lz_test.c frontend/lz.c frontend/assets.c game/data/asset_catalogue.c)
  target_include_directories(lz_test PRIVATE frontend game)
  add_executable(tiles_test tests/frontend/tiles_test.c frontend/decoded.c frontend/tiles.c frontend/packed.c
    frontend/lz.c frontend/assets.c game/data/asset_catalogue.c)
  target_include_directories(tiles_test PRIVATE frontend game)
  add_executable(maps_test tests/frontend/maps_test.c frontend/decoded.c frontend/maps.c frontend/packed.c
    frontend/lz.c frontend/assets.c game/data/asset_catalogue.c)
  target_include_directories(maps_test PRIVATE frontend game)
  add_executable(scripts_test tests/frontend/scripts_test.c frontend/decoded.c frontend/scripts.c frontend/text.c frontend/packed.c
    frontend/lz.c frontend/assets.c)
  target_include_directories(scripts_test PRIVATE frontend game)
  target_link_libraries(scripts_test PRIVATE ct_game)
  add_executable(commands_test tests/frontend/commands_test.c frontend/decoded.c frontend/commands.c frontend/packed.c frontend/lz.c
    frontend/battle_language.c frontend/menu_language.c frontend/enemy_scripts.c frontend/assets.c)
  target_include_directories(commands_test PRIVATE frontend game)
  target_link_libraries(commands_test PRIVATE ct_game)
  add_executable(text_test tests/frontend/text_test.c frontend/decoded.c frontend/text.c frontend/assets.c
    game/data/asset_catalogue.c)
  target_include_directories(text_test PRIVATE frontend game)
  add_executable(records_test tests/frontend/records_test.c frontend/decoded.c frontend/records.c frontend/assets.c
    game/data/asset_catalogue.c)
  target_include_directories(records_test PRIVATE frontend game)
  add_executable(player_files_test tests/frontend/player_files_test.c frontend/storage.c)
  target_include_directories(player_files_test PRIVATE frontend)
  add_executable(presentation_test tests/frontend/presentation_test.c frontend/presentation.c)
  target_include_directories(presentation_test PRIVATE frontend)
  add_executable(recorder_test tests/frontend/recorder_test.c frontend/track.c)
  target_link_libraries(recorder_test PRIVATE ct_media)
  add_executable(sound_setting_test tests/port/sound_setting_test.c)
  target_link_libraries(sound_setting_test PRIVATE ct_game)
  add_executable(palette_sharers_test tests/port/palette_sharers_test.c)
  target_link_libraries(palette_sharers_test PRIVATE ct_game)
  add_executable(overworld_leader_test tests/port/overworld_leader_test.c)
  target_link_libraries(overworld_leader_test PRIVATE ct_game)
  add_executable(object_talk_test tests/port/object_talk_test.c)
  target_link_libraries(object_talk_test PRIVATE ct_game)
  add_executable(party_trail_test tests/port/party_trail_test.c)
  target_link_libraries(party_trail_test PRIVATE ct_game)
  add_executable(menu_fade_test tests/port/menu_fade_test.c)
  target_link_libraries(menu_fade_test PRIVATE ct_game)
  add_executable(screen_command_test tests/port/screen_command_test.c)
  target_include_directories(screen_command_test PRIVATE runtime)
  target_link_libraries(screen_command_test PRIVATE ct_game)
  add_executable(sound_native_test tests/frontend/sound_native_test.c frontend/sound_native.c)
  target_include_directories(sound_native_test PRIVATE frontend runtime)
  target_link_libraries(sound_native_test PRIVATE ct_game)
  add_executable(enemy_movement_test tests/port/enemy_movement_test.c)
  target_include_directories(enemy_movement_test PRIVATE game runtime)
  target_link_libraries(enemy_movement_test PRIVATE ct_game)
  foreach(test player_files presentation recorder fiber route_driver file_store timing video tasks native_time program ${CT_VARS_HOST_TEST} unpack_time assets palettes fonts lz tiles maps scripts commands text records sound_setting sound_native object_talk party_trail enemy_movement menu_fade screen_command palette_sharers overworld_leader)
    if(test STREQUAL "file_store")
      add_test(NAME file_store COMMAND file_store_test ${CMAKE_CURRENT_BINARY_DIR}/file-store-test.bin)
    else()
      add_test(NAME ${test} COMMAND ${test}_test)
    endif()
    set_tests_properties(${test} PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endforeach()
  # The decoded maps drawn from their texts against ct_native's pictures (with the ROM staged).
  add_test(NAME map_pictures COMMAND ${Python3_EXECUTABLE} tests/frontend/map_pictures.py $<TARGET_FILE:ct_native>)
  set_tests_properties(map_pictures PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME measure COMMAND ${Python3_EXECUTABLE} tools/measure.py --ct-native $<TARGET_FILE:ct_native> --check)
  set_tests_properties(measure PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  # ct_native's reporting options only look: play is the same with each of them (with the ROM staged).
  # It holds the options to the timing report's, which the lean build hasn't.
  if(NOT CT_LEAN)
    add_test(NAME observers COMMAND ${Python3_EXECUTABLE} tests/frontend/observers.py $<TARGET_FILE:ct_native>)
    set_tests_properties(observers PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
  if(CT_SDL2)
    add_executable(bindings_test tests/frontend/bindings_test.c frontend/bindings.c frontend/bindings_config.c frontend/storage.c)
    target_include_directories(bindings_test PRIVATE frontend)
    target_link_libraries(bindings_test PRIVATE ${CT_SDL2})
    add_test(NAME bindings COMMAND bindings_test)
  endif()
endif()

file(GLOB_RECURSE CT_SNAPSHOT_SOURCES CONFIGURE_DEPENDS game/*.c game/*.h runtime/*.c runtime/*.h frontend/*.c frontend/*.h)
set(CT_SNAPSHOT_HASHES "")
foreach(source IN LISTS CT_SNAPSHOT_SOURCES)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
  file(MD5 "${source}" digest)
  string(APPEND CT_SNAPSHOT_HASHES "${digest}")
endforeach()
file(MD5 "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" digest)
string(APPEND CT_SNAPSHOT_HASHES "${digest}")
string(MD5 CT_SNAPSHOT_BUILD "${CT_SNAPSHOT_HASHES}")
configure_file(cmake/native_build.h.in native_build.h @ONLY)
target_include_directories(ct_native PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
