/* Menu mode: the bank C6 routines the menus' object scripts call
 * (game/menu/object_commands.c; the per-line effects are menu/line_effects.h's). */
#ifndef CT_GAME_MENU_OBJECT_COMMANDS_H
#define CT_GAME_MENU_OBJECT_COMMANDS_H

#include "port/native.h"

bool menu_object_bank_reaches_low_ram(Snes *s);
bool menu_object_routine_page(Snes *s);
bool menu_object_routine_runs(Snes *s);
bool menu_epoch_on_world_map(Snes *s);

void menu_object_read_scene_mode(Snes *s);
void menu_object_clear_scene_flag(Snes *s);
void menu_object_keep_song(Snes *s);
void menu_object_let_song_go(Snes *s);
void menu_object_bob_second_plane(Snes *s);
void menu_object_raise_second_plane(Snes *s);
void menu_object_accelerate(Snes *s);
void menu_object_drift(Snes *s);
void menu_epoch_outside_sides(Snes *s);
void menu_epoch_in_area(Snes *s);
void menu_object_load_scene_graphics_2(Snes *s);
void menu_object_load_scene_palette_1(Snes *s);
void menu_object_load_scene_graphics_13(Snes *s);
void menu_object_load_scene_palette_2(Snes *s);

#endif
