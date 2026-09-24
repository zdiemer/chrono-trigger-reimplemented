/* The load screen's save slots, and the menus' sprites each frame
 * (game/menu/save_screen.c). */
#ifndef CT_GAME_MENU_SAVE_SCREEN_H
#define CT_GAME_MENU_SAVE_SCREEN_H

#include "snes/system.h"

void menu_reset_sprites(Snes *s);
void menu_load_slot_rows_effect(Snes *s);
void menu_draw_member_portrait(Snes *s);
void menu_draw_save_slot(Snes *s);
void menu_show_save_slot(Snes *s);

bool menu_sprites_are_resettable(Snes *s);
bool menu_slot_rows_effect_is_loadable(Snes *s);
bool menu_portrait_is_drawable(Snes *s);
bool menu_save_slot_is_drawable(Snes *s);
bool menu_save_slots_are_showable(Snes *s);

#endif
