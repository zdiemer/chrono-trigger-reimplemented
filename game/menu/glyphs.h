/* Drawing a glyph of the proportional font into the text's tiles
 * (game/menu/glyphs.c). */
#ifndef CT_GAME_MENU_GLYPHS_H
#define CT_GAME_MENU_GLYPHS_H

#include <stdbool.h>
#include <stdint.h>

#include "snes/system.h"
#include "state.h"

/* The tiles a text is set in: four-colour (the dialog's and the menus') or
 * sixteen-colour, of which a glyph fills the first two bitplanes. */
typedef enum { MENU_TILES_4_COLOUR, MENU_TILES_16_COLOUR } MenuTextTiles;

/* Draw glyph `glyph` (the font's numbering: $A0 up, and the two-byte ones
 * $1xx and $2xx) with the pen `pen_x` pixels along the line of text whose
 * first tile row is at `line` (a 24-bit address in work RAM). */
void menu_put_glyph(GameState *g, uint16_t glyph, uint8_t pen_x, uint32_t line, MenuTextTiles tiles);

/* The drawer's entries, which take the glyph and the pen from the glyph
 * page's cells, and their guards. */
void menu_draw_glyph_line(Snes *s);
void menu_draw_glyph(Snes *s);
void menu_draw_glyph_16_colour(Snes *s);
bool menu_glyph_is_drawable(Snes *s);
bool menu_glyph_16_colour_is_drawable(Snes *s);
bool menu_glyph_line_is_drawable(Snes *s);

#endif
