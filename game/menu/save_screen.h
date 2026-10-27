/* The save slots' summaries, and the load and save screens' frames that show
 * them as the cursor moves (game/menu/save_screen.c). */
#ifndef CT_GAME_MENU_SAVE_SCREEN_H
#define CT_GAME_MENU_SAVE_SCREEN_H

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* The load screen has a row to show (a slot with a game, or the fourth row),
 * every slot's summary is one it can draw, and its drawing's counts have
 * room. */
bool menu_save_slots_can_show(const GameState *g);

/* The save screen can show the slot under its cursor as it waits for a
 * choice ($C2:CFA2): the cursor on one of the rows, a summary there the
 * drawing can take, and the drawing's counts with room. */
bool menu_slot_under_the_cursor_shows(const GameState *g);

/* The slots' rows' per-line effect for the next frame, the slot marked's row
 * marked ($C2:CFFB). */
void menu_put_slot_rows_effect(GameState *g);

/* The summary of the slot shown drawn, where it holds a game: its header and
 * era label, its members' rows and portraits ($C2:D36C). */
void menu_draw_the_slot_shown(GameState *g, Platform *pf);

void menu_read_save_slots(Snes *s);
/* The same as C ($C2:D156): the three slots read as the menus open; false
 * where a guard's state half doesn't take a slot, the program going no
 * further there. */
bool menu_save_slots_read(GameState *g, Platform *pf);
void menu_read_slot_summary(Snes *s);
/* A slot's summary read back once its game is saved ($C2:D19F), as C; false
 * where its guard's state half doesn't take it, the program going no
 * further there. */
bool menu_slot_summary_read_back(GameState *g, Platform *pf);
void menu_read_design_and_chapter(Snes *s);
void menu_draw_chapter_label(Snes *s);
void menu_draw_empty_label(Snes *s);
void menu_draw_slot_name(Snes *s);
void menu_load_slot_rows_effect(Snes *s);
void menu_draw_member_portrait(Snes *s);
void menu_draw_save_slot(Snes *s);
void menu_show_save_slot(Snes *s);
void menu_move_among_the_slots(Snes *s);

bool menu_slot_rows_effect_is_loadable(Snes *s);
bool menu_save_slots_are_readable(Snes *s);
bool menu_slot_labels_are_drawable(Snes *s);
bool menu_chapter_label_is_drawable(Snes *s);
bool menu_slot_summary_is_readable(Snes *s);
bool menu_portrait_is_drawable(Snes *s);
bool menu_save_slot_is_drawable(Snes *s);
bool menu_save_slots_are_showable(Snes *s);
bool menu_slot_under_the_cursor_is_showable(Snes *s);

#endif
