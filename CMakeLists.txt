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
add_library(ct_state_host STATIC runtime/port/vars_ram.c runtime/port/vars_host.c
  runtime/port/state_host.c runtime/port/rom_host.c runtime/port/time_rates.c)
target_include_directories(ct_state_host PUBLIC game)
target_link_libraries(ct_state_host PUBLIC ct_platform_native)

# The game calls native state and platform services directly.
add_library(ct_game STATIC
  game/core/program.c
  game/core/tasks.c
  game/core/display.c
  game/core/input.c
  game/core/palette.c
  game/core/sound_commands.c
  game/core/tile_animation.c
  game/core/sprite_table.c
  game/core/panel_layout.c
  game/menu/panels_across.c
  game/data/assets.c
  game/data/asset_catalogue.c
  game/data/decompress.c
  game/event/animation.c
  game/event/calls.c
  game/event/characters.c
  game/event/chests.c
  game/event/colours.c
  game/event/contact.c
  game/event/dialog.c
  game/event/engine.c
  game/event/facing.c
  game/event/flow.c
  game/event/input.c
  game/event/items.c
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
  game/field/battle_return.c
  game/field/battle_start.c
  game/field/cast.c
  game/field/palettes.c
  game/field/menus.c
  game/field/start.c
  game/field/display.c
  game/field/frames.c
  game/field/script_turns.c
  game/field/requests.c
  game/field/location.c
  game/field/main_loop.c
  game/field/screen_effects.c
  game/field/script_layers.c
  game/field/graphics.c
  game/field/interrupts.c
  game/field/layers.c
  game/field/line_effects.c
  game/field/motion.c
  game/field/object.c
  game/field/nmi.c
  game/field/object_update.c
  game/field/object_view.c
  game/field/objects_start.c
  game/field/party_trail.c
  game/field/streaming.c
  game/field/margin_objects.c
  game/field/switches.c
  game/field/dialog_window.c
  game/field/map.c
  game/field/screen_setup.c
  game/field/scroll.c
  game/field/tile_copy.c
  game/field/walk.c
  game/field/sounds.c
  game/field/sprites.c
  game/field/window_shape.c
  game/menu/cursor.c
  game/menu/description.c
  game/menu/digits.c
  game/menu/display.c
  game/menu/epoch.c
  game/menu/equipment.c
  game/menu/equipment_bridge.c
  game/menu/equipment_windows.c
  game/menu/exchange.c
  game/menu/fades.c
  game/menu/figures.c
  game/menu/frame.c
  game/menu/tasks.c
  game/menu/gate_screen.c
  game/menu/glyphs.c
  game/menu/graphics.c
  game/menu/healing.c
  game/menu/held_cursors.c
  game/menu/item_screen.c
  game/menu/item_screen_bridge.c
  game/menu/items.c
  game/menu/line_effects.c
  game/menu/lists.c
  game/menu/load_screen.c
  game/menu/main_menu.c
  game/menu/members.c
  game/menu/naming.c
  game/menu/map.c
  game/menu/map_scenes.c
  game/menu/object_commands.c
  game/menu/object_script.c
  game/menu/objects.c
  game/menu/opening.c
  game/menu/party.c
  game/menu/save_screen.c
  game/menu/saves.c
  game/menu/saving.c
  game/menu/script.c
  game/menu/screen_objects.c
  game/menu/screens.c
  game/menu/settings_screen.c
  game/menu/settings_screen_bridge.c
  game/menu/shop.c
  game/menu/shop_bridge.c
  game/menu/shop_steps.c
  game/menu/shop_equipment.c
  game/menu/sound.c
  game/menu/sprites.c
  game/menu/tech_screen.c
  game/menu/tech_screen_bridge.c
  game/menu/techs.c
  game/menu/text.c
  game/menu/tiles.c
  game/menu/typeset.c
  game/menu/top_layer.c
  game/menu/handlers.c
  game/overworld/handlers.c
  game/menu/turning_world.c
  game/menu/vram.c
  game/menu/wave.c
  game/menu/windows.c
  game/overworld/dactyl.c
  game/overworld/epoch.c
  game/overworld/followers.c
  game/overworld/main_loop.c
  game/overworld/music.c
  game/overworld/party.c
  game/overworld/places.c
  game/overworld/setup.c
  game/overworld/sign.c
  game/overworld/terrain.c
  game/overworld/vehicles.c
  game/overworld/walk.c
  game/sound/driver.c
  game/sound/loads.c
  game/sound/sequencer.c
  game/sound/songs.c
  game/sound/voices.c
  game/warp/circle.c
  game/warp/picture.c
  game/warp/warp.c
  game/scene/boot.c
  game/scene/bridge.c
  game/scene/circle.c
  game/scene/curtain.c
  game/scene/plane.c
  game/scene/frame.c
  game/scene/lavos_falls.c
  game/scene/epoch_dive.c
  game/scene/math.c
  game/scene/perspective.c
  game/scene/planet.c
  game/scene/beyond_time.c
  game/scene/program.c
  game/scene/race.c
  game/scene/race_camera.c
  game/scene/race_drawing.c
  game/scene/race_matrix.c
  game/scene/race_projection.c
  game/scene/race_rules.c
  game/scene/scaler.c
  game/scene/screen.c
  game/scene/script.c
  game/scene/sound.c
  game/scene/sprites.c
  game/scene/credit_pages.c
  game/scene/rising_palace.c
  game/scene/turning.c
  game/scene/staff_roll.c
  game/scene/stars.c
  game/scene/captions.c
  game/scene/random.c
  game/scene/endings.c
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
  game/battle/effect_amounts.c
  game/battle/effect_animation.c
  game/battle/effect_modifiers.c
  game/battle/enemy_actions.c
  game/battle/enemy_conditions.c
  game/battle/enemy_scripts.c
  game/battle/enemy_turn.c
  game/battle/effect_paths.c
  game/battle/effect_statuses.c
  game/battle/status_timers.c
  game/battle/steps.c
  game/battle/effect_steal.c
  game/battle/effect_work.c
  game/battle/inventory.c
  game/battle/party_work.c
  game/battle/frame.c
  game/battle/from_field.c
  game/battle/main_loop.c
  game/battle/aim.c
  game/battle/member_turn.c
  game/battle/enemy_movement.c
  game/battle/battle_end.c
  game/battle/party_turns.c
  game/battle/menu_window.c
  game/battle/menu_input.c
  game/battle/menu_line.c
  game/battle/party_poses.c
  game/battle/action_showing.c
  game/battle/action_animation.c
  game/battle/frame_step.c
  game/battle/pose_looks.c
  game/battle/menu_bridge.c
  game/battle/menu_lists.c
  game/battle/name_tiles.c
  game/battle/target_choice.c
  game/battle/area_search.c
  game/battle/nmi.c
  game/battle/numbers.c
  game/battle/objects.c
  game/battle/opening.c
  game/battle/positions.c
  game/battle/random.c
  game/battle/reset.c
  game/battle/screen_beam.c
  game/battle/screen_between.c
  game/battle/screen_circles.c
  game/battle/screen_divide.c
  game/battle/screen_gradient.c
  game/battle/screen_graphics.c
  game/battle/screen_layers.c
  game/battle/screen_motion.c
  game/battle/screen_palette.c
  game/battle/screen_script.c
  game/battle/screen_settings.c
  game/battle/screen_setup.c
  game/battle/screen_shapes.c
  game/battle/screen_specials.c
  game/battle/screen_sides.c
  game/battle/screen_triangles.c
  game/battle/screen_waves.c
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
  game/battle/message.c
  game/battle/sprites.c
  game/battle/status.c
  game/battle/targets.c
  game/battle/tech_records.c
  game/battle/fight_start.c
  game/battle/fight_enemies.c
  game/battle/fight_party.c
  game/battle/fight_timers.c
  game/battle/fight_entries.c
  game/battle/rewards_entries.c
  game/battle/script_language.c
  game/battle/script_flow.c
  game/battle/script_poses.c
  game/battle/script_effects.c
  game/battle/script_colors.c
  game/battle/script_places.c
  game/battle/script_moves.c
  game/battle/script_walk.c
  game/battle/script_sound.c
  game/battle/rewards_levels.c
  game/battle/rewards_messages.c
  game/battle/rewards_techs.c
  game/battle/member_stats.c
  game/battle/draw_objects.c
  game/battle/object_motion.c
  game/battle/tiles.c
  game/battle/battlefield_change.c
  game/battle/battlefield_restore.c
  game/battle/beaten_enemies.c
  game/battle/closing.c
  game/battle/uploads.c
  game/battle/wide_window.c
  game/battle/timers.c
  game/battle/wait.c
  game/battle/window.c)
target_link_libraries(ct_game PUBLIC ct_state_host)
add_executable(ct_native frontend/ct_native.c frontend/route_driver.c frontend/snapshot.c
  frontend/storage.c frontend/asset_files.c frontend/track.c frontend/sram_find.c
  frontend/file_store.c frontend/assets.c frontend/decoded.c frontend/palettes.c frontend/fonts.c
  frontend/lz.c frontend/packed.c frontend/tiles.c frontend/maps.c frontend/text.c
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
  add_executable(native_time_test tests/port/native_time_test.c runtime/port/state_host.c runtime/port/vars_host.c
  runtime/port/arena.c)
  target_include_directories(native_time_test PRIVATE game runtime)
  add_executable(program_test tests/port/program_test.c game/core/program.c game/core/sprite_table.c)
  target_link_libraries(program_test PRIVATE ct_state_host)
  add_executable(vars_host_test tests/port/vars_host_test.c game/menu/saves.c)
  # Preserve bounds diagnostics when an indexed variable moves into a struct.
  add_library(ct_state_host_checked STATIC runtime/port/vars_ram.c runtime/port/vars_host.c
    runtime/port/state_host.c runtime/port/rom_host.c runtime/port/time_rates.c)
  target_compile_definitions(ct_state_host_checked PRIVATE CT_ACCESSOR_CHECKS)
  target_include_directories(ct_state_host_checked PUBLIC game)
  target_link_libraries(ct_state_host_checked PUBLIC ct_platform_native)
  target_link_libraries(vars_host_test PRIVATE ct_state_host_checked)
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
  add_executable(text_test tests/frontend/text_test.c frontend/decoded.c frontend/text.c frontend/assets.c
    game/data/asset_catalogue.c)
  target_include_directories(text_test PRIVATE frontend game)
  add_executable(records_test tests/frontend/records_test.c frontend/decoded.c frontend/records.c frontend/assets.c
    game/data/asset_catalogue.c)
  target_include_directories(records_test PRIVATE frontend game)
  add_executable(player_files_test tests/frontend/player_files_test.c frontend/storage.c)
  target_include_directories(player_files_test PRIVATE frontend)
  add_executable(recorder_test tests/frontend/recorder_test.c frontend/track.c)
  target_link_libraries(recorder_test PRIVATE ct_media)
  add_executable(sound_setting_test tests/port/sound_setting_test.c)
  target_link_libraries(sound_setting_test PRIVATE ct_game)
  add_executable(object_talk_test tests/port/object_talk_test.c)
  target_link_libraries(object_talk_test PRIVATE ct_game)
  add_executable(sound_native_test tests/frontend/sound_native_test.c frontend/sound_native.c)
  target_include_directories(sound_native_test PRIVATE frontend runtime)
  target_link_libraries(sound_native_test PRIVATE ct_game)
  add_executable(enemy_movement_test tests/port/enemy_movement_test.c)
  target_include_directories(enemy_movement_test PRIVATE game runtime)
  target_link_libraries(enemy_movement_test PRIVATE ct_game)
  foreach(test player_files recorder fiber route_driver file_store timing video tasks native_time program vars_host unpack_time assets palettes fonts lz tiles maps text records sound_setting sound_native object_talk enemy_movement)
    if(test STREQUAL "file_store")
      add_test(NAME file_store COMMAND file_store_test ${CMAKE_CURRENT_BINARY_DIR}/file-store-test.bin)
    else()
      add_test(NAME ${test} COMMAND ${test}_test)
    endif()
    set_tests_properties(${test} PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endforeach()
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
