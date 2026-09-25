/* The load screen: its steps after the first, a slot's row, and a slot's
 * erasing (game/menu/load_screen.c). */
#ifndef CT_GAME_MENU_LOAD_SCREEN_H
#define CT_GAME_MENU_LOAD_SCREEN_H

#include "snes/system.h"

/* The screen's steps after its opening, in the order of the menus' table for
 * screen 14. */
void menu_choose_on_load_screen(Snes *s);
void menu_choose_game_to_carry_on_from(Snes *s);
void menu_answer_erase(Snes *s);

void menu_ask_which_to_carry_on_from(Snes *s);
void menu_erase_slot(Snes *s);
void menu_draw_slot_window(Snes *s);

bool menu_load_screen_runs(Snes *s);
bool menu_erase_is_answerable(Snes *s);
bool menu_slot_window_is_drawable(Snes *s);

#endif
