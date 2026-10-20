/* The cursors a screen holds while the player's cursor moves on, and the
 * scroll bar's start (game/menu/held_cursors.c). */
#ifndef CT_GAME_MENU_HELD_CURSORS_H
#define CT_GAME_MENU_HELD_CURSORS_H

#include <stdbool.h>

#include "snes/system.h"
#include "state.h"

/* A second cursor held where the player's was (menu_held_cursor), blinking
 * as menu_held_cursor_blink says; a frame of it. */
void menu_hold_a_cursor(GameState *g);
void menu_draw_the_held_cursor(GameState *g);

/* A second cursor held on the item the items screen picked up, kept on
 * its row as the list scrolls and slides; a frame of it. */
void menu_hold_the_cursor_on_the_list(GameState *g);
void menu_draw_the_cursor_held_on_the_list(GameState *g);

/* A second cursor held on the member the party exchange picked first, kept
 * on its row as the reserve scrolls and slides; a frame of it. */
void menu_hold_the_cursor_on_a_member(GameState *g);
void menu_draw_the_cursor_held_on_a_member(GameState *g);

/* A cursor on every active member, from the held cursor's place on, each
 * blinking in its own time; a frame of them. */
void menu_put_cursors_on_every_member(GameState *g);
void menu_draw_cursors_on_every_member(GameState *g);

/* The scroll bar beside a list shown, and its task started; a frame of its
 * arrows. */
void menu_start_the_scroll_bar(GameState *g);
void menu_draw_the_scroll_bar(GameState *g);

/* The registered entries ($C2:8A98, $C2:8ABA, $C2:89E4, $C2:89FA,
 * $C2:8A31, $C2:8A45, $C2:8AD5, $C2:8AFD, $C2:8B93) and their guards; the
 * scroll bar's task's (menu_run_the_scroll_bar_task, menu/tasks.c). */
void menu_hold_a_cursor_entry(Snes *s);
void menu_draw_the_held_cursor_entry(Snes *s);
void menu_hold_the_cursor_on_the_list_entry(Snes *s);
void menu_draw_the_cursor_held_on_the_list_entry(Snes *s);
void menu_hold_the_cursor_on_a_member_entry(Snes *s);
void menu_draw_the_cursor_held_on_a_member_entry(Snes *s);
void menu_put_cursors_on_every_member_entry(Snes *s);
void menu_draw_cursors_on_every_member_entry(Snes *s);
void menu_start_the_scroll_bar_entry(Snes *s);
bool menu_scroll_bar_is_drawable(Snes *s);
bool menu_held_cursor_is_drawable(Snes *s);
bool menu_members_cursors_are_drawable(Snes *s);
bool menu_list_cursor_is_drawable(Snes *s);
bool menu_member_cursor_is_drawable(Snes *s);

#endif
