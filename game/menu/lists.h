/* The menus' scrolling lists of items -- the items screen's list of the
 * party's items, the equipment screen's list of what fits a slot -- as their
 * rows sit in a tilemap buffer in work RAM (game/menu/lists.c).
 *
 * A list's rows are kept a row apart in a buffer with a row more than the
 * list shows. A list slides a row at a time: the row coming in is drawn in
 * the buffer's last row, the rows' lines move over a few frames (the
 * original's $C2:EB9B and $C2:EB1F), and then the rows are moved a row along
 * in the buffer so the list rests again. */
#ifndef CT_GAME_MENU_LISTS_H
#define CT_GAME_MENU_LISTS_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

/* Whether the scrolling list on the screen scrolls at all: it has places
 * past the rows it shows (its frame, the original's $C2:EA81, counts them). */
bool menu_list_scrolls(const GameState *g);

/* Whether the list slid up (its step negative, as its low byte has it). */
bool menu_list_slid_up(const GameState *g);

/* The rows of a list whose buffer starts at `rows_at` in bank $7E, the list
 * showing `shown_bytes` of them, moved a row (`row_bytes`) along once it has
 * slid: down the list, each row a row up, the new one already in the last;
 * up the list, each row a row down and the new one from the last to the
 * first. */
void menu_move_list_rows(GameState *g, uint16_t rows_at, unsigned shown_bytes, unsigned row_bytes, bool up);

#endif
