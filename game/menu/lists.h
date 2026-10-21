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

#include "snes/system.h"
#include "state.h"

/* Whether the scrolling list on the screen scrolls at all: it has places
 * past the rows it shows (its frame, the original's $C2:EA81, counts them). */
bool menu_list_scrolls(const GameState *g);

/* The cursor pushed along the list ($C2:EA53): a list that scrolls scrolls a
 * row where the push took the cursor past its rows (menu_list_scrolls_a_row),
 * which its screen does, and keeps a held Start repeating fast; one that
 * doesn't wraps the cursor round its places. Its entry and the screens'
 * steps' run of it are in menu/bridge.h. */
void menu_keep_the_cursor_on_the_list(GameState *g);
bool menu_list_scrolls_a_row(const GameState *g);

/* Whether the list slid up (its step negative, as its low byte has it). */
bool menu_list_slid_up(const GameState *g);

/* The rows of a list whose buffer starts at `rows_at` in bank $7E, the list
 * showing `shown_bytes` of them, moved a row (`row_bytes`) along once it has
 * slid: down the list, each row a row up, the new one already in the last;
 * up the list, each row a row down and the new one from the last to the
 * first. */
void menu_move_list_rows(GameState *g, uint16_t rows_at, unsigned shown_bytes, unsigned row_bytes, bool up);

/* A list's slide a row along, over a few frames. The list's rows are
 * shown by bands of the screen's per-line table, a row's twelve lines each
 * at the layer's vertical scroll that shows the row there; the screen names
 * the table (menu_block_move_dest, menu_block_move_size) and how many rows
 * move (menu_slide_rows), and draws the row coming in at the rows' buffer's
 * end. menu_start_list_slide makes the slide's bands from the table as it
 * rests: the rows, with the row coming in above them (a slide up) and the
 * row below them. A frame of the slide (menu_slide_list_a_frame, once the
 * screen has added the frame's step to menu_scroll_moved) shows as many
 * bands as there are rows, moved that far: the row going out at one end
 * loses the lines the one coming in at the other gains. Once it has slid,
 * menu_put_the_list_bands_back puts the table back as it rested, for the
 * screen to move its rows a row along in their buffer (menu_move_list_rows). */
void menu_start_list_slide(GameState *g);
void menu_slide_list_a_frame(GameState *g);
void menu_put_the_list_bands_back(GameState *g);

/* Their entries ($C2:EB9B; $C2:EB1F, which then waits for the next frame),
 * and their guards. */
void menu_start_list_slide_entry(Snes *s);
void menu_slide_list_a_frame_entry(Snes *s);
bool menu_list_slide_is_startable(Snes *s);
bool menu_list_slide_frame_is_showable(Snes *s);

/* A scrolling list's frame ($C2:EA81): the list laid out for its paging, its
 * cursor and its scroll bar, `places` places, `rows` of them in view at once,
 * and the bar's thumb moving over `track` lines (menu_frame_the_list, below).
 * The entry takes the places in Y and the rows and track where its callers
 * leave them, and the bridges that run it as C leave the registers as it
 * does. */
void menu_frame_the_list(GameState *g, uint8_t places, uint8_t rows, uint16_t track);
void menu_frame_the_list_entry(Snes *s);
void menu_leave_the_list_framed(Snes *s, uint8_t places);
bool menu_list_frame_is_layable(Snes *s);

/* A scrolling list paged with the shoulder assignments, its first row at
 * `top`: back a page or on a page, its rows as its frame laid them out
 * (menu_paging_rows, menu_paging_last_top). The new first row, `top` where
 * no page turns; a page that turns moves the scroll bar's thumb there and
 * ticks, and paging past an end puts the cursor on the end's place. */
uint8_t menu_turn_page(GameState *g, uint8_t top);

/* Whether a page turns for the frame's input, asked before the paging. */
bool menu_page_turns(const GameState *g, uint8_t top);

/* Its entry ($C2:93BF): the first row in menu_paging_top, the answer in A
 * $FF where a page turned. */
void menu_turn_page_entry(Snes *s);

#endif
