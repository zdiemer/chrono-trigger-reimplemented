/* The menus' text: strings, tiles and numbers drawn into the tilemaps in work
 * RAM (game/menu/text.c). */
#ifndef CT_GAME_MENU_TEXT_H
#define CT_GAME_MENU_TEXT_H

#include "snes/system.h"
#include "state.h"

/* Draw the string at `string` (a 24-bit address: the ROM's strings, or names
 * in work RAM) at `pen` in a tilemap in bank $7E: `settings` is the text's
 * flags, and in its low seven bits the most tiles it may draw (0 for 256).
 * Returns where the pen got to. */
uint16_t menu_draw_string(GameState *g, uint32_t string, uint16_t pen, uint8_t settings);

/* The time played, hours and minutes, from its digits at `digits` in bank
 * $7E, drawn at `pen`; returns where the pen goes on. */
uint16_t menu_draw_time_played(GameState *g, uint16_t pen, uint16_t digits);

/* Whether the time's digits at `digits` are ones the clock keeps: each 0-9,
 * unless the clock has run past them. For the guards of whatever draws it. */
bool menu_time_digits_are_decimal(const GameState *g, uint16_t digits);

void menu_number_to_bcd(Snes *s);
void menu_draw_time(Snes *s);
bool menu_time_is_drawable(Snes *s);
void menu_draw_number(Snes *s);
void menu_draw_tile(Snes *s);
void menu_draw_text(Snes *s);

bool menu_text_on_page(Snes *s);
bool menu_tile_on_page(Snes *s);
bool menu_number_is_drawable(Snes *s);
bool menu_number_is_convertible(Snes *s);

#endif
