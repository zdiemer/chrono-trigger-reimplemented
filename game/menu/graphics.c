/* The menus' graphics: the tile sets every menu screen draws with, built in
 * work RAM from the ROM's and sent to VRAM as the menus open ($C2:968D), and
 * the window design's tiles, which the settings screen changes too
 * ($C2:C511).
 *
 * Most of the ROM's graphics are stored with fewer bitplanes than the tiles
 * they end up in, and the menus make up the rest as they build them: four-
 * colour tiles laid out as sixteen-colour ones, three-plane tiles given a
 * fourth, and extra planes made from the ink of the ones there (a pixel set
 * in any plane), which puts what is drawn in a colour of its own. The sets
 * are built in the buffer at $7E:2E00 and around it, each sent with the
 * upload queue before the next is built; the screen is blanked while the
 * menus open. The menus' colours from the ROM go into the working palette,
 * which is sent at the end, and the window design the player chose is loaded
 * last. */
#include "menu/graphics.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/display.h"
#include "menu/menu_data.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "port/native.h"
#include "vars.h"
#include "wram.h"

/* ---- Building tiles ------------------------------------------------------------------------ */

/* A sixteen-colour tile: 32 bytes, eight rows of its first two planes (a
 * byte each) and then eight of its last two. A four-colour tile is the first
 * half alone; a three-plane one in the ROM is that and a byte a row of the
 * third plane. */
enum { TILE_ROWS = 8, FOUR_COLOUR_TILE = 16, SIXTEEN_COLOUR_TILE = 32, LAST_PLANES = 16, THREE_PLANE_TILE = 24 };

static uint32_t in_work_ram(uint16_t at) { return 0x7E0000u | at; }

static uint8_t tile_byte(const GameState *g, uint16_t at) { return wram_read8(g, in_work_ram(at)); }
static void put_tile_byte(GameState *g, uint16_t at, uint8_t v) { wram_write8(g, in_work_ram(at), v); }

/* A block moved into work RAM, from the ROM or from elsewhere in it, and the
 * block move's time. */
static void move_block(GameState *g, uint32_t from, uint16_t to, uint32_t bytes)
{
    wram_copy(g, from, in_work_ram(to), bytes);
    ORIGINAL_EFFECT(charge_block_move(g, bytes));
}

static void move_in_work_ram(GameState *g, uint16_t from, uint16_t to, uint32_t bytes) { move_block(g, in_work_ram(from), to, bytes); }
static void move_from_rom(GameState *g, uint16_t from, uint16_t to, uint32_t bytes) { move_block(g, MENU_ROM_BANK | from, to, bytes); }

/* The counts the loops that build a tile's planes keep in the menus' first
 * cells, which they leave at 0: the widening's rows and tiles a word each,
 * the inking's rows a byte. */
static void leave_counted_out(GameState *g)
{
    set_menu_scratch_graphics_count(g, 0);
    set_menu_scratch_graphics_tiles(g, 0);
}

static void leave_rows_counted_out(GameState *g) { set_menu_scratch_graphics_rows(g, 0); }

// $C2:96B2
/* Four-colour tiles laid out as sixteen-colour ones: each tile's 16 bytes the
 * first half of a tile twice its size, the second half left as it was. */
static void widen_to_sixteen_colours(GameState *g, uint16_t from, uint16_t to, unsigned tiles)
{
    for (unsigned t = 0; t < tiles; t++)
        move_in_work_ram(g, (uint16_t)(from + t * FOUR_COLOUR_TILE), (uint16_t)(to + t * SIXTEEN_COLOUR_TILE),
                         FOUR_COLOUR_TILE);
}

// $C2:960B
/* Three-plane tiles from the ROM made sixteen-colour ones: their first two
 * planes as they are, the third a byte a row with a clear fourth beside it. */
static void widen_three_plane_tiles(GameState *g, uint16_t from, uint16_t to, unsigned tiles)
{
    for (unsigned t = 0; t < tiles; t++) {
        uint16_t tile = (uint16_t)(from + t * THREE_PLANE_TILE);
        uint16_t into = (uint16_t)(to + t * SIXTEEN_COLOUR_TILE);
        move_from_rom(g, tile, into, FOUR_COLOUR_TILE);
        for (unsigned row = 0; row < TILE_ROWS; row++) {
            uint8_t third = menu_rom_graphic_byte(g, MENU_ROM_BANK | (uint16_t)(tile + FOUR_COLOUR_TILE + row));
            put_tile_byte(g, (uint16_t)(into + LAST_PLANES + 2 * row), third);
            put_tile_byte(g, (uint16_t)(into + LAST_PLANES + 2 * row + 1), 0);
        }
    }
    ORIGINAL_EFFECT(leave_counted_out(g));
}

/* A row's ink in its first two planes. The loops that make the planes count
 * in the menus' first cells, which they leave at 0. */
static uint8_t ink_of_two(const GameState *g, uint16_t row) { return (uint8_t)(tile_byte(g, row) | tile_byte(g, (uint16_t)(row + 1))); }

// $C2:962E
/* Each row's fourth plane set to the ink of its first three. */
static void ink_the_fourth_plane(GameState *g, uint16_t at, unsigned tiles)
{
    for (unsigned t = 0; t < tiles; t++)
        for (unsigned r = 0; r < TILE_ROWS; r++) {
            uint16_t row = (uint16_t)(at + t * SIXTEEN_COLOUR_TILE + 2 * r);
            put_tile_byte(g, (uint16_t)(row + LAST_PLANES + 1),
                          (uint8_t)(ink_of_two(g, row) | tile_byte(g, (uint16_t)(row + LAST_PLANES))));
        }
    ORIGINAL_EFFECT(leave_rows_counted_out(g));
}

// $C2:9653
/* Each row's third and fourth planes both set to the ink of its first two. */
static void ink_the_last_two_planes(GameState *g, uint16_t at, unsigned tiles)
{
    for (unsigned t = 0; t < tiles; t++)
        for (unsigned r = 0; r < TILE_ROWS; r++) {
            uint16_t row = (uint16_t)(at + t * SIXTEEN_COLOUR_TILE + 2 * r);
            uint8_t ink = ink_of_two(g, row);
            put_tile_byte(g, (uint16_t)(row + LAST_PLANES), ink);
            put_tile_byte(g, (uint16_t)(row + LAST_PLANES + 1), ink);
        }
    ORIGINAL_EFFECT(leave_rows_counted_out(g));
}

// $C2:9678
/* A tile's third plane set to the ink of its first two, its fourth cleared. */
static void ink_the_third_plane(GameState *g, uint16_t tile)
{
    for (unsigned r = 0; r < TILE_ROWS; r++) {
        uint16_t row = (uint16_t)(tile + 2 * r);
        put_tile_byte(g, (uint16_t)(row + LAST_PLANES), ink_of_two(g, row));
        put_tile_byte(g, (uint16_t)(row + LAST_PLANES + 1), 0);
    }
}

/* ---- Sending them ---------------------------------------------------------------------------- */

/* A set's upload records (bank C2, menu/menu_data.h) queued, and the queue
 * sent at once. */
static void queue_records(GameState *g, uint16_t first, unsigned count)
{
    for (unsigned i = 0; i < count; i++)
        menu_queue_upload(g, menu_upload_record(g, (uint16_t)(first + i * MENU_UPLOAD_BYTES)));
}

static void send_now(GameState *g, Platform *pf) { TIMED_AS(g, 0xC28663, menu_send_upload_queue(g, pf)); }

/* A queue written whole over the queue's start, as the original's block move
 * of the records does; the first also writes its count a word wide. */
static void queue_in_place_of(GameState *g, uint16_t first, unsigned count)
{
    set_menu_vram_queue_bytes(g, 0);
    queue_records(g, first, count);
    ORIGINAL_EFFECT(charge_block_move(g, count * MENU_UPLOAD_BYTES));
}

/* ---- The sets ------------------------------------------------------------------------------ */

/* Where the sets are built. */
enum {
    BUFFER = 0x2E00, WIDENED_AT = 0x3000, WIDENED_FROM = 0x4F00, WIDENED_TILES = 0xF0, FOUR_COLOUR_AT = 0x4E00,
    FIRST_TILES_AT = 0x5000, REPLACED_AT = 0x5100, INKED_TILE = 0x33E0, INKED_TILES_FROM = 0x4880,
    INKED_TILES_TO = 0x49C0, THREE_PLANE_AT = 0x5400, INKED_THREE_PLANE_TILES = 8, SECOND_SET_BYTES = 0x12A0,
    DESIGN_TILES_PLACE = BUFFER + MENU_WINDOW_DESIGNS * MENU_WINDOW_DESIGN_BYTES, DESIGN_TILES_FROM = 0x5F00, DESIGN_TILES_BYTES = 0x1A0, INKED_WINDOW_TILES = 0xA4,
};

// $C2:968D-$C2:96EB
/* The first sets: four-colour tiles laid out as sixteen-colour ones, nine
 * with their ink in the third plane, and the four-colour tiles as they are. */
static void build_first_sets(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC29542, menu_copy_rom_to_memory(g, pf, MENU_ROM_FOUR_COLOUR_TILES, FIRST_TILES_AT,
                                                   MENU_ROM_FOUR_COLOUR_TILES_BYTES));
    TIMED_AS(g, 0xC29542, menu_copy_rom_to_memory(g, pf, MENU_ROM_REPLACED_TILES, REPLACED_AT,
                                                   MENU_ROM_REPLACED_TILES_BYTES));
    widen_to_sixteen_colours(g, WIDENED_FROM, WIDENED_AT, WIDENED_TILES);
    ink_the_third_plane(g, INKED_TILE);
    for (uint16_t tile = INKED_TILES_FROM; tile < INKED_TILES_TO; tile = (uint16_t)(tile + SIXTEEN_COLOUR_TILE))
        ink_the_third_plane(g, tile);
    queue_in_place_of(g, MENU_UPLOADS_FIRST_SETS, 2);
    set_menu_vram_queue_bytes_high(g, 0);   /* the count written a word wide */
    send_now(g, pf);
}

// $C2:96EE-$C2:9721
/* The second set: three-plane tiles made sixteen-colour, the first eight
 * inked in their fourth plane, behind more four-colour tiles. */
static void build_second_set(GameState *g, Platform *pf)
{
    move_from_rom(g, MENU_ROM_MORE_FOUR_COLOUR_TILES, FOUR_COLOUR_AT, MENU_ROM_MORE_FOUR_COLOUR_TILES_BYTES);
    widen_three_plane_tiles(g, MENU_ROM_THREE_PLANE_TILES, THREE_PLANE_AT, MENU_THREE_PLANE_TILES);
    ink_the_fourth_plane(g, THREE_PLANE_AT, INKED_THREE_PLANE_TILES);
    move_in_work_ram(g, FOUR_COLOUR_AT, BUFFER, SECOND_SET_BYTES);
    TIMED_AS(g, 0xC28385, queue_records(g, MENU_UPLOAD_SECOND_SET, 1));
    send_now(g, pf);
}

// $C2:9724-$C2:9758
/* The windows' sets: the eight designs and the tiles beside them, inked in
 * their fourth plane, sent with two sets straight from the ROM. */
static void build_window_sets(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC29542, menu_copy_rom_to_memory(g, pf, MENU_ROM_WINDOW_DESIGNS, BUFFER,
                                                   MENU_WINDOW_DESIGNS * MENU_WINDOW_DESIGN_BYTES));
    move_in_work_ram(g, DESIGN_TILES_FROM, DESIGN_TILES_PLACE, DESIGN_TILES_BYTES);
    ink_the_fourth_plane(g, BUFFER, INKED_WINDOW_TILES);
    queue_in_place_of(g, MENU_UPLOADS_WINDOW_SETS, 3);
    send_now(g, pf);
}

/* The menus' colours from the ROM, into the working palette. */
static void load_colours(GameState *g, uint16_t from, unsigned first, unsigned count)
{
    for (unsigned i = 0; i < count; i++)
        set_menu_palette(g, first + i, menu_rom_colour(g, from, i));
    ORIGINAL_EFFECT(charge_block_move(g, 2 * count));
}

// $C2:975D-$C2:9798
enum { ROW_7 = 0x70, ROW_8 = 0x80, COLOURS_A_ROW = 16, A_FEW = 4 };

static void load_menu_colours(GameState *g)
{
    load_colours(g, MENU_ROM_FIRST_COLOURS, 0, A_FEW);
    load_colours(g, MENU_ROM_FIRST_COLOURS, A_FEW, A_FEW);
    load_colours(g, MENU_ROM_SECOND_COLOURS, COLOURS_A_ROW, A_FEW);
    load_colours(g, MENU_ROM_THIRD_COLOURS, COLOURS_A_ROW + A_FEW, A_FEW);
    load_colours(g, MENU_ROM_ROW_7_COLOURS, ROW_7, COLOURS_A_ROW);
}

// $C2:979B-$C2:9816
/* The last set: tiles moved into place over a cleared buffer, the first 32
 * inked in their fourth plane, and four sixteen-colour tiles from the ROM
 * with their ink in both their last planes. */
enum { LAST_SET_CLEARED = 0x1000, MOVED_FROM = 0x5C00, MOVED_TO = 0x2E80, SECOND_MOVED_TO = 0x3080, MOVED_BYTES = 0x180 };
enum { SPREAD_TO = 0x3200, SPREAD_BYTES = 0x400, INKED_LAST_TILES = 0x20, ROM_TILE_PAIR = 0x40, ROWS_APART = 0x200 };

static void build_last_set(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC29512, menu_clear_memory(g, pf, BUFFER, LAST_SET_CLEARED));
    move_in_work_ram(g, MOVED_FROM, MOVED_TO, MOVED_BYTES);
    move_in_work_ram(g, (uint16_t)(MOVED_FROM + MOVED_BYTES), SECOND_MOVED_TO, MOVED_BYTES);
    move_in_work_ram(g, BUFFER, SPREAD_TO, SPREAD_BYTES);
    ink_the_fourth_plane(g, SPREAD_TO, INKED_LAST_TILES);
    /* two pairs of tiles from the ROM, each pair's second in the row below */
    for (unsigned i = 0; i < 4; i++)
        move_from_rom(g, (uint16_t)(MENU_ROM_SIXTEEN_COLOUR_TILES + i * ROM_TILE_PAIR),
                      (uint16_t)(SPREAD_TO + (i & 1) * ROWS_APART + (i >> 1) * ROM_TILE_PAIR), ROM_TILE_PAIR);
    ink_the_last_two_planes(g, SPREAD_TO, 4);
    ink_the_last_two_planes(g, (uint16_t)(SPREAD_TO + ROWS_APART), 4);
    TIMED_AS(g, 0xC28385, queue_records(g, MENU_UPLOAD_LAST_SET, 1));
    send_now(g, pf);
}

/* The windows' first style: its tiles from $10, in the first palette row. */
enum { FIRST_WINDOW_STYLE = 0x0010, BUFFER_CLEARED = 0x4000, PALETTE_CHANGED = 0x01, DESIGN_SETTING_BITS = 0x07 };

// $C2:968D
void menu_build_graphics(GameState *g, Platform *pf)
{
    build_first_sets(g, pf);
    build_second_set(g, pf);
    build_window_sets(g, pf);
    load_menu_colours(g);
    build_last_set(g, pf);
    TIMED_AS(g, 0xC29512, menu_clear_memory(g, pf, BUFFER, BUFFER_CLEARED));
    load_colours(g, MENU_ROM_ROW_8_COLOURS, ROW_8, COLOURS_A_ROW);
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) | PALETTE_CHANGED));
    TIMED_AS(g, 0xC286A9, menu_send_palette(g, pf));
    set_menu_window_style(g, FIRST_WINDOW_STYLE);
    set_menu_window_design(g, (uint8_t)(game_settings(g, 1) & DESIGN_SETTING_BITS));
    TIMED_AS(g, 0xC2C511, menu_load_window_design(g));
}

// $C2:C511
/* The window design the player chose: its twenty tiles (and a byte past
 * them, as the original's block move takes) into the buffer at $7E:8800,
 * inked in their fourth plane, queued for VRAM, and its colours. The
 * original notes the tiles' VRAM word at $7E:9890 as well, which nothing
 * reads. */
enum { DESIGN_BUFFER = 0x8800, DESIGN_TILES = 20, DESIGN_VRAM = 0x3DC0 };

static void leave_design_vram(GameState *g)
{
    set_menu_save_summary_byte(g, 0, (uint8_t)DESIGN_VRAM);
    set_menu_save_summary_byte(g, 1, (uint8_t)(DESIGN_VRAM >> 8));
}

void menu_load_window_design(GameState *g)
{
    ORIGINAL_EFFECT(leave_design_vram(g));
    uint8_t design = (uint8_t)(menu_window_design(g) & DESIGN_SETTING_BITS);
    move_from_rom(g, (uint16_t)(MENU_ROM_WINDOW_DESIGNS + design * MENU_WINDOW_DESIGN_BYTES), DESIGN_BUFFER,
                  MENU_WINDOW_DESIGN_BYTES + 1);
    ink_the_fourth_plane(g, DESIGN_BUFFER, DESIGN_TILES);
    TIMED_AS(g, 0xC28385, queue_records(g, MENU_UPLOAD_WINDOW_DESIGN, 1));
    TIMED_AS(g, 0xC2ED08, menu_load_window_colours(g, menu_window_style(g)));
}

/* ---- The entries ------------------------------------------------------------------------- */

/* The graphics leave the accumulator 8-bit, the index registers 16-bit, and
 * the data bank on work RAM's, as the design's loading and the block moves
 * before it do; and the flags of the design's setting's last test (whether it
 * is the first design), with the carry of the tiles' last loop. */
void menu_leave_graphics_built(Snes *s)
{
    menu_set_widths(s, true, false);
    s->cpu.db = MENU_DATA_BANK;
    ORIGINAL_EFFECT(menu_leave_load(s, (uint8_t)(menu_window_design(state_of(s)))));
    ORIGINAL_EFFECT(m_set_flag(s, CPU_FLAG_C, true));
}

void menu_build_graphics_entry(Snes *s)
{
    menu_build_graphics(state_of(s), m_platform(s));
    menu_leave_graphics_built(s);
    m_rts(s);
}

/* The design's colours' block move leaves its count run out in A. */
static void leave_count_run_out(Snes *s) { s->cpu.a = 0xFFFF; }

void menu_load_window_design_entry(Snes *s)
{
    menu_load_window_design(state_of(s));
    ORIGINAL_EFFECT(leave_count_run_out(s));
    s->cpu.db = MENU_DATA_BANK;
    m_rts(s);
}

/* Both write work RAM's buffers through a data bank on it or its mirrors,
 * with the queue empty enough for what they queue. */
enum { QUEUE_BYTES = 0x100 };

static bool queue_has_room(const GameState *g, unsigned records)
{
    uint8_t queued = menu_vram_queue_bytes(g);
    return queued % MENU_UPLOAD_BYTES == 0 && queued + records * MENU_UPLOAD_BYTES <= QUEUE_BYTES;
}

bool menu_graphics_are_buildable(Snes *s)
{
    vars_peek(true);
    bool ok = queue_has_room(state_of(s), 3);
    vars_peek(false);
    return ok;
}

bool menu_window_design_is_loadable(Snes *s)
{
    vars_peek(true);
    bool ok = queue_has_room(state_of(s), 1) && menu_palette_changes_have_room(state_of(s));
    vars_peek(false);
    return ok;
}
