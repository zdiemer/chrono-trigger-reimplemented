/* The typesetter: a string of the game's text set in the proportional font,
 * a few glyphs at a time (game/menu/typeset.c). */
#ifndef CT_GAME_MENU_TYPESET_H
#define CT_GAME_MENU_TYPESET_H

#include <stdbool.h>

#include "snes/system.h"
#include "state.h"

/* Start the string the text page names (menu_text: which string of which
 * table, the tiles, the layout), and set glyphs of it until it stops the call
 * or the call's budget runs out (menu_text_stop says which). */
void menu_text_start(GameState *g);
void menu_text_go_on(GameState *g);

/* Set a label of the menus' table the text page names, in four-colour tiles
 * at `tiles` in work RAM (bank $7E), a line of up to 32 glyphs at a time,
 * each line $400 bytes below the one before. */
void menu_set_label(GameState *g, uint16_t tiles);

/* The entries, and their guards. */
void menu_text_start_entry(Snes *s);
void menu_text_go_on_entry(Snes *s);
void menu_set_label_entry(Snes *s);
bool menu_text_is_startable(Snes *s);
bool menu_text_can_go_on(Snes *s);
bool menu_label_is_settable(Snes *s);

#endif
