#include "registry.h"

#include "battle/animation.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/frame.h"
#include "battle/main_loop.h"
#include "battle/menu_bridge.h"
#include "battle/message.h"
#include "battle/wait.h"
#include "battle/window.h"
#include "battle/effects.h"
#include "battle/screen.h"
#include "battle/setup.h"
#include "battle/sound.h"
#include "core/console.h"
#include "core/display.h"
#include "core/input.h"
#include "core/palette.h"
#include "core/tile_animation.h"
#include "event/contact.h"
#include "event/event.h"
#include "event/party.h"
#include "field/bridge.h"
#include "field/dialog_window.h"
#include "field/display.h"
#include "field/switches.h"
#include "field/objects_start.h"
#include "field/screen_setup.h"
#include "field/map.h"
#include "field/field.h"
#include "field/frames.h"
#include "field/battle_return.h"
#include "field/battle_start.h"
#include "field/palettes.h"
#include "field/cast.h"
#include "field/main_loop.h"
#include "field/menus.h"
#include "field/start.h"
#include "field/requests.h"
#include "field/screen_effects.h"
#include "field/script_layers.h"
#include "field/sounds.h"
#include "field/graphics.h"
#include "field/layers.h"
#include "field/line_effects.h"
#include "field/object_update.h"
#include "field/party.h"
#include "field/streaming.h"
#include "field/walk.h"
#include "field/sprites.h"
#include "holds.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "menu/fades.h"
#include "menu/digits.h"
#include "menu/epoch.h"
#include "menu/exchange.h"
#include "menu/gate_screen.h"
#include "menu/equipment.h"
#include "menu/item_screen.h"
#include "menu/tech_screen.h"
#include "menu/settings_screen.h"
#include "menu/shop.h"
#include "menu/techs.h"
#include "menu/map.h"
#include "menu/figures.h"
#include "menu/healing.h"
#include "menu/description.h"
#include "menu/held_cursors.h"
#include "menu/saving.h"
#include "menu/main_menu.h"
#include "menu/naming.h"
#include "menu/lists.h"
#include "menu/members.h"
#include "menu/party.h"
#include "menu/line_effects.h"
#include "menu/load_screen.h"
#include "menu/object_commands.h"
#include "menu/turning_world.h"
#include "menu/map_scenes.h"
#include "menu/screen_objects.h"
#include "menu/script_bridge.h"
#include "menu/menu.h"
#include "scene/scene.h"
#include "menu/objects.h"
#include "menu/opening.h"
#include "menu/saves.h"
#include "menu/save_screen.h"
#include "menu/sprites.h"
#include "menu/tiles.h"
#include "menu/sound.h"
#include "menu/text.h"
#include "menu/typeset.h"
#include "menu/graphics.h"
#include "menu/items.h"
#include "menu/windows.h"
#include "menu/top_layer.h"
#include "menu/vram.h"
#include "overworld/entries.h"
#include "overworld/setup.h"
#include "overworld/main_loop.h"
#include "overworld/terrain.h"
#include "warp/warp.h"

/* Entry preconditions on P. */
#define NEEDS_M8 .p_mask = CPU_FLAG_M, .p_value = CPU_FLAG_M
#define NEEDS_M16 .p_mask = CPU_FLAG_M, .p_value = 0
#define NEEDS_M8_X16 .p_mask = CPU_FLAG_M | CPU_FLAG_X, .p_value = CPU_FLAG_M
#define NEEDS_M8_X8 .p_mask = CPU_FLAG_M | CPU_FLAG_X, .p_value = CPU_FLAG_M | CPU_FLAG_X
#define NEEDS_M8_X16_BINARY .p_mask = CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, .p_value = CPU_FLAG_M
#define NEEDS_M16_X16 .p_mask = CPU_FLAG_M | CPU_FLAG_X, .p_value = 0
#define NEEDS_M16_X16_BINARY .p_mask = CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, .p_value = 0
#define NEEDS_M8_X8_BINARY .p_mask = CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, .p_value = CPU_FLAG_M | CPU_FLAG_X
/* A routine that sets the accumulator's width itself. */
#define NEEDS_X16 .p_mask = CPU_FLAG_X, .p_value = 0
#define NEEDS_X16_BINARY .p_mask = CPU_FLAG_X | CPU_FLAG_D, .p_value = 0

/* Entries are keyed by the original entry address. cost_milli is the
 * original's time per native bus-access clock, x1000, DMA's time left out:
 * calibrated from the movie library by `tools/timing_gate.py --calibrate`
 * (docs/REIMPLEMENTATION.md, "The timing gate"). dont_care comes from
 * disasm/out/liveness.json ("dont_care"), and applies wherever the port
 * finishes: after it returns and at every stop it declares with m_stop_at.
 * tools/dontcare_check.py checks it against liveness. */
const NativeEntry game_natives[] = {
    { .addr = 0xC0712E, .name = "palette_upload", .fn = palette_upload, .p_mask = CPU_FLAG_M | CPU_FLAG_X, .p_value = CPU_FLAG_M, .cost_milli = 4514,
      .dont_care = DC_A | DC_X | DC_FLAGS, .guard = palette_bank_is_io },
    { .addr = 0xC00B4E, .name = "display_blank_and_stop_interrupts", .fn = display_blank_and_stop_interrupts,
      .p_mask = CPU_FLAG_M, .p_value = CPU_FLAG_M, .cost_milli = 5750, .dont_care = DC_X | DC_N | DC_Z | DC_C | DC_AL },
    { .addr = 0xC284F9, .name = "input_wait_for_controller", .fn = input_wait_for_controller,
      .p_mask = CPU_FLAG_M | CPU_FLAG_X, .p_value = CPU_FLAG_M, .idle = true, .idle_exit_span = 5, .guard = input_controller_wait_ready },
    { .addr = 0xC284FE, .name = "input_take_ready_controller", .fn = input_take_ready_controller,
      .p_mask = CPU_FLAG_M | CPU_FLAG_X, .p_value = CPU_FLAG_M, .cost_milli = 5567, .guard = input_controller_page_ready,
      .dont_care = DC_A | DC_X | DC_Y | DC_FLAGS },
    { .addr = 0x00FF00, .name = "console_reset", .fn = console_reset, .emulation = true, .cost_milli = 1000 },
    { .addr = 0x00FF00, .name = "console_reset", .fn = console_reset, .cost_milli = 1000 },
    { .addr = 0xC0FF00, .name = "console_reset", .fn = console_reset, .cost_milli = 1000 },
    { .addr = 0xFDC000, .name = "console_boot", .fn = console_boot, .cost_milli = 4060 },
    { .addr = 0x00FF10, .name = "console_vblank_vector", .fn = console_vblank_vector, .cost_milli = 1000 },
    { .addr = 0x00FF14, .name = "console_line_vector", .fn = console_line_vector, .cost_milli = 1000 },
    { .addr = 0x000500, .name = "console_vblank_stub", .fn = console_vblank_stub, .cost_milli = 1000,
      .guard = console_vblank_stub_is_set },
    { .addr = 0x000504, .name = "console_line_stub", .fn = console_line_stub, .cost_milli = 1000,
      .guard = console_line_stub_is_set },
    { .addr = 0xC0000E, .name = "field_first_start", .fn = field_first_start, .p_mask = CPU_FLAG_M | CPU_FLAG_X, .p_value = CPU_FLAG_M, .cost_milli = 66222, .retired = true },
};

const int game_native_count = (int)(sizeof game_natives / sizeof game_natives[0]);

/* The held waits' continuations (holds.h; runtime/port/native.h, native_hold
 * and NativeEntry.hold). Where the machine names their wait, with the main
 * thread resting in it at a frame end, the runtime runs them by the wait's
 * number. The runtime's table holds them beside the registry's rows (the
 * hosts add them, frontend/natives.c), but only for the address path: where
 * no wait is named (a caller still on the gate whose code comes to the loop's
 * head, a state from before the wait was held), the machine finds one by its
 * address, enters it as any entry and it holds the wait from there. */
const NativeEntry game_held_waits[] = {
    { .addr = 0xC2CBE0, .name = "menu_wait_for_menu_music_playing_loop", .fn = menu_wait_for_menu_music_playing_loop, NEEDS_M8,
      .idle = true, .idle_clocks = 60, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .hold = HOLD_MENU_TASKS, .retired = true },
    /* Task starts and resumptions are fresh roots after the scheduler has
     * selected a stack. Keep the complete gated contracts and rates; never
     * nest a new task inside the previous task's C call chain. */
    { .addr = 0xC2826F, .name = "menu_run_the_main_task", .fn = menu_run_the_main_task, .p_mask = CPU_FLAG_D, .p_value = 0,
      .cost_milli = 922, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_screen_step_is_known, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC283BD, .name = "menu_fade_in_task_held", .fn = menu_fade_in_task_entry, .cost_milli = 8768,
      .guard = menu_on_page_low_ram, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC289F2, .name = "menu_list_cursor_task_held", .fn = menu_run_the_list_cursor_task, .p_mask = CPU_FLAG_D,
      .p_value = 0, .cost_milli = 11694, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_list_cursor_is_drawable, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC28A45, .name = "menu_draw_the_cursor_held_on_a_member_held", .fn = menu_run_the_member_cursor_task,
      .p_mask = CPU_FLAG_D | CPU_FLAG_X, .p_value = 0, .cost_milli = 8825, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_member_cursor_is_drawable, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC28AB2, .name = "menu_held_cursor_task_held", .fn = menu_run_the_held_cursor_task, .p_mask = CPU_FLAG_D,
      .p_value = 0, .cost_milli = 14544, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_held_cursor_is_drawable, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC28AF5, .name = "menu_members_cursors_task_held", .fn = menu_run_the_members_cursors_task, .p_mask = CPU_FLAG_D,
      .p_value = 0, .cost_milli = 9092, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_members_cursors_are_drawable, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC28BB8, .name = "menu_scroll_bar_task_held", .fn = menu_run_the_scroll_bar_task, .p_mask = CPU_FLAG_D,
      .p_value = 0, .cost_milli = 2721, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_scroll_bar_is_drawable, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC29016, .name = "menu_raise_amounts_held", .fn = menu_raise_amounts, .p_mask = CPU_FLAG_D, .p_value = 0,
      .cost_milli = 31411, .guard = menu_amounts_can_be_shown, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC2CBBF, .name = "menu_play_the_gate_song_held", .fn = menu_play_the_gate_song_entry, NEEDS_X16_BINARY,
      .cost_milli = 4545, .guard = menu_on_page_work_ram, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC2CECE, .name = "menu_gate_screen_task_held", .fn = menu_run_the_gate_screen_task, .p_mask = CPU_FLAG_D,
      .p_value = 0, .cost_milli = 5650, .dont_care = DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_gate_swirl_turns, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC2E91B, .name = "menu_run_the_cursor_task_held", .fn = menu_run_the_cursor_task, .p_mask = CPU_FLAG_D,
      .p_value = 0, .cost_milli = 54678, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_cursor_is_placeable, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC2F657, .name = "menu_run_record_task_held", .fn = menu_run_record_task, .cost_milli = 8783,
      .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = menu_record_task_is_runnable, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC2F9B2, .name = "menu_type_the_setting_task_held", .fn = menu_type_the_setting_task_entry, .p_mask = CPU_FLAG_D,
      .p_value = 0, .cost_milli = 12733, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_setting_typing_can_start, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC2FA06, .name = "menu_type_the_value_task_held", .fn = menu_type_the_value_task_entry, .p_mask = CPU_FLAG_D,
      .p_value = 0, .cost_milli = 8251, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_value_typing_can_start, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC2FA9F, .name = "menu_type_the_description_task_held", .fn = menu_type_the_description_task_entry,
      .p_mask = CPU_FLAG_D, .p_value = 0, .cost_milli = 1, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_typing_can_go_on, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC2E921, .name = "menu_cursor_task_goes_on_held", .fn = menu_run_the_cursor_task, .p_mask = CPU_FLAG_D,
      .p_value = 0, .cost_milli = 25865, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_cursor_is_placeable, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC28C33, .name = "menu_scroll_bar_task_goes_on_held", .fn = menu_run_the_scroll_bar_task, .p_mask = CPU_FLAG_D,
      .p_value = 0, .cost_milli = 3602, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_scroll_bar_is_drawable, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC2F699, .name = "menu_record_task_goes_on_held", .fn = menu_run_record_task, .cost_milli = 8783,
      .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = menu_record_task_is_runnable, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC289F8, .name = "menu_list_cursor_task_held", .fn = menu_run_the_list_cursor_task, .p_mask = CPU_FLAG_D,
      .p_value = 0, .cost_milli = 11694, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_list_cursor_is_drawable, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC28A94, .name = "menu_draw_the_cursor_held_on_a_member_held", .fn = menu_run_the_member_cursor_task,
      .p_mask = CPU_FLAG_D | CPU_FLAG_X, .p_value = 0, .cost_milli = 8825, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_member_cursor_is_drawable, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC28AB8, .name = "menu_held_cursor_task_held", .fn = menu_run_the_held_cursor_task, .p_mask = CPU_FLAG_D,
      .p_value = 0, .cost_milli = 14544, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_held_cursor_is_drawable, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC28AFB, .name = "menu_members_cursors_task_held", .fn = menu_run_the_members_cursors_task, .p_mask = CPU_FLAG_D,
      .p_value = 0, .cost_milli = 9092, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = menu_members_cursors_are_drawable, .hold = HOLD_MENU_TASKS , .retired = true },
    /* Typing resumes each frame and finishes on its task's original stack. */
    { .addr = 0xC2FAA8, .name = "menu_type_the_description", .fn = menu_type_the_description, .p_mask = CPU_FLAG_D,
      .p_value = 0, .cost_milli = 14536, .dont_care = DC_A | DC_X | DC_Y | DC_FLAGS,
      .guard = menu_typing_can_go_on, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC2F9EB, .name = "menu_setting_text_finished", .fn = menu_setting_text_finished, NEEDS_M8_X16_BINARY,
      .cost_milli = 12733, .dont_care = DC_A | DC_X | DC_Y | DC_FLAGS,
      .guard = menu_on_page_work_ram, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC2FAA2, .name = "menu_description_task_finished", .fn = menu_typing_task_finished, NEEDS_M8_X16_BINARY,
      .cost_milli = 1, .dont_care = DC_A | DC_X | DC_Y | DC_FLAGS,
      .guard = menu_on_page_work_ram, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC2FA46, .name = "menu_value_task_finished", .fn = menu_typing_task_finished, NEEDS_M8_X16_BINARY,
      .cost_milli = 8251, .dont_care = DC_A | DC_X | DC_Y | DC_FLAGS,
      .guard = menu_on_page_work_ram, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC2B1A8, .name = "menu_tonic_member_choice_continues", .fn = menu_tonic_member_choice_continues,
      NEEDS_M8_X16_BINARY, .cost_milli = 3264, .guard = menu_member_window_opens,
      .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC2B22B, .name = "menu_tab_member_choice_continues", .fn = menu_tab_member_choice_continues,
      NEEDS_M8_X16_BINARY, .cost_milli = 5000, .guard = menu_member_window_opens,
      .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC2B0EC, .name = "menu_item_use_finishes", .fn = menu_item_use_finishes,
      .p_mask = CPU_FLAG_D, .p_value = 0, .cost_milli = 10181, .guard = menu_on_page_work_ram,
      .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC2AB41, .name = "menu_picked_item_use_finishes", .fn = menu_picked_item_use_finishes,
      NEEDS_M8_X16_BINARY, .cost_milli = 5852, .guard = menu_on_page_work_ram,
      .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .hold = HOLD_MENU_TASKS , .retired = true },
    /* Fade-in yields keep the phase and the task's original return frame. */
    { .addr = 0xC283C0, .name = "menu_fade_in_begins", .fn = menu_fade_in_begins,
      .p_mask = CPU_FLAG_X | CPU_FLAG_D, .p_value = 0, .cost_milli = 8768,
      .dont_care = 0, .guard = menu_on_page_low_ram, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC283FB, .name = "menu_fade_resumes", .fn = menu_fade_resumes, NEEDS_M8_X16_BINARY,
      .cost_milli = 8768, .dont_care = 0,
      .guard = menu_fade_can_resume, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC283D0, .name = "menu_fade_starts", .fn = menu_fade_starts, NEEDS_M8_X16_BINARY,
      .cost_milli = 8768, .dont_care = 0,
      .guard = menu_fade_can_start, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC283C7, .name = "menu_fade_in_finishes", .fn = menu_fade_in_finishes, NEEDS_M8_X16_BINARY,
      .cost_milli = 8768, .dont_care = 0,
      .guard = menu_on_page_low_ram, .hold = HOLD_MENU_TASKS , .retired = true },
    /* Startup returns cross held waits without requiring the interpreter. */
    { .addr = 0xC10031, .name = "battle_set_up_after_the_graphics_held", .fn = battle_set_up_after_the_graphics,
      NEEDS_M8_X16_BINARY, .cost_milli = 1006, .guard = battle_combatants_graphics_known, .hold = HOLD_BATTLE_GRAPHICS , .retired = true },
    { .addr = 0xC10042, .name = "battle_party_faces_the_enemies_held", .fn = battle_party_faces_the_enemies,
      NEEDS_M8_X16_BINARY, .cost_milli = 18454, .guard = battle_opening_ready,
      .dont_care = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z, .hold = HOLD_BATTLE_WINDOW , .retired = true },
    { .addr = 0xC1004D, .name = "battle_step_returns", .fn = battle_step_returns, NEEDS_M8_X16,
      .cost_milli = 2367, .guard = battle_on_page, .hold = HOLD_BATTLE_WINDOW , .retired = true },
    { .addr = 0xC18030, .name = "battle_main_loop_after_setup", .fn = battle_main_loop_after_setup, NEEDS_M8_X16_BINARY,
      .cost_milli = 114458, .guard = battle_on_page, .hold = HOLD_BATTLE_PASS , .retired = true },
    /* Battle waits and pass boundaries, retaining their gated aliases. */
    { .addr = 0xCCF05B, .name = "battle_wait_graphics_loaded_loop_held", .fn = battle_wait_graphics_loaded_loop, NEEDS_M8,
      .idle = true, .guard = battle_on_page, .hold = HOLD_BATTLE_GRAPHICS , .retired = true },
    { .addr = 0xC1003C, .name = "battle_wait_window_slide_loop_held", .fn = battle_wait_window_slide_loop, NEEDS_M8,
      .idle = true, .guard = battle_on_page, .hold = HOLD_BATTLE_WINDOW , .retired = true },
    { .addr = 0xC18120, .name = "battle_main_loop_held", .fn = battle_main_loop, NEEDS_M8_X16, .cost_milli = 3250,
      .guard = battle_on_page, .hold = HOLD_BATTLE_PASS , .retired = true },
    { .addr = 0xC1812C, .name = "battle_main_loop_body_held", .fn = battle_main_loop_body, NEEDS_M8_X16_BINARY,
      .cost_milli = 25934, .guard = battle_on_page, .hold = HOLD_BATTLE_PASS , .retired = true },
    { .addr = 0xC18231, .name = "battle_tick_timers_held", .fn = battle_tick_timers, .p_mask = CPU_FLAG_X,
      .p_value = 0, .cost_milli = 33831, .guard = battle_on_page, .hold = HOLD_BATTLE_PASS , .retired = true },
    { .addr = 0xC18290, .name = "battle_dispatch_expired_timers_held", .fn = battle_dispatch_expired_timers,
      NEEDS_M8_X16_BINARY, .cost_milli = 15339, .guard = battle_on_page, .hold = HOLD_BATTLE_PASS , .retired = true },
    { .addr = 0xC18345, .name = "battle_main_loop_tail_held", .fn = battle_main_loop_tail, NEEDS_M8_X16,
      .cost_milli = 16728, .guard = battle_pass_ends_plainly, .hold = HOLD_BATTLE_PASS , .retired = true },
    /* Menu comparisons remain registered while the unreached audit closes.
     * Shipping waits name these continuations, preserving their original
     * contracts and the scheduler's calibrated rate at a saveable boundary. */
    { .addr = 0xC20348, .name = "menu_frame_held", .fn = menu_wait_for_frame_loop_entry,
      .loop = menu_wait_for_frame_loop, NEEDS_M8, .idle = true,
      .guard = menu_frame_wait_on_page, .dont_care = DC_X | DC_Y | DC_N | DC_Z | DC_C,
      .hold = HOLD_MENU_FRAME , .retired = true },
    { .addr = 0xC22468, .name = "overworld_exit_fade_finished", .fn = overworld_exit_fade_finished, NEEDS_M8_X16_BINARY,
      .cost_milli = 5557, .guard = overworld_exit_fade_can_resume, .pulls = 2, .hold = HOLD_MENU_FRAME,
      .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .retired = true },
    { .addr = 0xC28216, .name = "menu_task_ends_held", .fn = menu_task_ends, .p_mask = CPU_FLAG_D, .p_value = 0,
      .cost_milli = 4035, .guard = menu_task_can_end, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC20458, .name = "menu_counted_frame_finished", .fn = menu_counted_frame_finished, NEEDS_M8_X16,
      .cost_milli = 3092, .guard = menu_on_page_low_ram, .pulls = 2, .hold = HOLD_MENU_FRAME , .retired = true },
    { .addr = 0xC263BD, .name = "menu_epoch_map_frame_finished", .fn = menu_epoch_map_frame_finished, NEEDS_M8_X16,
      .cost_milli = 4560, .guard = menu_map_page_is_menu, .hold = HOLD_MENU_FRAME, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C , .retired = true },
    { .addr = 0xC263CA, .name = "menu_epoch_map_finished", .fn = menu_epoch_map_finished, NEEDS_M8_X16,
      .cost_milli = 4560, .guard = menu_map_page_is_menu, .hold = HOLD_MENU_FRAME, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C , .retired = true },
    { .addr = 0xC26A9C, .name = "menu_epoch_dial_opened", .fn = menu_epoch_dial_opened, NEEDS_M8_X16,
      .cost_milli = 4388, .guard = menu_map_page_is_menu, .hold = HOLD_MENU_FRAME, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C , .retired = true },
    { .addr = 0xC26A9F, .name = "menu_epoch_dial_frame_finished", .fn = menu_epoch_dial_frame_finished, NEEDS_M8_X16,
      .cost_milli = 4388, .guard = menu_map_page_is_menu, .hold = HOLD_MENU_FRAME, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C , .retired = true },
    { .addr = 0xC26AB4, .name = "menu_epoch_dial_finished", .fn = menu_epoch_dial_finished, NEEDS_M8_X16,
      .cost_milli = 4388, .guard = menu_map_page_is_menu, .hold = HOLD_MENU_FRAME, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C , .retired = true },
    { .addr = 0xC22535, .name = "overworld_menu_fade_finished", .fn = overworld_menu_fade_finished, NEEDS_M8_X16_BINARY,
      .cost_milli = 5557, .guard = overworld_main_loop_runs, .hold = HOLD_MENU_FRAME, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .retired = true },
    { .addr = 0xC22585, .name = "overworld_menu_return_fade_finished", .fn = overworld_menu_return_fade_finished, NEEDS_M8_X16_BINARY,
      .cost_milli = 1, .guard = overworld_main_loop_runs, .hold = HOLD_MENU_FRAME, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .retired = true },
    { .addr = 0xC225A4, .name = "overworld_map_view_fade_finished", .fn = overworld_map_view_fade_finished, NEEDS_M8_X16_BINARY,
      .cost_milli = 5557, .guard = overworld_main_loop_runs, .hold = HOLD_MENU_FRAME, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .retired = true },
    { .addr = 0xC225D2, .name = "overworld_back_from_the_map_view", .fn = overworld_back_from_the_map_view, NEEDS_M8_X16_BINARY,
      .cost_milli = 5557, .guard = overworld_main_loop_runs, .hold = HOLD_MENU_FRAME, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .retired = true },
    { .addr = 0xC22615, .name = "overworld_screen_return_fade_finished", .fn = overworld_screen_return_fade_finished, NEEDS_M8_X16_BINARY,
      .cost_milli = 5557, .guard = overworld_main_loop_runs, .hold = HOLD_MENU_FRAME, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .retired = true },
    { .addr = 0xC22634, .name = "overworld_dial_fade_finished", .fn = overworld_dial_fade_finished, NEEDS_M8_X16_BINARY,
      .cost_milli = 5557, .guard = overworld_main_loop_runs, .hold = HOLD_MENU_FRAME, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .retired = true },
    { .addr = 0xC22658, .name = "overworld_back_from_the_time_dial", .fn = overworld_back_from_the_time_dial, NEEDS_M8_X16_BINARY,
      .cost_milli = 5557, .guard = overworld_main_loop_runs, .hold = HOLD_MENU_FRAME, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .retired = true },
    { .addr = 0xC226A0, .name = "overworld_screen_return_fade_finished", .fn = overworld_screen_return_fade_finished, NEEDS_M8_X16_BINARY,
      .cost_milli = 5557, .guard = overworld_main_loop_runs, .hold = HOLD_MENU_FRAME, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .retired = true },
    { .addr = 0xC2826C, .name = "menu_main_task_waits_held", .fn = menu_main_task_waits, .p_mask = CPU_FLAG_D, .p_value = 0,
      .cost_milli = 1, .guard = menu_on_page, .hold = HOLD_MENU_TASKS, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C , .retired = true },
    { .addr = 0xC281CA, .name = "menu_uploads_held", .fn = menu_wait_ready_loop_entry,
      .loop = menu_wait_ready_loop, NEEDS_M8, .idle = true,
      .guard = menu_ready_wait_reaches_low_ram, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC281CF, .name = "menu_tasks_held", .fn = menu_run_the_tasks, NEEDS_M8_X8, .cost_milli = 4107,
      .guard = menu_tasks_can_run, .hold = HOLD_MENU_TASKS , .retired = true },
    { .addr = 0xC00281, .name = "field_no_battle_returns", .fn = field_no_battle_returns, NEEDS_M8_X16,
      .cost_milli = 4219, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z,
      .guard = field_battle_can_begin, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC01893, .name = "field_requested_battle_decided", .fn = field_requested_battle_decided, NEEDS_M8_X16,
      .cost_milli = 2941, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC0262B, .name = "field_scene_close_field_is_aside", .fn = field_scene_close_field_is_aside, NEEDS_M8_X16,
      .cost_milli = 2858, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C,
      .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },

    { .addr = 0xC00000, .name = "field_starts_after_location_change", .fn = field_enter, NEEDS_M8_X16,
      .cost_milli = 1, .guard = field_location_is_a_number, .hold = HOLD_FIELD_LOCATION, .retired = true },
    /* the scene program's main loop, its gate retired (the shipping mode
     * only): a pass, with the call a setup named and the program's end once
     * a script ends the scene, and the idle pass with nothing to call (the
     * scene's page) */
    { .addr = 0x7E3023, .name = "scene_program_next_pass", .fn = scene_program_next_pass, NEEDS_M16, .cost_milli = 3125,
      .hold = HOLD_SCENE_MAIN_LOOP, .retired = true, .guard = scene_program_passes },
    { .addr = 0x7E3023, .name = "scene_program_waits", .fn = scene_program_waits, NEEDS_M16, .idle = true,
      .hold = HOLD_SCENE_MAIN_LOOP, .retired = true, .guard = scene_program_waits_for_the_frame },
    { .addr = 0x7E3026, .name = "scene_program_finishes_pass", .fn = scene_program_finishes_pass, NEEDS_M16,
      .cost_milli = 3125, .hold = HOLD_SCENE_MAIN_LOOP, .retired = true, .guard = scene_program_pass_finished },
    /* The race's upload wait keeps its caller frame, without a suspended
     * port. Its exit gathers the picture and calls that caller directly. */
    { .addr = 0x7E5211, .name = "scene_race_waits_for_upload", .fn = scene_race_wait_for_tilemap_upload_loop, NEEDS_M16,
      .idle = true, .hold = HOLD_SCENE_RACE_UPLOAD, .retired = true, .guard = scene_race_wait_for_tilemap_upload_present },
    { .addr = 0x7E5215, .name = "scene_race_upload_finished", .fn = scene_race_upload_finished, NEEDS_M16_X16,
      .cost_milli = 5204, .dont_care = DC_X | DC_Y | DC_N | DC_Z,
      .hold = HOLD_SCENE_RACE_UPLOAD, .retired = true, .guard = scene_race_gather_scaled_present },
    /* the time warp's main loop, its gate retired (the shipping mode only): its
     * next frame, once the vblank handler has taken the last, and its idle
     * pass before (the warp's page, D = $0500) */
    { .addr = 0xFDEAED, .name = "warp_next_frame", .fn = warp_next_frame, NEEDS_M8_X16, .cost_milli = 2653,
      .hold = HOLD_WARP_FRAME, .retired = true, .guard = warp_frame_taken },
    { .addr = 0xFDEAED, .name = "warp_waits_for_the_frame", .fn = warp_waits_for_the_frame, NEEDS_M8, .idle = true,
      .hold = HOLD_WARP_FRAME, .retired = true, .guard = warp_frame_not_taken },
    /* the overworld's main loop, its gate retired (the shipping mode only): its
     * pass once the vblank handler has counted the next frame, and its idle
     * pass before (the mode's page, D = 0); and its halt, a pass at a time.
     * The pass keeps the estimate the loop's entry ($C2:23CF) had. */
    { .addr = 0xC20348, .name = "overworld_next_frame", .fn = overworld_next_frame, NEEDS_M8_X16_BINARY,
      .cost_milli = 5557, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .pulls = 2,
      .hold = HOLD_OVERWORLD_FRAME, .retired = true, .guard = overworld_frame_counted },
    { .addr = 0xC20348, .name = "overworld_waits_for_the_frame", .fn = overworld_waits_for_the_frame, NEEDS_M8,
      .idle = true, .hold = HOLD_OVERWORLD_FRAME, .retired = true, .guard = overworld_frame_not_counted },
    { .addr = 0xC2250D, .name = "overworld_halt", .fn = overworld_halt_entry, NEEDS_M8_X16_BINARY,
      .cost_milli = 3000, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
      .hold = HOLD_OVERWORLD_HALT, .retired = true, .guard = overworld_main_loop_runs },
    { .addr = 0xC22556, .name = "overworld_back_from_the_menus", .fn = overworld_back_from_the_menus, NEEDS_M8_X16_BINARY,
      .cost_milli = 1, .hold = HOLD_MENU_RETURN, .retired = true, .guard = overworld_main_loop_runs },
    /* the field's wait for the next frame: its pass, for every routine that
     * waits there, which returns to the caller's rest once the vblank handler
     * has taken the frame (a data bank reaching low RAM); and the main loop's
     * rest, a continuation of the same wait where the wait returns into the
     * loop (the field's page, the data bank 0): its next pass from the head or
     * after the requests, and the wait again. Each declares what the pass
     * doesn't read from there, as the loop's stop at its head and the walk's
     * call do. */
    { .addr = 0xC0EC71, .name = "field_waits_for_the_frame", .fn = field_wait_for_frame_loop_entry,
      .loop = field_wait_for_frame_loop, NEEDS_M8, .idle = true,
      .idle_return_span = 6,
      .hold = HOLD_FIELD_FRAME, .guard = field_bank_low_ram, .retired = true },
    { .addr = 0xC00097, .name = "field_first_frame", .fn = field_frame, NEEDS_M8_X16, .cost_milli = 1402,
      .hold = HOLD_FIELD_FRAME, .guard = field_on_page_bank_zero, .retired = true },
    { .addr = 0xC000BD, .name = "field_next_frame", .fn = field_next_frame, NEEDS_M8_X16, .cost_milli = 1000,
      .dont_care = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C, .hold = HOLD_FIELD_FRAME, .guard = field_on_page_bank_zero, .retired = true },
    { .addr = 0xC000A7, .name = "field_next_frame_from_the_walk", .fn = field_frame_goes_on_with_the_walk, NEEDS_M8_X16,
      .cost_milli = 2249, .hold = HOLD_FIELD_FRAME,
      .guard = field_on_page_bank_zero, .retired = true },
    /* The setup also calls this wait directly through the timing service;
     * its retained idle row names the wait on that first address entry. */
    { .addr = 0xC00B38, .name = "field_waits_to_show_the_picture", .fn = field_screen_on_wait_for_vblank_loop,
      NEEDS_M8, .idle = true, .guard = field_display_bank_is_io, .hold = HOLD_FIELD_SCREEN, .retired = true },
    { .addr = 0xC00B3D, .name = "field_picture_starts", .fn = field_turn_the_handlers_on, NEEDS_M8_X16,
      .cost_milli = 5059, .dont_care = DC_A | DC_N | DC_Z, .guard = field_display_bank_is_io,
      .hold = HOLD_FIELD_SCREEN, .retired = true },
    /* Field caller continuations. Each keeps the estimate of the entry
     * whose remaining work it performs; callees retain their own time tags. */
    { .addr = 0xC000A4, .name = "field_held_requests", .fn = field_frame_goes_on_with_the_requests,
      NEEDS_M8_X16, .cost_milli = 2376, .guard = field_on_page_bank_zero , .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC000AA, .name = "field_held_confirm", .fn = field_frame_goes_on_with_confirm,
      NEEDS_M8_X16, .cost_milli = 2447, .guard = field_on_page_bank_zero , .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC000AD, .name = "field_held_dialog",
      .fn = field_frame_goes_on_with_the_dialog_window, NEEDS_M8_X16, .cost_milli = 2442, .guard = field_on_page_bank_zero , .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC000B1, .name = "field_held_effect",
      .fn = field_frame_goes_on_with_the_screen_effect, NEEDS_M8_X16, .cost_milli = 2538, .guard = field_on_page_bank_zero , .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC000B4, .name = "field_held_script_layers",
      .fn = field_frame_goes_on_with_the_script_layers, NEEDS_M8_X16, .cost_milli = 2539, .guard = field_on_page_bank_zero , .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC000B7, .name = "field_held_work", .fn = field_frame_goes_on_with_the_frame_work,
      NEEDS_M8_X16, .cost_milli = 2589, .guard = field_on_page_bank_zero , .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC000BA, .name = "field_held_frame_end", .fn = field_wait_for_the_next_frame, NEEDS_M8_X16,
      .cost_milli = 3669, .dont_care = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero , .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC000EB, .name = "field_held_walk", .fn = field_walk_a_frame, NEEDS_M8_X16, .cost_milli = 1,
      .guard = field_on_page_bank_zero , .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC03B71, .name = "field_waits_to_change_location",
      .fn = field_change_location_wait_for_vblank_loop, NEEDS_M8, .idle = true, .guard = field_display_bank_is_io , .hold = HOLD_FIELD_LOCATION, .retired = true },
    { .addr = 0xC03B76, .name = "event_location_changes_after_vblank", .fn = event_go_now_in_the_dark, NEEDS_M8_X16,
      .cost_milli = 3062, .guard = field_on_page , .hold = HOLD_FIELD_LOCATION, .retired = true },
    { .addr = 0xC04181, .name = "field_waits_to_restart", .fn = field_restart_wait_for_vblank_loop,
      NEEDS_M8, .idle = true, .guard = field_display_bank_is_io , .hold = HOLD_FIELD_RESTART, .retired = true },
    { .addr = 0xC04186, .name = "field_restarts_after_vblank", .fn = field_restart_in_the_dark,
      NEEDS_M8_X16, .cost_milli = 1000 , .hold = HOLD_FIELD_RESTART, .retired = true },
    { .addr = 0xC028A4, .name = "field_picture_waits_for_its_first_frame", .fn = field_picture_waits_for_its_first_frame, NEEDS_M8_X16_BINARY,
      .cost_milli = 7125, .dont_care = DC_X | DC_Y | DC_N | DC_C, .guard = field_on_page_low_bank, .hold = HOLD_FIELD_SCREEN, .retired = true },
    { .addr = 0xC028A7, .name = "field_picture_is_ready", .fn = field_picture_is_ready, NEEDS_M8_X16_BINARY,
      .cost_milli = 7125, .dont_care = DC_X | DC_Y | DC_N | DC_C, .guard = field_on_page_low_bank, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC0284B, .name = "field_picture_begins_brightening", .fn = field_picture_begins_brightening, NEEDS_M8_X16_BINARY,
      .cost_milli = 6474, .dont_care = DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_low_bank, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC02827, .name = "field_menu_picture_begins_brightening", .fn = field_menu_picture_begins_brightening, NEEDS_M8_X16_BINARY,
      .cost_milli = 1, .dont_care = DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_low_bank, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC02860, .name = "field_picture_brightened", .fn = field_picture_brightened, NEEDS_M8_X16_BINARY,
      .cost_milli = 6474, .dont_care = DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_low_bank, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC0283C, .name = "field_menu_picture_brightened", .fn = field_menu_picture_brightened, NEEDS_M8_X16_BINARY,
      .cost_milli = 1, .dont_care = DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_low_bank, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC01964, .name = "field_back_from_the_main_menu", .fn = field_back_from_the_main_menu, NEEDS_M8_X16,
      .cost_milli = 2981, .dont_care = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_MENU_RETURN, .retired = true },
    { .addr = 0xC019D2, .name = "field_back_from_the_start_menu", .fn = field_back_from_the_start_menu, NEEDS_M8_X16,
      .cost_milli = 2981, .dont_care = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_MENU_RETURN, .retired = true },
    { .addr = 0xC00D1C, .name = "field_back_from_a_script_menu", .fn = field_back_from_a_script_menu, NEEDS_M8_X16,
      .cost_milli = 2981, .dont_care = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_MENU_RETURN, .retired = true },
    { .addr = 0xC01981, .name = "field_back_from_a_menu_fade", .fn = field_back_from_a_menu_fade, NEEDS_M8_X16,
      .cost_milli = 2981, .dont_care = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC019FF, .name = "field_start_menu_back_from_a_fade", .fn = field_back_from_a_menu_fade, NEEDS_M8_X16,
      .cost_milli = 2981, .dont_care = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC01920, .name = "field_after_the_start_menu", .fn = field_after_the_start_menu, NEEDS_M8_X16,
      .cost_milli = 2981, .dont_care = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC018F7, .name = "field_pause_continues", .fn = field_pause_continues, NEEDS_M8_X16,
      .cost_milli = 2981, .dont_care = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC01956, .name = "field_menu_fade_continues", .fn = field_menu_fade_continues, NEEDS_M8_X16,
      .cost_milli = 2981, .dont_care = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC019BF, .name = "field_start_menu_fade_continues", .fn = field_start_menu_fade_continues, NEEDS_M8_X16,
      .cost_milli = 2981, .dont_care = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC00CE8, .name = "field_script_menu_fade_continues", .fn = field_script_menu_fade_continues, NEEDS_M8_X16,
      .cost_milli = 2981, .dont_care = DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC0178E, .name = "field_tile_copy_recentres", .fn = field_tile_copy_recentres, NEEDS_M8_X16,
      .cost_milli = 2941, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC0179A, .name = "field_tile_copy_sends_layers", .fn = field_tile_copy_sends_layers, NEEDS_M8_X16,
      .cost_milli = 2941, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC017BC, .name = "field_tile_copy_finishes_bg1", .fn = field_tile_copy_finishes_bg1, NEEDS_M8_X16,
      .cost_milli = 2941, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC017E5, .name = "field_tile_copy_finishes_bg2", .fn = field_tile_copy_finishes_bg2, NEEDS_M8_X16,
      .cost_milli = 2941, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC01882, .name = "field_tile_copy_finishes_bg3", .fn = field_tile_copy_finishes_bg3, NEEDS_M8_X16,
      .cost_milli = 2941, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC0188B, .name = "field_tile_copy_returns", .fn = field_tile_copy_returns, NEEDS_M8_X16,
      .cost_milli = 2941, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC0180E, .name = "field_tile_copy_sends_second_layer", .fn = field_tile_copy_sends_second_layer, NEEDS_M8_X16,
      .cost_milli = 2941, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC01835, .name = "field_tile_copy_restores_first_layer", .fn = field_tile_copy_restores_first_layer, NEEDS_M8_X16,
      .cost_milli = 2941, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC01859, .name = "field_tile_copy_finishes_both_layers", .fn = field_tile_copy_finishes_both_layers, NEEDS_M8_X16,
      .cost_milli = 2941, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC018A7, .name = "field_requested_battle_starts", .fn = field_requested_battle_starts, NEEDS_M8_X16,
      .cost_milli = 2941, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC018AB, .name = "field_requested_battle_ended", .fn = field_requested_battle_ended, NEEDS_M8_X16,
      .cost_milli = 2941, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC018C4, .name = "field_requested_battle_finishes", .fn = field_requested_battle_finishes, NEEDS_M8_X16,
      .cost_milli = 2941, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC018CD, .name = "field_requested_battle_returns", .fn = field_requested_battle_returns, NEEDS_M8_X16,
      .cost_milli = 2941, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC002AD, .name = "field_game_over_waits_for_a_button", .fn = field_game_over_waits_for_a_button, NEEDS_M8_X16,
      .cost_milli = 18526, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC002C1, .name = "field_game_over_fades", .fn = field_game_over_fades, NEEDS_M8_X16,
      .cost_milli = 18526, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC002E8, .name = "field_return_to_kept_place_fades", .fn = field_return_to_kept_place_fades, NEEDS_M8_X16,
      .cost_milli = 18526, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC0031C, .name = "field_restores_its_picture", .fn = field_restores_its_picture, NEEDS_M8_X16,
      .cost_milli = 18526, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC0032A, .name = "field_restores_its_script", .fn = field_restores_its_script, NEEDS_M8_X16,
      .cost_milli = 18526, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC00343, .name = "field_finishes_restoring", .fn = field_finishes_restoring, NEEDS_M8_X16,
      .cost_milli = 18526, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC02628, .name = "field_scene_close_puts_the_field_aside", .fn = field_scene_close_puts_the_field_aside, NEEDS_M8_X16,
      .cost_milli = 2858, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC02631, .name = "field_scene_close_starts_the_scene", .fn = field_scene_close_starts_the_scene, NEEDS_M8_X16,
      .cost_milli = 2858, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC0268B, .name = "field_ripple_settles", .fn = field_ripple_settles, NEEDS_M8_X16,
      .cost_milli = 2858, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC02691, .name = "field_ripple_settled_a_frame", .fn = field_ripple_settled_a_frame, NEEDS_M8_X16,
      .cost_milli = 2858, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC026C7, .name = "field_ripple_grew_a_frame", .fn = field_ripple_grew_a_frame, NEEDS_M8_X16,
      .cost_milli = 2858, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC026DA, .name = "field_ripple_held_a_frame", .fn = field_ripple_held_a_frame, NEEDS_M8_X16,
      .cost_milli = 2858, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC026F1, .name = "field_ripple_shrank_a_frame", .fn = field_ripple_shrank_a_frame, NEEDS_M8_X16,
      .cost_milli = 2858, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC021ED, .name = "field_screen_effect_returns", .fn = field_screen_effect_returns, NEEDS_M8_X16,
      .cost_milli = 2858, .dont_care = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
    { .addr = 0xC03FCD, .name = "event_scene_fade_continues", .fn = event_scene_fade_continues, NEEDS_M8_X16_BINARY,
      .cost_milli = 5449, .dont_care = DC_A | DC_N | DC_Z, .guard = field_on_page_bank_zero, .hold = HOLD_FIELD_FRAME, .retired = true },
};
const int game_held_wait_count = (int)(sizeof game_held_waits / sizeof game_held_waits[0]);

/* The estimates of routines whose entries left the registry but whose work
 * ports still run as C under their names (TIMED_AS, NATIVE_TIME_AS;
 * native_keep_estimates): each at the cost_milli its entry was calibrated
 * at, which the machine never enters. */
const NativeEntry game_kept_estimates[] = {
    /* the time warp, its gate retired (docs/REIMPLEMENTATION.md, "The time
     * warp's teardown"): its set-up, the end's screen gone dark and story
     * flags put back, the screen turned on, its vblank handler and each kind's
     * frame */
    { .addr = 0xFDDB97, .name = "warp_set_up", .cost_milli = 24782 },
    { .addr = 0xFDEAC8, .name = "warp_go_dark", .cost_milli = 4300 },
    { .addr = 0xFDEAB7, .name = "warp_put_back_the_story_flags", .cost_milli = 6994 },
    { .addr = 0xFDEADF, .name = "warp_turn_the_screen_on", .cost_milli = 4429 },
    { .addr = 0xFDDE15, .name = "warp_vblank", .cost_milli = 6710 },
    { .addr = 0xFDDA06, .name = "warp_long_frame", .cost_milli = 8320 },
    { .addr = 0xFDDA65, .name = "warp_short_frame", .cost_milli = 8332 },
    { .addr = 0xFDDAC4, .name = "warp_shortest_frame", .cost_milli = 8303 },
    { .addr = 0xFDDB27, .name = "warp_flickering_frame", .cost_milli = 8325 },
    /* the overworld, its gate retired (docs/REIMPLEMENTATION.md, "The
     * overworld's teardown"): the set up and its loads, the objects' handlers
     * and the routines a scene calls, which the menus' object loop and script
     * bridge run as the machine forms, the ground's look-up, the main loop's
     * pass (as C, overworld_runs), the overworld put aside and brought back,
     * the return from the menus and the sound request passed on */
    { .addr = 0xC223A8, .name = "overworld_set_up", .cost_milli = 1 },
    { .addr = 0xC22C93, .name = "overworld_load_the_map_again", .cost_milli = 11247 },
    { .addr = 0xC227DE, .name = "overworld_load_the_map_tiles", .cost_milli = 317556 },
    { .addr = 0xC22D47, .name = "overworld_finish_the_graphics", .cost_milli = 1 },
    { .addr = 0xC23404, .name = "overworld_lead", .cost_milli = 5446 },
    { .addr = 0xC23AE2, .name = "overworld_follow", .cost_milli = 6449 },
    { .addr = 0xC23264, .name = "overworld_watch_the_ground", .cost_milli = 3902 },
    { .addr = 0xC22F94, .name = "overworld_region_song", .cost_milli = 5630 },
    { .addr = 0xC25628, .name = "overworld_sign", .cost_milli = 5428 },
    { .addr = 0xC2309E, .name = "overworld_vehicles", .cost_milli = 5052 },
    { .addr = 0xC242DD, .name = "overworld_epoch", .cost_milli = 11559 },
    { .addr = 0xC2495E, .name = "overworld_epoch_second_sprite", .cost_milli = 5416 },
    { .addr = 0xC24CC5, .name = "overworld_dactyl", .cost_milli = 5731 },
    { .addr = 0xC2504B, .name = "overworld_dactyl_second_sprite", .cost_milli = 5445 },
    { .addr = 0xC25097, .name = "overworld_following_dactyl", .cost_milli = 5433 },
    { .addr = 0xC25494, .name = "overworld_following_dactyl_second_sprite", .cost_milli = 8178 },
    { .addr = 0xC249CF, .name = "overworld_black_omen_watch", .cost_milli = 3617 },
    { .addr = 0xC248A9, .name = "overworld_show_the_epoch_flying", .cost_milli = 4263 },
    { .addr = 0xC248CD, .name = "overworld_carry_the_epoch_round", .cost_milli = 1889 },
    { .addr = 0xC22F0F, .name = "overworld_start_the_location_song", .cost_milli = 5022 },
    { .addr = 0xC25798, .name = "overworld_clear_the_sign_label", .cost_milli = 147240 },
    { .addr = 0xC262AE, .name = "overworld_ground_at", .cost_milli = 6384 },
    { .addr = 0xC223CF, .name = "overworld_main_loop", .cost_milli = 5557 },
    { .addr = 0xC22E23, .name = "overworld_put_aside", .cost_milli = 2876 },
    { .addr = 0xC22E72, .name = "overworld_bring_back", .cost_milli = 2876 },
    { .addr = 0xC22556, .name = "overworld_come_back_to_the_map_s_screen", .cost_milli = 1 },
    { .addr = 0xC2255A, .name = "overworld_back_from_the_menus", .cost_milli = 1000 },
    { .addr = 0xC22ED9, .name = "overworld_pass_on_sound", .cost_milli = 3517 },
    /* the scene program, its gate retired (docs/REIMPLEMENTATION.md, "The
     * scene program's teardown"): its start, its work RAM's clear, the sound
     * put back to normal and its end */
    { .addr = 0x7E3000, .name = "scene_program_start", .cost_milli = 10929 },
    { .addr = 0x7E3092, .name = "scene_clear_work_ram", .cost_milli = 6782 },
    { .addr = 0x7E3177, .name = "scene_sound_back_to_normal", .cost_milli = 4325 },
    { .addr = 0x7E302A, .name = "scene_program_restart", .cost_milli = 1 },
    { .addr = 0x7E30F9, .name = "scene_init_video", .cost_milli = 3889 },
};
const int game_kept_estimate_count = (int)(sizeof game_kept_estimates / sizeof game_kept_estimates[0]);

/* The data subsystem's gate retired (docs/REIMPLEMENTATION.md, "The data
 * subsystem's teardown"): the unpacker has no entry here, every port calls it
 * as C, and the cells the original's unpacker keeps its place in as it works
 * -- where a segment ends, the bits and control byte left, the last run's
 * length and the last reference's place -- nothing but that routine reads.
 * The C keeps them in locals, so the comparisons leave them out. */
const NativeRange game_uncompared_wram[] = {
    { 0x7E0309, 0x7E0316 },   /* decompress_segment_end .. decompress_scratch */
};
const int game_uncompared_wram_count = (int)(sizeof game_uncompared_wram / sizeof game_uncompared_wram[0]);
