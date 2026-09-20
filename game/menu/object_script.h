/* Menu: the object scripts' command handlers (bank C2). */
#ifndef CT_GAME_MENU_OBJECT_SCRIPT_H
#define CT_GAME_MENU_OBJECT_SCRIPT_H

#include "snes/system.h"

/* The commands run on the menus' page with the data bank on low RAM. */
bool menu_object_script_page_is_menu(Snes *s);

/* Starting objects, and the position step every motion command makes. */
void menu_object_spawn(Snes *s);
void menu_object_spawn_high(Snes *s);
void menu_object_start_script(Snes *s);
void menu_object_start_script_high(Snes *s);
void menu_object_step_position(Snes *s);

/* The commands, in the order of the table at $C2:0F91. */
void menu_object_cmd_reset_state(Snes *s);
void menu_object_cmd_set_sprite_palette(Snes *s);
void menu_object_cmd_set_sprite_priority(Snes *s);
void menu_object_cmd_start_sprite_object(Snes *s);
void menu_object_cmd_load_palette_row(Snes *s);
void menu_object_cmd_change_location(Snes *s);
void menu_object_cmd_wait_frame(Snes *s);
void menu_object_cmd_write_table_byte(Snes *s);
void menu_object_cmd_set_scene_script(Snes *s);
void menu_object_cmd_start_object(Snes *s);
void menu_object_cmd_clear_slot_byte(Snes *s);
void menu_object_cmd_increment_slot_byte(Snes *s);
void menu_object_cmd_decrement_slot_byte(Snes *s);
void menu_object_cmd_set_slot_byte(Snes *s);
void menu_object_cmd_set_slot_bits(Snes *s);
void menu_object_cmd_clear_slot_bits(Snes *s);
void menu_object_cmd_clear_cell(Snes *s);
void menu_object_cmd_increment_cell(Snes *s);
void menu_object_cmd_decrement_cell(Snes *s);
void menu_object_cmd_set_cell(Snes *s);
void menu_object_cmd_set_cell_bits(Snes *s);
void menu_object_cmd_clear_cell_bits(Snes *s);
void menu_object_cmd_store_slot_byte(Snes *s);
void menu_object_cmd_load_slot_byte(Snes *s);
void menu_object_cmd_copy_slot_byte(Snes *s);
void menu_object_cmd_copy_cell(Snes *s);
void menu_object_cmd_jump(Snes *s);
void menu_object_cmd_loop_slot_byte(Snes *s);
void menu_object_cmd_branch_if_slot_byte_zero(Snes *s);
void menu_object_cmd_branch_if_slot_byte_set(Snes *s);
void menu_object_cmd_branch_if_slot_byte_not(Snes *s);
void menu_object_cmd_branch_if_slot_byte_is(Snes *s);
void menu_object_cmd_branch_if_slot_bits_set(Snes *s);
void menu_object_cmd_branch_if_slot_bits_clear(Snes *s);
void menu_object_cmd_branch_if_cell_zero(Snes *s);
void menu_object_cmd_branch_if_cell_set(Snes *s);
void menu_object_cmd_branch_if_cell_not(Snes *s);
void menu_object_cmd_branch_if_cell_is(Snes *s);
void menu_object_cmd_branch_if_cell_bits_set(Snes *s);
void menu_object_cmd_branch_if_cell_bits_clear(Snes *s);
void menu_object_cmd_branch_if_cell_below(Snes *s);
void menu_object_cmd_branch_if_cell_at_least(Snes *s);
void menu_object_cmd_fade_out(Snes *s);
void menu_object_cmd_fade_in(Snes *s);
void menu_object_cmd_mosaic_on(Snes *s);
void menu_object_cmd_mosaic_off(Snes *s);
void menu_object_cmd_set_position(Snes *s);
void menu_object_cmd_nothing(Snes *s);
void menu_object_cmd_set_velocity_x(Snes *s);
void menu_object_cmd_set_velocity_y(Snes *s);
void menu_object_cmd_set_sprite_script(Snes *s);
void menu_object_cmd_animate_for(Snes *s);
void menu_object_cmd_scroll_map_for(Snes *s);
void menu_object_cmd_queue_vram_upload(Snes *s);
void menu_object_cmd_call_routine(Snes *s);
void menu_object_cmd_start_handler(Snes *s);
void menu_object_cmd_call(Snes *s);
void menu_object_cmd_return(Snes *s);
void menu_object_cmd_wait_for(Snes *s);
void menu_object_cmd_animate_wait_for(Snes *s);
void menu_object_cmd_stop_after_age(Snes *s);
void menu_object_cmd_sound_18(Snes *s);
void menu_object_cmd_sound_19(Snes *s);
void menu_object_cmd_play_song_unless_flag(Snes *s);
void menu_object_cmd_redraw_map(Snes *s);
void menu_object_cmd_move_x_to(Snes *s);
void menu_object_cmd_move_y_to(Snes *s);
void menu_object_cmd_wait_frame_again(Snes *s);
void menu_object_cmd_start_script(Snes *s);
void menu_object_cmd_start_handler_high(Snes *s);
void menu_object_cmd_mark_table_entry(Snes *s);
void menu_object_cmd_unmark_table_entry(Snes *s);
void menu_object_cmd_add_to_slot_byte(Snes *s);
void menu_object_cmd_subtract_from_slot_byte(Snes *s);
void menu_object_cmd_add_to_cell(Snes *s);
void menu_object_cmd_subtract_from_cell(Snes *s);
void menu_object_cmd_play_song(Snes *s);
void menu_object_cmd_sound_command(Snes *s);
void menu_object_cmd_call_routine_long(Snes *s);
void menu_object_cmd_copy_tilemap_rows(Snes *s);
void menu_object_cmd_copy_tilemap_block(Snes *s);
void menu_object_cmd_scroll_map_plane_for(Snes *s);
void menu_object_cmd_end(Snes *s);

#endif
