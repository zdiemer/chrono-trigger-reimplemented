/* Menu: the save file screen's slot display and the per-frame sprite reset. */
#ifndef CT_GAME_MENU_SAVE_SCREEN_H
#define CT_GAME_MENU_SAVE_SCREEN_H

#include "snes/system.h"

/* menu/save_screen.c */
void menu_reset_sprites(Snes *s);
void menu_load_row_table(Snes *s);
void menu_draw_member_portrait(Snes *s);
void menu_draw_save_slot(Snes *s);
void menu_show_save_slot(Snes *s);

#endif
