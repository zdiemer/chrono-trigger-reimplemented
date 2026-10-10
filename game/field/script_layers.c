/* Field mode: the work on the layers scripts leave the main loop.
 *
 * Three things, looked at once a frame after the screen effect's step:
 * - BG2's left or right half cleared: the layers' staging blanked, and the
 *   vblank asked to send it to that half of BG2's tile map.
 * - The credits the endings roll: texts from the ROM written into BG3's tile
 *   map, a line a frame. A script picks the text and the place its first line
 *   goes (events $29 and $2F) and starts the writing (event $32); each line
 *   is laid out in BG3's staged row and the vblank sends it to its row of the
 *   tile map, from the line's column to the row's end. The text's end stops
 *   the writing and leaves a row blank after it.
 * - The message the field opened for a chest closed once the chest has
 *   scrolled off the screen.
 * The layer work comes first, a piece a frame; the message is looked at only
 * on frames without it. */
#include "field/script_layers.h"

#include "cpu_io.h"
#include "field/bridge.h"
#include "field/dialog_window.h"
#include "field/field.h"
#include "field/field_data.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The credits -------------------------------------------------------------------------------- */

/* A credits text's bytes: letters, a line's end, the text's end. */
enum { TEXT_ENDS = 0x80, LINE_ENDS = '/', FULL_STOP = '.', SPACE = ' ' };

/* The tiles the credits' letters are, in the font event $29 loads into BG3's
 * tiles: a letter's code less $40 (A is tile 1), a full stop tile $1B, a
 * space the blank tile 0; on palette 3, in front. */
enum { LETTER_TILES = 0x40, FULL_STOP_TILE = 0x1B, BLANK_TILE = 0 };
enum { CREDITS_ATTRIBUTES = 0x2C };

static uint8_t credits_tile(uint8_t c)
{
    if (c == SPACE)
        return BLANK_TILE;
    if (c == FULL_STOP)
        return FULL_STOP_TILE;
    return (uint8_t)(c - LETTER_TILES);
}

static bool ends_text(uint8_t c) { return (c & TEXT_ENDS) != 0; }

// $C0:3E84 (the search)
uint16_t credits_text_start(const GameState *g, uint8_t text)
{
    uint16_t at = 0;
    for (uint8_t passed = 0; passed != text; passed++) {
        while (!ends_text(field_credits_byte(g, at)))
            at++;
        at++;
    }
    return at;
}

void credits_start_at(GameState *g, uint8_t column, uint8_t row)
{
    set_field_credits_column(g, column);
    set_field_credits_row(g, row);
}

/* BG3's tile map in VRAM, a row of 32 entries of two bytes; the credits keep
 * to its first 32 columns and 64 rows. */
enum { CREDITS_MAP_VRAM = 0x7000, MAP_COLUMNS = 32, ENTRY_BYTES = 2, COLUMN_BITS = 0x1F, ROW_BITS = 0x3F };

// $C0:27EB
/* The line laid out goes from its column to the row's end, and the next
 * starts a row down. */
static void send_the_line(GameState *g, uint16_t next)
{
    set_field_credits_cursor(g, next);
    uint8_t column = field_credits_column(g) & COLUMN_BITS, row = field_credits_row(g) & ROW_BITS;
    set_field_bg3_piece_vram(g, (uint16_t)(CREDITS_MAP_VRAM + row * MAP_COLUMNS + column));
    set_field_bg3_piece_bytes(g, (uint16_t)((MAP_COLUMNS - column) * ENTRY_BYTES));
    set_field_credits_row(g, (uint8_t)(field_credits_row(g) + 1));
    field_send_maps(g, FIELD_SEND_BG3_PIECE);
}

static void blank_the_staged_row(GameState *g)
{
    enum { ROW_BYTES = MAP_COLUMNS * ENTRY_BYTES };
    for (unsigned i = 0; i < ROW_BYTES; i++)
        set_field_layer_staging_byte(g, FIELD_BG3, FIELD_STAGED_ROW + i,
                                     field_blank_entries_byte(g, i % FIELD_BLANK_ENTRIES_BYTES));
}

/* For the original's code and shadow mode: it lays the line out through the
 * work RAM port, which it leaves past the line's last entry, and works the
 * line's place out from its column, which it leaves in the field page's
 * scratch. */
static void leave_the_line(GameState *g, unsigned entries)
{
    hw_port_seek(g, (uint16_t)(field_staged_line(FIELD_BG3, false) + entries * ENTRY_BYTES), 0);
    original_cell16(g, FIELD_LAST_WORKED_CELL, field_credits_column(g) & COLUMN_BITS);
}

// $C0:278F
/* A line of the credits: BG3's staged row blanked, the line's letters laid
 * out in it, and the line sent. At the text's end the writing stops, and the
 * next text starts a row further down. */
static void write_a_credits_line(GameState *g)
{
    blank_the_staged_row(g);
    uint16_t at = field_credits_cursor(g);
    unsigned entries = 0;
    for (;;) {
        uint8_t c = field_credits_byte(g, at);
        if (ends_text(c)) {
            field_mode_flag(g, FIELD_WRITING_CREDITS, false);
            send_the_line(g, (uint16_t)(at + 1));
            set_field_credits_row(g, (uint8_t)(field_credits_row(g) + 1));
            break;
        }
        if (c == LINE_ENDS) {
            send_the_line(g, (uint16_t)(at + 1));
            break;
        }
        set_field_layer_staging_byte(g, FIELD_BG3, FIELD_STAGED_ROW + entries * ENTRY_BYTES, credits_tile(c));
        set_field_layer_staging_byte(g, FIELD_BG3, FIELD_STAGED_ROW + entries * ENTRY_BYTES + 1, CREDITS_ATTRIBUTES);
        entries++;
        at++;
    }
    ORIGINAL_EFFECT(leave_the_line(g, entries));
}

/* A line the staging holds: one ending within BG3's staged lines. */
enum { LONGEST_LINE = RAM_FIELD_LAYER_STAGING_STRIDE / ENTRY_BYTES };

static bool credits_line_fits(const GameState *g)
{
    uint16_t at = field_credits_cursor(g);
    for (unsigned n = 0; n < LONGEST_LINE; n++, at++) {
        uint8_t c = field_credits_byte(g, at);
        if (ends_text(c) || c == LINE_ENDS)
            return true;
    }
    return false;
}

/* ---- BG2's halves ------------------------------------------------------------------------------ */

enum { BG2_HALVES = FIELD_CLEAR_BG2_LEFT | FIELD_CLEAR_BG2_RIGHT };

void script_asks_for_layer_work(GameState *g, uint8_t work) { field_mode_flag(g, work, true); }

/* The halves asked for go to BG2 in the vblank: each mode flag's bit is its
 * send's, a bit down. */
static void send_bg2_halves(GameState *g)
{
    field_send_maps(g, (uint8_t)((field_mode_flags(g) & BG2_HALVES) << 1));
}

static void bg2_halves_cleared(GameState *g) { field_mode_flag(g, BG2_HALVES, false); }

/* ---- The chest's message ------------------------------------------------------------------------- */

/* Whether the chest the leader opened is on the screen: right of its left
 * edge, up to its right one, below its top and down to its bottom, in map
 * tiles (the edges are in 8-pixel tiles). */
static bool chest_on_screen(const GameState *g)
{
    uint16_t chest = field_opened_chest_tile(g);
    uint8_t column = (uint8_t)chest, row = (uint8_t)(chest >> 8);
    return field_screen_left(g) / FIELD_ENTRIES_A_MAP_TILE < column &&
           field_screen_right(g) / FIELD_ENTRIES_A_MAP_TILE >= column &&
           field_screen_top(g) / FIELD_ENTRIES_A_MAP_TILE < row && field_screen_bottom(g) / FIELD_ENTRIES_A_MAP_TILE >= row;
}

static void close_the_chests_message(GameState *g)
{
    if (!chest_on_screen(g))
        dialog_end(g);
}

/* ---- The frame's turn --------------------------------------------------------------------------- */

typedef enum { NO_WORK, WROTE_A_LINE, CLEARS_BG2 } LayerWork;

// $C0:274D
static LayerWork do_the_layer_work(GameState *g)
{
    uint8_t flags = field_mode_flags(g);
    if (flags & FIELD_WRITING_CREDITS) {
        write_a_credits_line(g);
        return WROTE_A_LINE;
    }
    if (flags & BG2_HALVES) {
        send_bg2_halves(g);
        return CLEARS_BG2;
    }
    if (flags & FIELD_DIALOG_OPENED_BY_THE_FIELD)
        close_the_chests_message(g);
    return NO_WORK;
}

/* ---- Where it meets the original -----------------------------------------------------------------
 *
 * BG2's halves are cleared from the layers' staging, which the original's
 * routine blanks ($C0:75A0); the port calls it as the original does. */
enum { BLANK_THE_STAGING = 0xC075A0, READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* On the field's page, with 8-bit A and 16-bit index registers. A, X, Y and
 * the flags but V are dead after it; a credits line leaves V clear (the
 * sum its send's size is worked out with never overflows), the staging's
 * blank as it leaves V, and the rest as it came. */
void field_do_script_layer_work(Snes *s)
{
    GameState *g = state_of(s);
    switch (do_the_layer_work(g)) {
    case WROTE_A_LINE: m_set_flag(s, CPU_FLAG_V, false); break;
    case CLEARS_BG2:
        if (!bridge_call(s, 0xC02787, BLANK_THE_STAGING, READS_NOTHING, on_the_field_page(s, false)))
            return;
        bg2_halves_cleared(g);
        break;
    default: break;
    }
    m_rts(s);
}

/* The guard: on the field's page with the data bank $00, and a credits line
 * to write ending before it would run past BG3's staging. */
bool field_script_layer_work_can_run(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = !(field_mode_flags(g) & FIELD_WRITING_CREDITS) || credits_line_fits(g);
    vars_peek(false);
    return ok;
}
