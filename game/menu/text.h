/* The menus' text: strings, tiles and numbers drawn into the tilemaps in work
 * RAM (game/menu/text.c). */
#ifndef CT_GAME_MENU_TEXT_H
#define CT_GAME_MENU_TEXT_H

#include "snes/system.h"

void menu_number_to_bcd(Snes *s);
void menu_draw_number(Snes *s);
void menu_draw_tile(Snes *s);
void menu_draw_text(Snes *s);

bool menu_text_on_page(Snes *s);
bool menu_tile_on_page(Snes *s);
bool menu_number_is_drawable(Snes *s);
bool menu_number_is_convertible(Snes *s);

#endif
