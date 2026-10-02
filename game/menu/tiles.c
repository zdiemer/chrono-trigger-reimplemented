/* Touches the menus' screens make to a tilemap they are building in work
 * RAM, from the drawing origin (menu_draw_origin): a character's large
 * portrait laid out and its colours loaded, and the palette and priority of
 * stretches of tiles changed, a row pair at a time or down a block of rows.
 * A tilemap is a word a tile, its number in the low byte and its attribute
 * (the palette in bits 2-4, priority, the ninth tile bit) in the high one,
 * $40 bytes a row. */
#include "menu/tiles.h"

#include "charge.h"
#include "menu/menu_data.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

enum { WORK_RAM = 0x7E0000, TILE_BYTES = 2, ROW_BYTES = 0x40, ATTRIBUTE = 1 };

static uint8_t attribute_at(const GameState *g, uint16_t at) { return wram_read8(g, WORK_RAM | (uint16_t)(at + ATTRIBUTE)); }
static void set_attribute_at(GameState *g, uint16_t at, uint8_t v) { wram_write8(g, WORK_RAM | (uint16_t)(at + ATTRIBUTE), v); }

/* ---- A character's large portrait ---------------------------------------------------------------- */

/* The portrait is 36 tiles in a block of six by six, numbered on from its
 * first a row after another, with the attribute its caller leaves in the
 * tile word's high byte. The word counts on past the last tile, into the
 * attribute where the numbers carry, and the sixteen colours go to the
 * palette the attribute names then. */
enum { PORTRAIT_SIDE = 6, PALETTE_BITS = 0x1C, PALETTE_BYTES_SHIFT = 3, PORTRAIT_COLOURS = 16, CHARACTER_SLOTS = 8 };
enum { PALETTE_BYTES = 2 };

// $C2:EC38
void menu_draw_portrait(GameState *g, uint16_t origin, uint8_t character)
{
    uint16_t tile = (uint16_t)((menu_scratch_portrait_tile(g) & 0xFF00) | menu_large_portrait_first_tile(g, character));
    for (unsigned row = 0; row < PORTRAIT_SIDE; row++)
        for (unsigned column = 0; column < PORTRAIT_SIDE; column++) {
            wram_write16(g, WORK_RAM | (uint16_t)(origin + row * ROW_BYTES + column * TILE_BYTES), tile);
            tile = (uint16_t)(tile + 1);
        }
    set_menu_scratch_portrait_tile(g, tile);
    uint8_t attribute = (uint8_t)(tile >> 8);
    unsigned first_colour = (unsigned)((attribute & PALETTE_BITS) << PALETTE_BYTES_SHIFT) / PALETTE_BYTES;
    uint16_t colours = menu_large_portrait_colours(g, character % CHARACTER_SLOTS);
    for (unsigned i = 0; i < PORTRAIT_COLOURS; i++)
        set_menu_palette(g, first_colour + i, menu_large_portrait_colour(g, colours, i));
    ORIGINAL_EFFECT(charge_block_move(g, PORTRAIT_COLOURS * PALETTE_BYTES));
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
}

/* ---- Recolouring tiles --------------------------------------------------------------------------- */

/* A stretch of tiles of a row and the row below it given one attribute. */
// $C2:ECAC
static void set_row_pair_attribute(GameState *g, uint16_t at, uint32_t tiles, uint8_t attribute)
{
    for (uint32_t i = 0; i < tiles; i++) {
        set_attribute_at(g, (uint16_t)(at + i * TILE_BYTES), attribute);
        set_attribute_at(g, (uint16_t)(at + ROW_BYTES + i * TILE_BYTES), attribute);
    }
}

/* A block of rows from `at` down, each a stretch of tiles given a palette
 * and priority (the attribute's top bits over its low four). The drawing
 * origin is left a row past the block, where the original walks it. */
enum { KEPT_BITS = 0x0F };

// $C2:EC93, $C2:ECC2
void menu_recolour_block(GameState *g, uint16_t at, uint32_t rows, uint32_t tiles, uint8_t colour)
{
    for (uint32_t row = 0; row < rows; row++, at = (uint16_t)(at + ROW_BYTES))
        for (uint32_t i = 0; i < tiles; i++) {
            uint16_t tile_at = (uint16_t)(at + i * TILE_BYTES);
            set_attribute_at(g, tile_at, (uint8_t)((attribute_at(g, tile_at) & KEPT_BITS) | colour));
        }
    set_menu_draw_origin(g, at);
}

/* ---- The entries --------------------------------------------------------------------------------- */

/* The counts are the accumulator's low byte (tiles) and X (rows), counted
 * down to zero in a 16-bit register, so 0 counts the whole bank. */
static uint32_t count_of(uint16_t n) { return n ? n : 0x10000u; }
static uint32_t tiles_asked(Snes *s) { return count_of((uint8_t)s->cpu.a); }

// $C2:EC38
void menu_draw_large_portrait(Snes *s)
{
    GameState *g = state_of(s);
    menu_draw_portrait(g, menu_draw_origin(g), menu_portrait_character(g));
    m_rts(s);
}

// $C2:ECAC
/* Its callers go on with the count of tiles in A's high byte. */
void menu_set_row_pair_attribute(Snes *s)
{
    GameState *g = state_of(s);
    set_row_pair_attribute(g, menu_draw_origin(g), tiles_asked(s), (uint8_t)(s->cpu.a >> 8));
    s->cpu.a = (uint16_t)(s->cpu.a << 8 | s->cpu.a >> 8);
    m_rts(s);
}

// $C2:EC93
void menu_recolour_rows(Snes *s)
{
    GameState *g = state_of(s);
    menu_recolour_block(g, menu_draw_origin(g), count_of(s->cpu.x), tiles_asked(s), (uint8_t)(s->cpu.a >> 8));
    m_rts(s);
}

/* ---- The guards ---------------------------------------------------------------------------------- */

/* They draw on the menus' page with the data bank on work RAM, into a
 * tilemap buffer past the low pages that doesn't run off the bank's end; the
 * portrait with the palette's changes short of the count's top, which the
 * original carries into the next byte. */
enum { LOW_PAGES_END = 0x2000, BANK_END = 0x10000 };

static bool on_the_menus_page(Snes *s) { return m_direct_page(s) == 0x0000 && s->cpu.db == 0x7E; }

static bool in_a_buffer(uint16_t at, uint32_t reach) { return at >= LOW_PAGES_END && (uint32_t)at + reach <= BANK_END; }

bool menu_large_portrait_is_drawable(Snes *s)
{
    if (!on_the_menus_page(s))
        return false;
    vars_peek(true);
    bool ok = in_a_buffer(menu_draw_origin(state_of(s)), (PORTRAIT_SIDE - 1) * ROW_BYTES + PORTRAIT_SIDE * TILE_BYTES)
              && menu_palette_changes_have_room(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_row_pair_is_colourable(Snes *s)
{
    if (!on_the_menus_page(s))
        return false;
    vars_peek(true);
    bool ok = in_a_buffer(menu_draw_origin(state_of(s)), ROW_BYTES + tiles_asked(s) * TILE_BYTES);
    vars_peek(false);
    return ok;
}

bool menu_rows_are_colourable(Snes *s)
{
    if (!on_the_menus_page(s))
        return false;
    vars_peek(true);
    uint32_t reach = (count_of(s->cpu.x) - 1) * ROW_BYTES + tiles_asked(s) * TILE_BYTES;
    bool ok = in_a_buffer(menu_draw_origin(state_of(s)), reach);
    vars_peek(false);
    return ok;
}
