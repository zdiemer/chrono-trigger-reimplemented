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
bool menu_object_can_be_placed(Snes *s);
void menu_object_place_under_the_view(Snes *s);
void menu_object_place_under_the_stretch(Snes *s);
void menu_object_give_random_velocity(Snes *s);
void menu_object_load_mode7_graphics(Snes *s);
void menu_object_load_mode7_palette(Snes *s);
void menu_object_load_second_mode7_graphics(Snes *s);
void menu_object_load_second_mode7_palette(Snes *s);

#endif
