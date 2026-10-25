/* The description window: the line of text that says what a screen's
 * cursor is on, typed a few glyphs a frame (game/menu/description.c). */
#ifndef CT_GAME_MENU_DESCRIPTION_H
#define CT_GAME_MENU_DESCRIPTION_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* What a screen describes -- an item, a tech, a tech kind's caption, a
 * setting -- is a text of the screen's table, by number. A screen asks for
 * one with where its row is (menu_description_line) and how many glyphs a
 * frame it types (menu_description_pace) set; only something other than
 * what the window shows, or what the screen has marked stale, is typed. */
bool menu_description_shows_another(const GameState *g, uint8_t which);

/* Describe `which`, a text of the table at `texts` (a long pointer to a word
 * each), on the row that goes to VRAM at `row_vram`: the row cleared and the
 * typing started. */
void menu_describe(GameState *g, uint8_t which, uint32_t texts, uint16_t row_vram);

/* The typing started on what the description's cells name (its row, its
 * text and table, its pace, where its row goes in VRAM): the row cleared
 * and the text's first glyphs set at once. */
void menu_start_typing(GameState *g);

/* An item's description, on the items screen's, the equipment screen's and
 * the shop's row, a glyph a frame, where it is another item. */
void menu_describe_item(GameState *g, uint8_t item);

/* The settings screen's description: the option's text and a tile after it
 * its value's (`value_text`, a text of the same table); for the option the
 * window shows already, the value's alone, once it has changed and the
 * typing is done. */
void menu_describe_setting(GameState *g, uint8_t option, uint8_t value_text);

/* A frame of the typing's: what was typed sent to VRAM, the line's tiles
 * laid on the row as far as the pen got. */
void menu_send_the_typing(GameState *g);

/* The entries ($C2:F2F3, $C2:FA49, $C2:F943, $C2:FAE9; the typing's task,
 * $C2:FA9F, $C2:F9B2 and $C2:FA06 its three starts, and $C2:FAA8 the typing
 * itself), and their guards. */
void menu_describe_item_entry(Snes *s);

/* For the original's code and shadow mode: starting the typing leaves A
 * clear (the text's start, $C2:57DF, loads nothing over it). */
void menu_leave_typing_started(Snes *s);
void menu_start_typing_entry(Snes *s);
void menu_describe_setting_entry(Snes *s);
void menu_send_the_typing_entry(Snes *s);
/* The typing's three tasks, by their starts, as C (menu/tasks.c's
 * coroutines): a description, a setting's option and value, a value. */
enum { MENU_TYPE_A_DESCRIPTION = 0xFA9F, MENU_TYPE_A_SETTING = 0xF9B2, MENU_TYPE_A_VALUE = 0xFA06 };
void menu_type_the_description_task_as_c(GameState *g, Platform *pf);
void menu_type_the_setting_task_as_c(GameState *g, Platform *pf);
void menu_type_the_value_task_as_c(GameState *g, Platform *pf);
void menu_type_the_description_task(Snes *s);
void menu_type_the_setting_task(Snes *s);
void menu_type_the_value_task(Snes *s);
void menu_type_the_description(Snes *s);
bool menu_item_is_describable(Snes *s);
bool menu_typing_can_start(Snes *s);
bool menu_setting_is_describable(Snes *s);
bool menu_typing_can_be_sent(Snes *s);
bool menu_typing_can_go_on(Snes *s);
bool menu_setting_typing_can_start(Snes *s);
bool menu_value_typing_can_start(Snes *s);

#endif
