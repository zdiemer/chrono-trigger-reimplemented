/* The load screen's save slots (game/menu/save_screen.c). */
#ifndef CT_GAME_MENU_SAVE_SCREEN_H
#define CT_GAME_MENU_SAVE_SCREEN_H

#include "snes/system.h"
#include "state.h"

/* The load screen has a row to show (a slot with a game, or the fourth row),
 * every slot's summary is one it can draw, and its drawing's counts have
 * room. */
bool menu_save_slots_can_show(const GameState *g);

void menu_read_save_slots(Snes *s);
void menu_read_slot_summary(Snes *s);
void menu_read_design_and_chapter(Snes *s);
void menu_draw_chapter_label(Snes *s);
void menu_draw_empty_label(Snes *s);
void menu_draw_slot_name(Snes *s);
void menu_load_slot_rows_effect(Snes *s);
void menu_draw_member_portrait(Snes *s);
void menu_draw_save_slot(Snes *s);
void menu_show_save_slot(Snes *s);

bool menu_slot_rows_effect_is_loadable(Snes *s);
bool menu_save_slots_are_readable(Snes *s);
bool menu_slot_labels_are_drawable(Snes *s);
bool menu_chapter_label_is_drawable(Snes *s);
bool menu_slot_summary_is_readable(Snes *s);
bool menu_portrait_is_drawable(Snes *s);
bool menu_save_slot_is_drawable(Snes *s);
bool menu_save_slots_are_showable(Snes *s);

#endif
