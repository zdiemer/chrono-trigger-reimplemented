/* Menu: text, numbers and tiles drawn into tilemap buffers. */
#ifndef CT_GAME_MENU_TEXT_H
#define CT_GAME_MENU_TEXT_H

#include "snes/system.h"

/* menu/text.c */
void menu_number_to_bcd(Snes *s);
void menu_draw_number(Snes *s);
void menu_draw_tile(Snes *s);
void menu_draw_text(Snes *s);

#endif
