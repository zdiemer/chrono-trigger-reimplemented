/* Menu mode: the bank C6 routines the menus' object scripts call
 * (game/menu/object_commands.c). */
#ifndef CT_GAME_MENU_OBJECT_COMMANDS_H
#define CT_GAME_MENU_OBJECT_COMMANDS_H

#include "port/native.h"

void menu_object_read_mode(Snes *s);
void menu_object_clear_7f0062(Snes *s);
void menu_object_set_7f01ed(Snes *s);
void menu_object_clear_7f01ed(Snes *s);
void menu_object_advance_line_tables(Snes *s);
void menu_object_step_28(Snes *s);
void menu_object_step_28_down(Snes *s);
void menu_object_add_velocity(Snes *s);
void menu_object_drift(Snes *s);
void menu_epoch_outside_sides(Snes *s);
void menu_epoch_in_area(Snes *s);
bool menu_epoch_on_world_map(Snes *s);
void menu_object_load_graphics_2(Snes *s);
void menu_object_load_c3_stream_1(Snes *s);
void menu_object_load_graphics_13(Snes *s);
void menu_object_load_c3_stream_2(Snes *s);
void menu_object_window1_lines(Snes *s);
void menu_object_window2_lines(Snes *s);
void menu_object_bg1_scroll_lines(Snes *s);
bool menu_object_bank_reaches_io(Snes *s);
void menu_object_squashed_iris_1(Snes *s);
void menu_object_squashed_iris_2(Snes *s);
void menu_object_capsule_iris_1(Snes *s);
void menu_object_capsule_iris_2(Snes *s);

#endif
