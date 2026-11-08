/* Contracts of the field machine forms retained for callers and held
 * continuations. Rejection reports a broken contract before the original
 * entry is left to the machine. */
#include "field/machine_forms.h"
#include "core/program.h"
#include "port/machine.h"
#include "field/battle_return.h"
#include "field/battle_start.h"
#include "field/bridge.h"
#include "field/cast.h"
#include "field/dialog_window.h"
#include "field/display.h"
#include "field/field.h"
#include "field/frames.h"
#include "field/graphics.h"
#include "field/layers.h"
#include "field/line_effects.h"
#include "field/main_loop.h"
#include "field/map.h"
#include "field/menus.h"
#include "field/object_update.h"
#include "field/objects_start.h"
#include "field/palettes.h"
#include "field/party.h"
#include "field/requests.h"
#include "field/screen_effects.h"
#include "field/screen_setup.h"
#include "field/script_layers.h"
#include "field/sounds.h"
#include "field/sprites.h"
#include "field/start.h"
#include "field/streaming.h"
#include "field/switches.h"
#include "field/walk.h"

static const struct {
    void (*entry)(Snes *);
    uint32_t address;
    uint8_t mask, value;
    bool (*takes)(Snes *);
} forms[] = {
    { field_setup_line_effects, 0xFDC1EE, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_display_bank_is_io },
    { field_init_line_effect_values, 0xFDC124, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_line_effects_page_is_field },
    { field_build_line_effect_tables, 0xFDC2C1, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M | CPU_FLAG_X, field_line_effects_can_build },
    { field_fade_in_entry, 0xC02848, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_on_page_low_bank },
    { field_fade_in_after_a_menu_entry, 0xC02824, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_on_page_low_bank },
    { field_set_the_screen_up, 0xC0286C, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_on_page_low_bank },
    { field_draw_the_objects_in_view, 0xC0B0E6, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_objects_in_view_can_be_drawn },
    { field_build_the_sprites_afresh, 0xC0B192, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_sprites_can_be_built_afresh },
    { field_forget_the_steps_entry, 0xC028AA, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { field_press_the_remembered_switches_entry, 0xC028E1, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_remembered_switches_can_be_pressed },
    { field_mark_the_chests, 0xC0A712, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_on_layers_page },
    { field_bring_in_the_right_columns, 0xC087F1, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_right_columns_can_come_in },
    { field_set_up_the_objects, 0xC056A6, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_objects_can_be_set_up },
    { field_start_the_objects, 0xC05709, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_objects_can_start },
    { field_screen_on_wait_for_vblank_loop, 0xC00B38, CPU_FLAG_M, CPU_FLAG_M, field_display_bank_is_io },
    { field_turn_the_handlers_on, 0xC00B3D, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_display_bank_is_io },
    { field_build_the_window_frame_entry, 0xC029F7, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { field_build_the_text_area_entry, 0xC02B78, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { field_dialog_window_step_entry, 0xC01F87, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_dialog_window_can_step },
    { field_irq, 0xC0ECCC, 0, 0, NULL },
    { field_ready_the_picture, 0xC00B28, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_on_page_bank_zero },
    { field_nmi, 0xC0EA63, 0, 0, NULL },
    { field_wait_for_frame_loop, 0xC0EC71, CPU_FLAG_M, CPU_FLAG_M, field_bank_low_ram },
    { field_change_location_wait_for_vblank_loop, 0xC03B71, CPU_FLAG_M, CPU_FLAG_M, field_display_bank_is_io },
    { field_restart_wait_for_vblank_loop, 0xC04181, CPU_FLAG_M, CPU_FLAG_M, field_display_bank_is_io },
    { field_restart_in_the_dark, 0xC04186, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { field_scroll_layers, 0xC02C41, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, NULL },
    { field_update_object_visibility, 0xC0A88D, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M | CPU_FLAG_X, field_view_can_run },
    { field_walk_the_leader, 0xC0881E, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_walk_can_run },
    { field_recentre_the_screen, 0xC0885A, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_recentring_can_run },
    { field_update_objects, 0xC0A810, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M | CPU_FLAG_X, field_objects_counted },
    { field_leave_object_row, 0xC0A98A, 0, 0, field_object_rows_linked },
    { field_join_object_row, 0xC0A9CD, 0, 0, field_object_rows_linked },
    { field_tick_object_animation, 0xC0C98A, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M | CPU_FLAG_X, field_object_delays_in_bank },
    { field_build_sprite_table, 0xC0B271, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_low_bank },
    { field_draw_object_sprites, 0xC0B309, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_sprites_draw_in_table },
    { field_free_every_sprite_area, 0xC0E935, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { field_update_layer_scroll, 0xC091AC, CPU_FLAG_M | CPU_FLAG_D, CPU_FLAG_M, field_on_layers_page_low_bank },
    { field_stream_the_layers, 0xC093E1, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_streaming_can_run },
    { field_draw_bg1_at_the_load, 0xC075E9, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_streaming_can_run },
    { field_draw_bg2_at_the_load, 0xC078EC, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_bg2_drawing_can_run },
    { field_draw_bg3_at_the_load, 0xC07CB5, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_streaming_can_run },
    { field_copy_tiles, 0xC0AF4E, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_tile_copy_fits },
    { field_unpack_the_map, 0xC0A33B, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_map_can_be_unpacked },
    { field_move_screen_effect_on, 0xC021E1, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_screen_effect_can_move_on },
    { field_do_script_layer_work, 0xC0274D, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_script_layer_work_can_run },
    { field_handle_requests, 0xC00C76, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_requests_can_run },
    { field_return_from_battle, 0xC00715, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_battle_return_can_run },
    { field_start, 0xC00B86, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_low_bank },
    { field_first_start, 0xC0000E, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { field_enter, 0xC00000, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_location_is_a_number },
    { field_start_again, 0xC00CC0, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { field_frame, 0xC00097, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_frame_goes_on_with_the_requests, 0xC000A4, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_frame_goes_on_with_the_walk, 0xC000A7, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_frame_goes_on_with_confirm, 0xC000AA, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_frame_goes_on_with_the_dialog_window, 0xC000AD, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_frame_goes_on_with_the_screen_effect, 0xC000B1, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_frame_goes_on_with_the_script_layers, 0xC000B4, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_frame_goes_on_with_the_frame_work, 0xC000B7, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_wait_for_the_next_frame, 0xC000BA, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_next_frame, 0xC000BD, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { field_start_the_scene, 0xC03FD6, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page },
    { field_close_into_the_scene, 0xC02643, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page },
    { field_frame_work, 0xC000BF, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_refresh_effects, 0xC000DE, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_walk_a_frame, 0xC000EB, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_finish_frame, 0xC0EC60, 0, 0, field_bank_low_ram },
    { field_load_location, 0xC000F4, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_load_the_script_entry, 0xC056D4, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_script_can_be_loaded },
    { field_put_aside_for_a_menu, 0xC0011B, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_party_can_be_put_aside },
    { field_set_up_after_a_menu, 0xC001A5, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_can_be_set_up_again },
    { field_start_location_song, 0xC01B53, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_play_the_switch_sound_entry, 0xC01B90, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_sound_at_the_leader_can_play },
    { field_play_the_chest_sound_entry, 0xC01BA7, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_sound_at_the_leader_can_play },
    { field_start_the_battle_song_entry, 0xC00008, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { field_bring_the_music_back_entry, 0xC0000B, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { field_load_the_credits_letters_entry, 0xC06E27, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_load_tile_looks_entry, 0xC009DD, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_battle_begins, 0xC0024C, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_battle_can_begin },
    { field_after_battle, 0xC00283, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_can_take_up_after_battle },
    { field_load_bg3_tile_looks_entry, 0xC00A14, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_pause_and_menus_entry, 0xC018D9, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page_bank_zero },
    { field_press_start, 0xC01985, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_start_can_be_pressed },
    { field_bring_the_party_back, 0xC01A03, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_party_can_come_back },
    { field_draw_whole_figure, 0xC0E12A, CPU_FLAG_M | CPU_FLAG_D, CPU_FLAG_M, field_figure_can_be_drawn },
    { field_queue_due_turns, 0xC059D9, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M | CPU_FLAG_X, field_turns_can_be_counted },
    { field_run_queued_turns, 0xC05A46, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M | CPU_FLAG_X, field_turns_can_run },
    { field_draw_queued_frames, 0xC0CA76, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M | CPU_FLAG_X, field_queued_frames_can_be_drawn },
    { field_draw_next_frame, 0xC0CAD9, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M | CPU_FLAG_X, field_frames_can_draw },
};

bool field_machine_form_takes(Snes *s, void (*entry)(Snes *))
{
    for (unsigned i = 0; i < sizeof forms / sizeof forms[0]; i++) {
        if (forms[i].entry != entry)
            continue;
        if (!s->cpu.e && (s->cpu.p & forms[i].mask) == forms[i].value &&
            (!forms[i].takes || forms[i].takes(s)))
            return true;
        program_breaks_a_contract(state_of(s), forms[i].address, "a field call outside its contract");
        return false;
    }
    return false;
}
