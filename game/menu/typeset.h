/* The typesetter: a string of the game's text set in the proportional font,
 * a few glyphs at a time (game/menu/typeset.c). */
#ifndef CT_GAME_MENU_TYPESET_H
#define CT_GAME_MENU_TYPESET_H

#include <stdbool.h>
#include <stdint.h>

#include "snes/system.h"
#include "state.h"

/* How a text is laid out in its tiles (menu_text_layout): the low four bits
 * the kind of tiles (none four-colour, any sixteen-colour; a tile further
 * left sixteen-colour tiles whose lines start a tile further in), and the top
 * bit lines that start at the tiles' very edge. */
typedef uint8_t MenuTextLayout;
enum {
    MENU_TEXT_FOUR_COLOUR = 0x00,
    MENU_TEXT_A_TILE_FURTHER_LEFT = 0x02,
    MENU_TEXT_TILE_KIND = 0x0F,
    MENU_TEXT_LINES_AT_THE_EDGE = 0x80,
};

/* Start the string the text page names (menu_text: which string of which
 * table, the tiles, the layout), and set glyphs of it until it stops the call
 * or the call's budget runs out (menu_text_stop says which). */
void menu_text_start(GameState *g);
void menu_text_go_on(GameState *g);

/* Set a label, string `string` of the table at `strings`, in four-colour
 * tiles at `tiles` in work RAM (bank $7E), a line of up to 32 glyphs at a
 * time, each line $400 bytes below the one before. Returns how wide its last
 * line is, in pixels. */
uint8_t menu_set_label(GameState *g, uint32_t strings, uint8_t string, uint16_t tiles);

/* The entries, and their guards. */
void menu_text_start_entry(Snes *s);
void menu_text_go_on_entry(Snes *s);
void menu_set_label_entry(Snes *s);
bool menu_text_is_startable(Snes *s);
bool menu_text_can_go_on(Snes *s);
bool menu_label_is_settable(Snes *s);

/* What the label's setting leaves in the registers, for a port that sets a
 * label as C where the original's caller reads them after. */
void menu_leave_label_set(Snes *s);

#endif
