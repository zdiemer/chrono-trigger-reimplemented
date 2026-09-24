/* The dialog and the menus: drawing a glyph of the proportional font into the
 * text's tiles.
 *
 * Text is drawn a glyph at a time into a buffer of 2-bit tiles, which the
 * text engine then sends to VRAM. A glyph is 12 pixels wide and 12 lines tall
 * (game/menu/menu_data.h, the font), and a pen moves along the line of text
 * by each glyph's width, so a glyph starts anywhere in a tile: its 12 pixels
 * reach into the next tile always and into a third when the pen is five or
 * more pixels into its own. What is drawn in the pen's tile is added to what
 * the glyph before left there, except at the tile's left edge, where the tile
 * is new; the tiles to its right are the glyph's alone.
 *
 * The text engine ($C2:5DC4) sets the pen and the glyph up in the glyph
 * page's cells -- where the pen is, which glyph, where its pixels are in the
 * font and where in the buffer the pen's tile row is -- and calls the drawer,
 * which draws the glyph a byte at a time: its lines' two bitplanes, top to
 * bottom. The glyph sits four lines down its tile row, so its first four
 * lines go in the bottom half of the row's tiles and the other eight in the
 * row below. */
#include "menu/menu.h"

#include "menu/menu_data.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* A glyph: 12 lines of two bitplanes, a byte each. */
enum { GLYPH_BYTES = 24, TILE_WIDTH = 8 };

/* The text's tiles are 16 bytes each, 16 to a row of tiles; the glyph's top
 * four lines (its first 8 bytes) go in the bottom half of the pen's row, and
 * the rest in the row below. */
enum { BYTES_IN_THE_PEN_ROW = 8, TILE_ROW_BYTES = 0x100, TILE_BYTES = 16 };

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

static Glyph glyph_being_drawn(const GameState *g)
{
    return (Glyph){ menu_glyph_left_pixels(g), menu_glyph_right_pixels(g), (menu_glyph_number(g) & 1) != 0 };
}

static Pen pen(const GameState *g)
{
    return (Pen){ (uint8_t)(menu_glyph_pen_column(g) >> 1), menu_glyph_pen_offset(g) };
}

/* A line of the glyph in one bitplane (`byte` of its 24), its 12 pixels in
 * the top of 16 bits. */
static uint16_t glyph_pixels(const GameState *g, Glyph glyph, uint8_t byte)
{
    uint8_t left = menu_font_byte(g, glyph.left + byte);
    uint8_t shared = menu_font_byte(g, glyph.right + byte);
    uint8_t right = glyph.odd ? (uint8_t)(shared << 4) : (uint8_t)(shared & 0xF0);
    return (uint16_t)(left << 8 | right);
}

/* A line reaches a third tile when the pen is far enough into its own that
 * 12 pixels don't fit in two. */
static bool reaches_a_third_tile(uint8_t pen_offset) { return pen_offset + 12 > 2 * TILE_WIDTH; }

/* Where `column`'s tile has the line at `at` (a place in the pen's tile). */
static uint32_t in_column(const GameState *g, uint32_t at, unsigned column)
{
    return (at + menu_text_column_offset(g, column)) & 0xFFFFFFu;
}

/* Put a line of pixels at the pen, `at` being the line's byte in the pen's
 * tile: shifted along by the pen's offset, the pixels fill the tiles from the
 * right, the pen's own last, where they join what is there already. */
static void put_pixels(GameState *g, uint32_t at, Pen p, uint16_t pixels)
{
    uint32_t across = (uint32_t)pixels << TILE_WIDTH >> p.offset;   /* the three tiles' bytes, the pen's first */
    if (reaches_a_third_tile(p.offset))
        wram_write8(g, in_column(g, at, p.column + 2u), (uint8_t)across);
    wram_write8(g, in_column(g, at, p.column + 1u), (uint8_t)(across >> 8));
    uint32_t own = in_column(g, at, p.column);
    uint8_t mine = (uint8_t)(across >> 16);
    wram_write8(g, own, p.offset == 0 ? mine : (uint8_t)(mine | wram_read8(g, own)));
}

/* Where the glyph's byte goes, from where its first four lines start. */
static uint32_t glyph_byte_place(uint32_t first, uint8_t byte)
{
    uint32_t down = byte < BYTES_IN_THE_PEN_ROW ? byte : (uint32_t)(TILE_ROW_BYTES - TILE_BYTES + byte);
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
static void leave_glyph_drawn(GameState *g, uint32_t first)
{
    set_menu_glyph_byte(g, GLYPH_BYTES);
    set_menu_glyph_lines_left(g, 0);
    set_menu_glyph_dest(g, glyph_byte_place(first, GLYPH_BYTES));
}

// $C2:5E89
/* Draw the byte of the glyph the drawer is on, at the place the glyph page
 * points at. */
static void draw_glyph_line(GameState *g)
{
    Glyph glyph = glyph_being_drawn(g);
    Pen p = pen(g);
    uint8_t byte = (uint8_t)menu_glyph_byte(g);
    uint16_t pixels = glyph_pixels(g, glyph, byte);
    put_pixels(g, menu_glyph_dest(g), p, pixels);
    ORIGINAL_EFFECT(leave_spill(g, p, pixels));
}

// $C2:5E36
/* Draw the whole glyph at the pen. */
static void draw_glyph(GameState *g)
{
    Glyph glyph = glyph_being_drawn(g);
    Pen p = pen(g);
    uint32_t first = menu_glyph_dest(g);
    uint16_t pixels = 0;
    for (uint8_t byte = 0; byte < GLYPH_BYTES; byte++) {
        pixels = glyph_pixels(g, glyph, byte);
        put_pixels(g, glyph_byte_place(first, byte), p, pixels);
    }
    ORIGINAL_EFFECT(leave_spill(g, p, pixels));
    ORIGINAL_EFFECT(leave_glyph_drawn(g, first));
}

/* ---- The entries ------------------------------------------------------------------------- */

enum { GLYPH_PAGE = 0x0200, FONT_TABLES_BANK = 0xC2 };

/* The glyph drawer's writes land in the text's tiles, which the text engine
 * keeps well clear of the glyph page (the drawer reads its cells a line at a
 * time, so a glyph drawn over them would read what it drew): its reach from
 * the pointer is under $400 bytes. */
enum { GLYPH_REACH = 0x400 };

static bool clear_of_the_glyph_page(uint32_t dest)
{
    uint8_t bank = (uint8_t)(dest >> 16);
    uint16_t at = (uint16_t)dest;
    if (at > 0x10000 - GLYPH_REACH)
        return false;
    bool low_ram = bank == 0x7E || m_io_bank(bank);
    return !low_ram || at >= GLYPH_PAGE + 0x100 || at + GLYPH_REACH <= GLYPH_PAGE;
}

/* The game enters the drawer on the glyph page, with the pen somewhere a
 * glyph's lines keep inside the line of text (the pen's X is under 256, and
 * its column the one table entry it names), and the glyph's pixels bound for
 * the text's tiles. */
static bool glyph_is_one_the_game_draws(Snes *s)
{
    if (m_direct_page(s) != GLYPH_PAGE)
        return false;
    GameState *g = state_of(s);
    vars_peek(true);
    uint16_t column = menu_glyph_pen_column(g);
    uint8_t offset = menu_glyph_pen_offset(g);
    uint32_t dest = menu_glyph_dest(g);
    vars_peek(false);
    unsigned reaches = (column >> 1) + (reaches_a_third_tile(offset) ? 3u : 2u);
    return offset < TILE_WIDTH && (column & 1) == 0 && reaches <= MENU_TEXT_COLUMNS
           && clear_of_the_glyph_page(dest);
}

bool menu_glyph_is_drawable(Snes *s) { return glyph_is_one_the_game_draws(s); }

/* The line entry also reads the column table through the data bank, which
 * the drawer sets to the table's bank. */
bool menu_glyph_line_is_drawable(Snes *s) { return s->cpu.db == FONT_TABLES_BANK && glyph_is_one_the_game_draws(s); }

/* Both leave Y at the pen's own tile's offset, the last they looked up. */
static void leave_y(Snes *s)
{
    GameState *g = state_of(s);
    vars_peek(true);
    s->cpu.y = menu_text_column_offset(g, pen(g).column);
    vars_peek(false);
}

void menu_draw_glyph_line(Snes *s)
{
    draw_glyph_line(state_of(s));
    ORIGINAL_EFFECT(leave_y(s));
    m_rts(s);
}

void menu_draw_glyph(Snes *s)
{
    draw_glyph(state_of(s));
    ORIGINAL_EFFECT(leave_y(s));
    m_rts(s);
}
