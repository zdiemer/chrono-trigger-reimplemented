/* Menu mode: the routines the world maps' scenes call from their scripts
 * (game/menu/object_commands.c; the per-line effects are menu/line_effects.h's,
 * the Epoch's area check menu/epoch.h's). */
#ifndef CT_GAME_MENU_OBJECT_COMMANDS_H
#define CT_GAME_MENU_OBJECT_COMMANDS_H

#include "port/native.h"

bool menu_object_bank_reaches_low_ram(Snes *s);
bool menu_object_routine_page(Snes *s);
bool menu_object_routine_runs(Snes *s);

void menu_object_read_scene_mode(Snes *s);
void menu_object_clear_scene_flag(Snes *s);
void menu_object_keep_song(Snes *s);
void menu_object_let_song_go(Snes *s);
void menu_object_bob_second_plane(Snes *s);
void menu_object_raise_second_plane(Snes *s);
void menu_object_accelerate(Snes *s);
void menu_object_drift(Snes *s);
bool menu_object_jolt_runs(Snes *s);
void menu_object_jolt(Snes *s);
bool menu_object_can_be_placed(Snes *s);
void menu_object_place_under_the_view(Snes *s);
void menu_object_place_under_the_stretch(Snes *s);
void menu_object_give_random_velocity(Snes *s);
void menu_object_step_right_at_random(Snes *s);
void menu_object_place_in_the_east_stretch(Snes *s);
void menu_object_place_in_the_west_stretch(Snes *s);
void menu_object_place_about_the_view(Snes *s);
bool menu_object_can_be_placed_about_the_view(Snes *s);
void menu_object_hide_the_party(Snes *s);
void menu_object_keep_the_party(Snes *s);
void menu_object_bring_the_party_back(Snes *s);
void menu_object_mark_the_scene_played(Snes *s);
void menu_object_ask_the_story(Snes *s);
void menu_object_note_the_song(Snes *s);
void menu_object_load_mode7_graphics(Snes *s);
void menu_object_load_mode7_palette(Snes *s);
void menu_object_load_second_mode7_graphics(Snes *s);
void menu_object_load_second_mode7_palette(Snes *s);

void menu_object_accelerate_down(Snes *s);
void menu_object_mark_scene_event(Snes *s);
void menu_object_keep_first_scene_result(Snes *s);
void menu_object_keep_second_scene_result(Snes *s);

bool menu_scene_map_graphics_fit(Snes *s);
bool menu_scene_top_layer_fits(Snes *s);
void menu_object_load_scene_map_graphics(Snes *s);
void menu_object_load_scene_top_layer(Snes *s);
void menu_object_lay_out_scene_pattern(Snes *s);

bool menu_object_scene_edge_is_readable(Snes *s);
void menu_object_place_on_scene_edge(Snes *s);

#endif
