#include "charge.h"
/* The dialog and the menus: drawing a glyph of the proportional font into the
 * text's tiles.
 *
 * Text is drawn a glyph at a time into a buffer of tiles, which whoever sets
 * the text then sends to VRAM. A glyph is 12 pixels wide and 12 lines tall
 * (game/menu/menu_data.h, the font), in two bitplanes, and a pen moves along
 * the line of text by each glyph's width, so a glyph starts anywhere in a
 * tile: its 12 pixels reach into the next tile always and into a third when
 * the pen is five or more pixels into its own. What is drawn in the pen's
 * tile is added to what the glyph before left there, except at the tile's
 * left edge, where the tile is new; the tiles to its right are the glyph's
 * alone.
 *
 * The tiles are four-colour ones (two bitplanes, $10 bytes a tile, $100 a row
 * of 16) or sixteen-colour ones, of which a glyph fills the first two
 * bitplanes ($20 bytes a tile, $200 a row). A line of text is two rows of
 * tiles; the glyph sits four lines down its tile row, so its first four lines
 * go in the bottom half of the row's tiles and the other eight in the row
 * below.
 *
 * The typesetter (typeset.c) draws each glyph with menu_put_glyph. The
 * drawer's own entries ($C2:5E36, a whole glyph; $C2:5E89, a line of one)
 * take the glyph and the pen from the glyph page's cells, as the original's
 * setter leaves them there. */
#include "menu/glyphs.h"

#include "menu/menu.h"
#include "menu/menu_data.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* A glyph: 12 lines of two bitplanes, a byte each. */
enum { GLYPH_BYTES = 24, TILE_WIDTH = 8, GLYPH_WIDTH = 12 };

/* The glyph's top four lines (its first 8 bytes) go in the bottom half of
 * the pen's row, and the rest in the row below. */
enum { BYTES_IN_THE_PEN_ROW = 8, LINES_DOWN_THE_ROW = 8 };

/* How the tiles are laid out: a tile's bytes, a row of tiles' bytes. */
typedef struct {
    uint16_t tile_bytes, row_bytes;
    bool sixteen_colour;
} Tiles;

static const Tiles FOUR_COLOUR = { 0x10, 0x100, false }, SIXTEEN_COLOUR = { 0x20, 0x200, true };

static Tiles tiles_of(MenuTextTiles kind) { return kind == MENU_TILES_16_COLOUR ? SIXTEEN_COLOUR : FOUR_COLOUR; }

/* The glyph: where its left eight pixels and its right four are in the font,
 * and which half of the byte it shares its right four are in. */
typedef struct {
    uint32_t left, right;
    bool odd;
} Glyph;

/* The pen: which column of the line of text it is in, and how many pixels
 * into that column's tile. */
typedef struct {
    uint8_t column;
    uint8_t offset;
} Pen;

/* The font: a glyph's left pixels 24 bytes a glyph from $FF:2060, its right
 * four 24 bytes a pair from $FF:3860 (menu_data.h). The original works the
 * offsets out a word wide. */
enum { FONT_BANK = 0xFF0000u, LEFT_PIXELS = 0x2060, RIGHT_PIXELS = 0x3860 };

static Glyph glyph_numbered(uint16_t glyph)
{
    return (Glyph){ FONT_BANK | (uint16_t)(LEFT_PIXELS + glyph * GLYPH_BYTES),
                    FONT_BANK | (uint16_t)(RIGHT_PIXELS + (glyph >> 1) * GLYPH_BYTES), (glyph & 1) != 0 };
}

static Pen pen_at(uint8_t x) { return (Pen){ (uint8_t)(x / TILE_WIDTH), (uint8_t)(x % TILE_WIDTH) }; }

/* A line of the glyph in one bitplane (`byte` of its 24; the drawer's index
 * is a word, which the line entry takes as its caller leaves it), its 12
 * pixels in the top of 16 bits. */
static uint16_t glyph_pixels(const GameState *g, Glyph glyph, uint16_t byte)
{
    uint8_t left = menu_font_byte(g, glyph.left + byte);
    uint8_t shared = menu_font_byte(g, glyph.right + byte);
    uint8_t right = glyph.odd ? (uint8_t)(shared << 4) : (uint8_t)(shared & 0xF0);
    return (uint16_t)(left << 8 | right);
}

/* A line reaches a third tile when the pen is far enough into its own that
 * 12 pixels don't fit in two. */
static bool reaches_a_third_tile(uint8_t pen_offset) { return pen_offset + GLYPH_WIDTH > 2 * TILE_WIDTH; }

/* Where `column`'s tile has the line at `at` (a place in the pen's tile). */
static uint32_t in_column(const GameState *g, Tiles t, uint32_t at, unsigned column)
{
    uint16_t offset = t.sixteen_colour ? menu_text_column_offset_16_colour(g, column) : menu_text_column_offset(g, column);
    return (at + offset) & 0xFFFFFFu;
}

/* Put a line of pixels at the pen, `at` being the line's byte in the pen's
 * tile: shifted along by the pen's offset, the pixels fill the tiles from the
 * right, the pen's own last, where they join what is there already. */
static void put_pixels(GameState *g, Tiles t, uint32_t at, Pen p, uint16_t pixels)
{
    uint32_t across = (uint32_t)pixels << TILE_WIDTH >> p.offset;   /* the three tiles' bytes, the pen's first */
    if (reaches_a_third_tile(p.offset))
        wram_write8(g, in_column(g, t, at, p.column + 2u), (uint8_t)across);
    wram_write8(g, in_column(g, t, at, p.column + 1u), (uint8_t)(across >> 8));
    uint32_t own = in_column(g, t, at, p.column);
    uint8_t mine = (uint8_t)(across >> 16);
    wram_write8(g, own, p.offset == 0 ? mine : (uint8_t)(mine | wram_read8(g, own)));
}

/* Where the glyph's byte goes, from where its first four lines start: the
 * row below takes the rest. */
static uint32_t glyph_byte_place(Tiles t, uint32_t first, uint8_t byte)
{
    uint32_t down = byte < BYTES_IN_THE_PEN_ROW ? byte : (uint32_t)(t.row_bytes - 2 * BYTES_IN_THE_PEN_ROW + byte);
    return (first + down) & 0xFFFFFFu;
}









/* ---- Drawing ----------------------------------------------------------------------------- */

static void draw_glyph_from(GameState *g, Tiles t, Glyph glyph, Pen p, uint32_t first)
{
    uint16_t pixels = 0;
    for (uint8_t byte = 0; byte < GLYPH_BYTES; byte++) {
        pixels = glyph_pixels(g, glyph, byte);
        put_pixels(g, t, glyph_byte_place(t, first, byte), p, pixels);
    }
    charge_machine_accesses(g, 2);
    /* Two-byte progress, one-byte count and three-byte destination. */
    charge_machine_accesses(g, 6);
}

// $C2:5DC4 (the glyph and the pen worked out), $C2:5E36, $C2:5F07
/* Draw glyph `glyph` with the pen `pen_x` pixels along the line of text
 * whose first tile row is at `line`. */
void menu_put_glyph(GameState *g, uint16_t glyph, uint8_t pen_x, uint32_t line, MenuTextTiles kind)
{
    uint32_t first = (line & 0xFF0000u) | (uint16_t)(line + LINES_DOWN_THE_ROW);
    charge_machine_accesses(g, 13);
    draw_glyph_from(g, tiles_of(kind), glyph_numbered(glyph), pen_at(pen_x), first);
}

enum { GLYPH_PAGE = 0x0200, FONT_TABLES_BANK = 0xC2 };
