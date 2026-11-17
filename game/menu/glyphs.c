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
 * drawer's own routines ($C2:5E36, a whole glyph; $C2:5E89, a line of one)
 * take the glyph and the pen from the glyph page's cells, as the original's
 * setter leaves them there. */
#include "menu/glyphs.h"

#include "menu/menu.h"
#include "menu/menu_data.h"
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

/* The font: a glyph's left pixels 24 bytes a glyph from its start
 * ($FF:2060), its right four 24 bytes a pair after the left ones' 256
 * (menu_data.h). The original works the offsets out a word wide. */
enum { RIGHT_PIXELS = 0x1800 };

static uint32_t in_the_font(uint16_t offset)
{
    return asset_place_address(ASSET_DIALOG_FONT, (uint16_t)(asset_address(ASSET_DIALOG_FONT) + offset));
}

static Glyph glyph_numbered(uint16_t glyph)
{
    return (Glyph){ in_the_font((uint16_t)(glyph * GLYPH_BYTES)),
                    in_the_font((uint16_t)(RIGHT_PIXELS + (glyph >> 1) * GLYPH_BYTES)), (glyph & 1) != 0 };
}

static Pen pen_at(uint8_t x) { return (Pen){ (uint8_t)(x / TILE_WIDTH), (uint8_t)(x % TILE_WIDTH) }; }

/* A line of the glyph in one bitplane, from its left byte and the byte its
 * right four pixels share: its 12 pixels in the top of 16 bits. */
static uint16_t line_pixels(Glyph glyph, uint8_t left, uint8_t shared)
{
    uint8_t right = glyph.odd ? (uint8_t)(shared << 4) : (uint8_t)(shared & 0xF0);
    return (uint16_t)(left << 8 | right);
}

/* The line (`byte` of its 24, counted in a word as the original counts it)
 * read from the font. */
static uint16_t glyph_pixels(const GameState *g, Glyph glyph, uint16_t byte)
{
    return line_pixels(glyph, menu_font_byte(g, glyph.left + byte), menu_font_byte(g, glyph.right + byte));
}

/* A line reaches a third tile when the pen is far enough into its own that
 * 12 pixels don't fit in two. */
static bool reaches_a_third_tile(uint8_t pen_offset) { return pen_offset + GLYPH_WIDTH > 2 * TILE_WIDTH; }

/* How far into the text's tiles each of the pen's tile and the two after it
 * start, the pen's first (the third's only where a line reaches it). */
typedef struct {
    uint16_t of[3];
} Columns;

/* Read from the table of columns' offsets, as each line reads them. */
static Columns columns_at(const GameState *g, Tiles t, Pen p)
{
    Columns c = { { 0, 0, 0 } };
    for (unsigned k = reaches_a_third_tile(p.offset) ? 3 : 2; k-- > 0;)
        c.of[k] = t.sixteen_colour ? menu_text_column_offset_16_colour(g, p.column + k) : menu_text_column_offset(g, p.column + k);
    return c;
}

/* Put a line of pixels at the pen, `at` being the line's byte in the pen's
 * tile: shifted along by the pen's offset, the pixels fill the tiles from the
 * right, the pen's own last, where they join what is there already. */
static void put_pixels(GameState *g, Columns c, uint32_t at, Pen p, uint16_t pixels)
{
    uint32_t across = (uint32_t)pixels << TILE_WIDTH >> p.offset;   /* the three tiles' bytes, the pen's first */
    if (reaches_a_third_tile(p.offset))
        wram_write8(g, (at + c.of[2]) & 0xFFFFFFu, (uint8_t)across);
    wram_write8(g, (at + c.of[1]) & 0xFFFFFFu, (uint8_t)(across >> 8));
    uint32_t own = (at + c.of[0]) & 0xFFFFFFu;
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

/* A line of the glyph read from the font and put at the pen. */
static void draw_glyph_line(GameState *g, Tiles t, Glyph glyph, Pen p, uint32_t first, uint8_t byte)
{
    uint16_t pixels = glyph_pixels(g, glyph, byte);
    put_pixels(g, columns_at(g, t, p), glyph_byte_place(t, first, byte), p, pixels);
}

/* The same from the glyph's bytes and the columns' offsets found in the
 * cartridge once for the glyph, charged as the line's own reads of them: the
 * original reads both again for each of a glyph's 24 lines, and read so, a
 * screen of text's opening (the load screen's slots) was some tens of
 * thousands of lookups, a dozen GBA frames. */
static void draw_glyph_line_from(GameState *g, Tiles t, const uint8_t *left, const uint8_t *right, Glyph glyph,
                                 Columns c, Pen p, uint32_t first, uint8_t byte)
{
    charge_rom_accesses(g, 2u + 2u * (reaches_a_third_tile(p.offset) ? 3u : 2u));
    put_pixels(g, c, glyph_byte_place(t, first, byte), p, line_pixels(glyph, left[byte], right[byte]));
}

enum { GLYPH_LINE = 0xC25E89 };   /* the drawer's routine for a line of a glyph */

/* The glyph's lines as runs: in each column's tile, its first lines in the
 * pen's row and the rest in the row below, each part's bytes one after
 * another. Where the runs lie apart, a part's bytes in each column are
 * worked out together and put in one go, the pen's tile read first where the
 * glyph joins what is there: the same bytes as putting the lines one at a
 * time, which write each byte once. Each line's reads and writes are charged
 * as the line's own. */
enum { GLYPH_PARTS = 2, MOST_COLUMNS = 3, MOST_PART_LINES = GLYPH_BYTES - BYTES_IN_THE_PEN_ROW };
static const struct {
    uint8_t first, lines;
} glyph_parts[GLYPH_PARTS] = { { 0, BYTES_IN_THE_PEN_ROW }, { BYTES_IN_THE_PEN_ROW, GLYPH_BYTES - BYTES_IN_THE_PEN_ROW } };

typedef struct {
    uint32_t at[GLYPH_PARTS][MOST_COLUMNS];
    unsigned columns;
} GlyphRuns;

static bool runs_overlap(uint32_t a, unsigned a_bytes, uint32_t b, unsigned b_bytes) { return a < b + b_bytes && b < a + a_bytes; }

/* Where each part's run starts in each column, and whether they all lie apart. */
static bool glyph_runs(Tiles t, Columns c, Pen p, uint32_t first, GlyphRuns *r)
{
    r->columns = reaches_a_third_tile(p.offset) ? 3 : 2;
    for (unsigned part = 0; part < GLYPH_PARTS; part++)
        for (unsigned k = 0; k < r->columns; k++)
            r->at[part][k] = (glyph_byte_place(t, first, glyph_parts[part].first) + c.of[k]) & 0xFFFFFFu;
    for (unsigned i = 0; i < GLYPH_PARTS * r->columns; i++)
        for (unsigned j = i + 1; j < GLYPH_PARTS * r->columns; j++) {
            unsigned pi = i / r->columns, pj = j / r->columns;
            if (runs_overlap(r->at[pi][i % r->columns], glyph_parts[pi].lines, r->at[pj][j % r->columns],
                             glyph_parts[pj].lines))
                return false;
        }
    return true;
}

static void put_glyph_runs(GameState *g, const GlyphRuns *r, const uint8_t *left, const uint8_t *right, Glyph glyph,
                           Pen p)
{
    uint32_t mark = charge_uncounted_begin(g);
    for (unsigned part = 0; part < GLYPH_PARTS; part++) {
        unsigned lines = glyph_parts[part].lines;
        uint8_t bytes[MOST_COLUMNS][MOST_PART_LINES];
        if (p.offset != 0)
            wram_read_span(g, r->at[part][0], bytes[0], lines);
        for (unsigned i = 0; i < lines; i++) {
            uint8_t byte = (uint8_t)(glyph_parts[part].first + i);
            uint32_t across = (uint32_t)line_pixels(glyph, left[byte], right[byte]) << TILE_WIDTH >> p.offset;
            uint8_t mine = (uint8_t)(across >> 16);
            bytes[0][i] = p.offset == 0 ? mine : (uint8_t)(mine | bytes[0][i]);
            bytes[1][i] = (uint8_t)(across >> 8);
            bytes[2][i] = (uint8_t)across;
        }
        for (unsigned k = 0; k < r->columns; k++)
            wram_write_span(g, r->at[part][k], bytes[k], lines);
    }
    charge_uncounted_end(g, mark);
    charge_rom_accesses(g, GLYPH_BYTES * (2u + 2u * r->columns));
    charge_accesses(g, GLYPH_BYTES * (r->columns + (p.offset != 0 ? 1u : 0u)));
}

static void draw_glyph_from(GameState *g, Tiles t, Glyph glyph, Pen p, uint32_t first)
{
    unsigned columns = reaches_a_third_tile(p.offset) ? 3 : 2;
    const uint8_t *left = menu_font_bytes(glyph.left, GLYPH_BYTES), *right = menu_font_bytes(glyph.right, GLYPH_BYTES);
    const uint8_t *offsets = menu_text_column_offsets(t.sixteen_colour, p.column, columns);
    if (left && right && offsets) {
        Columns c = { { 0, 0, 0 } };
        for (unsigned k = 0; k < columns; k++)
            c.of[k] = (uint16_t)(offsets[2 * k] | offsets[2 * k + 1] << 8);
        GlyphRuns runs;
        if (glyph_runs(t, c, p, first, &runs)) {
            TIMED_AS(g, GLYPH_LINE, put_glyph_runs(g, &runs, left, right, glyph, p));
            return;
        }
        for (uint8_t byte = 0; byte < GLYPH_BYTES; byte++)
            TIMED_AS(g, GLYPH_LINE, draw_glyph_line_from(g, t, left, right, glyph, c, p, first, byte));
        return;
    }
    for (uint8_t byte = 0; byte < GLYPH_BYTES; byte++)
        TIMED_AS(g, GLYPH_LINE, draw_glyph_line(g, t, glyph, p, first, byte));
    /* Two-byte progress, one-byte count and three-byte destination. */
}

// $C2:5DC4 (the glyph and the pen worked out), $C2:5E36, $C2:5F07
/* Draw glyph `glyph` with the pen `pen_x` pixels along the line of text
 * whose first tile row is at `line`. */
void menu_put_glyph(GameState *g, uint16_t glyph, uint8_t pen_x, uint32_t line, MenuTextTiles kind)
{
    uint32_t first = (line & 0xFF0000u) | (uint16_t)(line + LINES_DOWN_THE_ROW);
    draw_glyph_from(g, tiles_of(kind), glyph_numbered(glyph), pen_at(pen_x), first);
}

