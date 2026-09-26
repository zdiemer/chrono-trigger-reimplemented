/* The typesetter: a string of the game's text set in the proportional font,
 * a few glyphs at a time (game/menu/typeset.c). */
#ifndef CT_GAME_MENU_TYPESET_H
#define CT_GAME_MENU_TYPESET_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/glyphs.h"
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

/* The kind of tiles a layout sets its text in. */
MenuTextTiles menu_text_tiles_of_layout(MenuTextLayout l);

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

/* A string read without being set, a piece at a time, for the guards, which
 * read ahead of a call, and the menus' leaves (menu/leaves.c), which read
 * back over one: a glyph; what starts an insert (a word of the dictionary, a
 * number, a name); or a code the setting stops a call at (the end, a wait, a
 * break, a pause, and $12's choices that aren't a name or a word). `code` is
 * the piece's first byte. */
typedef enum { MENU_TEXT_GLYPH, MENU_TEXT_INSERT, MENU_TEXT_STOP } MenuTextPieceKind;
typedef struct {
    MenuTextPieceKind kind;
    uint8_t code;
    uint8_t bytes;
    uint8_t arguments;   /* bytes it takes of the string's arguments */
} MenuTextPiece;

MenuTextPiece menu_text_piece_at(const GameState *g, uint32_t at);
uint32_t menu_text_past(uint32_t at, MenuTextPiece piece);

/* Where the text page's string begins; whether its text has ended; whether
 * the setting is partway through an insert. */
uint32_t menu_text_string_begins(const GameState *g);
bool menu_text_has_ended(const GameState *g);
bool menu_text_in_an_insert(const GameState *g);

/* A glyph's index among the font's (its width's place). */
uint8_t menu_text_glyph_index(uint16_t glyph);

/* Whether a label can be set: from memory, into work RAM, with none of its
 * names or lines on the text page (typeset.c's guards say why). */
bool menu_label_can_be_set(const GameState *g, uint32_t strings, uint8_t string, uint16_t tiles);

/* The entries, and their guards. */
void menu_text_start_entry(Snes *s);
void menu_text_go_on_entry(Snes *s);
void menu_set_label_entry(Snes *s);
bool menu_text_is_startable(Snes *s);
bool menu_text_can_go_on(Snes *s);
bool menu_label_is_settable(Snes *s);

#endif
