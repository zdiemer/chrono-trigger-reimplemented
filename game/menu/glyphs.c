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
#include "original_effects.h"
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

/* ---- What the original leaves behind ----------------------------------------------------- */

/* The part of a line the original shifts into its own cell for the pen's
 * tile, when the line reaches a third tile (menu_glyph_spill). */
static void leave_spill(GameState *g, Pen p, uint16_t pixels)
{
    uint16_t spill = reaches_a_third_tile(p.offset) ? (uint16_t)(pixels >> (p.offset + TILE_WIDTH)) : 0;
    set_menu_glyph_spill(g, spill);
}

/* The drawer's own cells as its loops leave them: past the last byte, the
 * line pairs counted out, and the pointer a tile row and a line further on. */
static void leave_glyph_drawn(GameState *g, Tiles t, uint32_t first)
{
    set_menu_glyph_byte(g, GLYPH_BYTES);
    set_menu_glyph_lines_left(g, 0);
    set_menu_glyph_dest(g, glyph_byte_place(t, first, GLYPH_BYTES));
}

/* The cells the original's setter ($C2:5DC4) hands the drawer the glyph and
 * the pen in, and a step of its arithmetic. */
static void leave_glyph_set_up(GameState *g, uint16_t glyph, uint8_t pen_x)
{
    Glyph gl = glyph_numbered(glyph);
    set_menu_glyph_number(g, (uint8_t)glyph);
    set_menu_glyph_pen_offset(g, (uint16_t)(pen_x % TILE_WIDTH));
    set_menu_glyph_pen_column(g, (uint16_t)((pen_x & ~(TILE_WIDTH - 1)) >> 2));
    set_menu_glyph_font_step(g, (uint16_t)((glyph >> 1) * TILE_WIDTH));
    set_menu_glyph_left_pixels(g, gl.left);
    set_menu_glyph_right_pixels(g, gl.right);
}

/* ---- Drawing ----------------------------------------------------------------------------- */

static void draw_glyph_from(GameState *g, Tiles t, Glyph glyph, Pen p, uint32_t first)
{
    uint16_t pixels = 0;
    for (uint8_t byte = 0; byte < GLYPH_BYTES; byte++) {
        pixels = glyph_pixels(g, glyph, byte);
        put_pixels(g, t, glyph_byte_place(t, first, byte), p, pixels);
    }
    ORIGINAL_EFFECT(leave_spill(g, p, pixels));
    ORIGINAL_EFFECT(leave_glyph_drawn(g, t, first));
}

// $C2:5DC4 (the glyph and the pen worked out), $C2:5E36, $C2:5F07
/* Draw glyph `glyph` with the pen `pen_x` pixels along the line of text
 * whose first tile row is at `line`. */
void menu_put_glyph(GameState *g, uint16_t glyph, uint8_t pen_x, uint32_t line, MenuTextTiles kind)
{
    uint32_t first = (line & 0xFF0000u) | (uint16_t)(line + LINES_DOWN_THE_ROW);
    ORIGINAL_EFFECT(leave_glyph_set_up(g, glyph, pen_x));
    draw_glyph_from(g, tiles_of(kind), glyph_numbered(glyph), pen_at(pen_x), first);
}

/* ---- The entries ------------------------------------------------------------------------- */

/* The glyph and the pen the cells hold, as the original's setter left them. */
static Glyph glyph_in_cells(const GameState *g)
{
    return (Glyph){ menu_glyph_left_pixels(g), menu_glyph_right_pixels(g), (menu_glyph_number(g) & 1) != 0 };
}

static Pen pen_in_cells(const GameState *g)
{
    return (Pen){ (uint8_t)(menu_glyph_pen_column(g) >> 1), (uint8_t)menu_glyph_pen_offset(g) };
}

// $C2:5E89
/* Draw the byte of the glyph the drawer is on, at the place the glyph page
 * points at. */
static void draw_glyph_line(GameState *g)
{
    Glyph glyph = glyph_in_cells(g);
    Pen p = pen_in_cells(g);
    uint16_t pixels = glyph_pixels(g, glyph, menu_glyph_byte(g));
    put_pixels(g, FOUR_COLOUR, menu_glyph_dest(g), p, pixels);
    ORIGINAL_EFFECT(leave_spill(g, p, pixels));
}

enum { GLYPH_PAGE = 0x0200, FONT_TABLES_BANK = 0xC2 };

/* The glyph drawer's writes land in the text's tiles, which the text engine
 * keeps well clear of the glyph page (the drawer reads its cells a line at a
 * time, so a glyph drawn over them would read what it drew): its reach from
 * the pointer is under $400 bytes in four-colour tiles, $800 in sixteen. */
static bool clear_of_the_glyph_page(uint32_t dest, Tiles t)
{
    uint16_t reach = (uint16_t)(4 * t.row_bytes);
    uint8_t bank = (uint8_t)(dest >> 16);
    uint16_t at = (uint16_t)dest;
    if (at > 0x10000 - reach)
        return false;
    bool low_ram = bank == 0x7E || m_io_bank(bank);
    return !low_ram || at >= GLYPH_PAGE + 0x100 || at + reach <= GLYPH_PAGE;
}

/* The game enters the drawer on the glyph page, with the pen somewhere a
 * glyph's lines keep inside the line of text (the pen's X is under 256, and
 * its column the one table entry it names), and the glyph's pixels bound for
 * the text's tiles. */
static bool glyph_is_one_the_game_draws(Snes *s, Tiles t)
{
    if (m_direct_page(s) != GLYPH_PAGE)
        return false;
    GameState *g = state_of(s);
    vars_peek(true);
    uint16_t column = menu_glyph_pen_column(g);
    uint8_t offset = (uint8_t)menu_glyph_pen_offset(g);   /* the drawer masks the word it is kept in */
    uint32_t dest = menu_glyph_dest(g);
    vars_peek(false);
    unsigned reaches = (column >> 1) + (reaches_a_third_tile(offset) ? 3u : 2u);
    return offset < TILE_WIDTH && (column & 1) == 0 && reaches <= MENU_TEXT_COLUMNS && clear_of_the_glyph_page(dest, t);
}

bool menu_glyph_is_drawable(Snes *s) { return glyph_is_one_the_game_draws(s, FOUR_COLOUR); }
bool menu_glyph_16_colour_is_drawable(Snes *s) { return glyph_is_one_the_game_draws(s, SIXTEEN_COLOUR); }

/* The line entry also reads the column table through the data bank, which
 * the drawer sets to the table's bank, and is called for one of the glyph's
 * 24 bytes. */
static bool on_a_byte_of_the_glyph(Snes *s)
{
    vars_peek(true);
    bool ok = menu_glyph_byte(state_of(s)) < GLYPH_BYTES;
    vars_peek(false);
    return ok;
}

bool menu_glyph_line_is_drawable(Snes *s)
{
    return s->cpu.db == FONT_TABLES_BANK && glyph_is_one_the_game_draws(s, FOUR_COLOUR) && on_a_byte_of_the_glyph(s);
}

/* All three leave Y at the pen's own tile's offset, the last they looked up. */
static void leave_y(Snes *s, Tiles t)
{
    GameState *g = state_of(s);
    vars_peek(true);
    unsigned column = pen_in_cells(g).column;
    s->cpu.y = t.sixteen_colour ? menu_text_column_offset_16_colour(g, column) : menu_text_column_offset(g, column);
    vars_peek(false);
}

void menu_draw_glyph_line(Snes *s)
{
    draw_glyph_line(state_of(s));
    ORIGINAL_EFFECT(leave_y(s, FOUR_COLOUR));
    m_rts(s);
}

static void draw_glyph_in_cells(Snes *s, Tiles t)
{
    GameState *g = state_of(s);
    draw_glyph_from(g, t, glyph_in_cells(g), pen_in_cells(g), menu_glyph_dest(g));
    ORIGINAL_EFFECT(leave_y(s, t));
    m_rts(s);
}

// $C2:5E36
void menu_draw_glyph(Snes *s) { draw_glyph_in_cells(s, FOUR_COLOUR); }

// $C2:5F07
void menu_draw_glyph_16_colour(Snes *s) { draw_glyph_in_cells(s, SIXTEEN_COLOUR); }
