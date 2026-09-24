/* The load screen: the save slots, and the menus' sprites each frame.
 *
 * The load screen lists the save slots, and shows the summary of the one the
 * cursor is on: its header (the time played and where the party is, drawn by
 * the menus' window command lists), a label for the era it was saved in, and
 * a row for each party member with the member's portrait. Moving the cursor
 * skips the slots that hold no game. Once the game has been finished, a
 * fourth row offers two choices, which left and right flip between. Behind
 * the rows a per-line effect sets the slot rows apart, and the row of the
 * slot after the one shown is marked in it.
 *
 * The summaries are kept for every slot ($7E:7600, save_slot_summaries); the
 * screen copies the one shown to its working copy and draws from that. The
 * window command lists ($C2:ED31), the location name ($C2:F28D), the loader
 * of the era label's graphic ($C2:F90C), the drawing of its tiles ($C2:FB97)
 * and the fourth row's redraw ($C2:EAC2) are still the original's, so the two
 * routines that call them are bridges: the logic here, and the calls made
 * with the registers the original has at each.
 *
 * Every frame the menus also put the sprites back: most are hidden, and the
 * cursor goes where the current menu's cursor position says. */
#include "menu/save_screen.h"

#include "menu/menu_data.h"
#include "menu/vram.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* ---- The sprites --------------------------------------------------------------------------- */

/* The menus own sprites 16-95; those they aren't using wait below the screen,
 * large, with nothing drawn. The cursor is sprite 28. */
enum { FIRST_MENU_SPRITE = 16, MENU_SPRITES = 80, CURSOR_SPRITE = 28 };
enum { HIDDEN_Y = 0xE0, HIDDEN_ATTRIBUTES = 0xE0, ALL_LARGE = 0xAA, SPRITES_A_HIGH_BYTE = 4 };
enum { FLIPPED = 0x40 };

static void hide_menu_sprites(GameState *g)
{
    for (unsigned i = FIRST_MENU_SPRITE; i < FIRST_MENU_SPRITE + MENU_SPRITES; i++) {
        set_oam_buffer_x(g, i, 0);
        set_oam_buffer_y(g, i, HIDDEN_Y);
        set_oam_buffer_tile(g, i, 0);
        set_oam_buffer_attributes(g, i, HIDDEN_ATTRIBUTES);
    }
    for (unsigned i = FIRST_MENU_SPRITE; i < FIRST_MENU_SPRITE + MENU_SPRITES; i += SPRITES_A_HIGH_BYTE)
        set_oam_high_table_byte(g, i / SPRITES_A_HIGH_BYTE, ALL_LARGE);
}

/* The cursor at the current menu's cursor position: its sprite's tile and
 * attributes, flipped where the menu says. */
static void place_cursor(GameState *g)
{
    uint8_t at = menu_cursor_index(g);
    set_oam_buffer_x(g, CURSOR_SPRITE, menu_cursor_places_x(g, at));
    set_oam_buffer_y(g, CURSOR_SPRITE, menu_cursor_places_y(g, at));
    uint16_t sprite = menu_cursor_sprite(g);
    set_oam_buffer_tile(g, CURSOR_SPRITE, (uint8_t)sprite);
    set_oam_buffer_attributes(g, CURSOR_SPRITE, (uint8_t)(sprite >> 8));
    if (menu_cursor_flip(g) != 0)
        set_oam_buffer_attributes(g, CURSOR_SPRITE, (uint8_t)(oam_buffer_attributes(g, CURSOR_SPRITE) | FLIPPED));
}

// $C2:E92D
/* The menus' sprites each frame: all hidden, then the cursor placed, unless
 * the menu hid it for this one frame. */
static void reset_sprites(GameState *g)
{
    hide_menu_sprites(g);
    bool hidden = menu_cursor_hidden(g) != 0;
    set_menu_cursor_hidden(g, 0);
    if (!hidden)
        place_cursor(g);
}

/* ---- The slot rows' per-line effect ------------------------------------------------------------ */

/* The slot rows' values ($C2:D044, 33 bytes), where the channel reads its
 * table from, and the marked row's band: 32 lines of the values' last. */
enum { ROW_VALUES = 0x21, EFFECT_TABLE = 0x9300, MARKED_LINES = 0x20, MARKED_VALUES = 0x9740 };
enum { TABLE_BANDS = 5 };
/* menu_line_effects: the block move the vblank makes, and the channel of the
 * load screen's per-line effect. */
enum { BLOCK_MOVE = 0x80, SLOT_ROWS_CHANNEL = 0x40 };

// $C2:CFFB
/* Set the per-line effect up for the next frame: the rows' values, the table
 * as the screen's setup built it with the row of the slot after the one shown
 * marked, and the vblank asked to move the table into place. */
static void load_slot_rows_effect(GameState *g)
{
    for (unsigned i = 0; i < ROW_VALUES; i++)
        set_menu_slot_row_values_byte(g, i, menu_slot_row_value(g, i));
    for (unsigned band = 0; band < TABLE_BANDS; band++) {
        set_menu_slot_row_table_lines(g, band, menu_slot_row_template_lines(g, band));
        set_menu_slot_row_table_values(g, band, menu_slot_row_template_values(g, band));
    }
    set_menu_slot_row_table_end(g, menu_slot_row_template_end(g));
    set_menu_block_move_source(g, RAM_ABS(RAM_MENU_SLOT_ROW_TABLE));
    set_menu_block_move_dest(g, EFFECT_TABLE);
    set_menu_block_move_size(g, TABLE_BANDS * 3 + 1);
    unsigned marked = menu_save_slot_next(g) + 1u;
    set_menu_slot_row_table_lines(g, marked, MARKED_LINES);
    set_menu_line_effects(g, (uint8_t)(menu_line_effects(g) | BLOCK_MOVE | SLOT_ROWS_CHANNEL));
    set_menu_slot_row_table_values(g, marked, MARKED_VALUES);
}

/* ---- A member's row ---------------------------------------------------------------------------- */

/* The tilemaps the load screen draws are in bank $7E. */
static void put_tilemap_byte(GameState *g, uint16_t origin, uint16_t offset, uint8_t v)
{
    wram_write8(g, 0x7E0000u + origin + offset, v);
}

/* A member's row is three tilemap rows ($C0 bytes); its portrait is two tiles
 * square, a row down and fifteen tiles in, in the palette four on from the
 * row (and the tiles' upper half), from the portraits' tiles and colours
 * (menu_data.h). */
enum { TILEMAP_ROW = 0x40, MEMBER_ROW_BYTES = 0xC0, PORTRAIT_AT = 0x5E };
enum { PALETTE_SHIFT = 2, FIRST_PORTRAIT_PALETTE = 4 << PALETTE_SHIFT, UPPER_TILES = 0x01, COLOURS = 16 };
enum { PORTRAIT_PALETTE_COLOURS = 4 * COLOURS };

// $C2:D466
/* The member's portrait on its row: its tiles, and its colours in the row's
 * palette. */
static void draw_member_portrait(GameState *g)
{
    static const uint16_t corners[4] = { 0, 2, TILEMAP_ROW, TILEMAP_ROW + 2 };
    uint16_t origin = menu_draw_origin(g);
    uint16_t row = menu_save_member_row(g);
    uint8_t attribute = (uint8_t)((uint8_t)(row << PALETTE_SHIFT) | FIRST_PORTRAIT_PALETTE | UPPER_TILES);
    for (unsigned i = 0; i < 4; i++)
        put_tilemap_byte(g, origin, (uint16_t)(PORTRAIT_AT + corners[i] + 1), attribute);
    uint8_t character = (uint8_t)menu_scratch_save_member(g);
    ORIGINAL_EFFECT(set_menu_scratch_save_member(g, character));
    uint8_t tile = menu_portrait_first_tile(g, character);
    for (unsigned i = 0; i < 4; i++)
        put_tilemap_byte(g, origin, (uint16_t)(PORTRAIT_AT + corners[i]), (uint8_t)(tile + i));
    for (unsigned i = 0; i < COLOURS; i++)
        set_menu_palette(g, PORTRAIT_PALETTE_COLOURS + row * COLOURS + i, menu_portrait_colour(g, character, i));
}

/* ---- A slot's summary ------------------------------------------------------------------------- */

/* The summary: three member records of 16 bytes, the first byte the member's
 * character (none, with its top bit set), and the slot's header after them:
 * where the party is, and the location's name. */
enum { SUMMARY_BYTES = 0x40, MEMBER_ROWS = 3, MEMBER_RECORD_BYTES = 0x10, NO_MEMBER = 0x80 };
enum { SUMMARY_LOCATION = 0x32 };

/* What a status window draws a member from: the record's first nine bytes,
 * its next six, and its last with the byte after it (the next record's first,
 * or the header's), which the window reads as a word. */
enum { STATUS_FIRST_BYTES = 9, STATUS_NEXT_BYTES = 6, STATUS_LAST = STATUS_FIRST_BYTES + STATUS_NEXT_BYTES };

/* The slots the load screen can show: the fourth row is a choice, not a slot. */
enum { SAVE_SLOTS = 3 };

enum { SUMMARY_MEMBER_BYTES = MEMBER_ROWS * MEMBER_RECORD_BYTES };

static void copy_summary(GameState *g, uint8_t slot)
{
    for (unsigned i = 0; i < SUMMARY_MEMBER_BYTES; i++)
        set_menu_save_summary_byte(g, i, save_slot_summaries_members_byte(g, slot, i));
    for (unsigned i = SUMMARY_MEMBER_BYTES; i < SUMMARY_BYTES; i++)
        set_menu_save_summary_byte(g, i, save_slot_summaries_header_byte(g, slot, i - SUMMARY_MEMBER_BYTES));
}

static bool member_present(const GameState *g, unsigned row)
{
    return (menu_save_summary_byte(g, row * MEMBER_RECORD_BYTES) & NO_MEMBER) == 0;
}

/* The member's record where a status window draws it from. */
static void show_member(GameState *g, unsigned row)
{
    unsigned record = row * MEMBER_RECORD_BYTES;
    for (unsigned i = 0; i < STATUS_FIRST_BYTES; i++)
        set_menu_status_record_head_byte(g, i, menu_save_summary_byte(g, record + i));
    for (unsigned i = 0; i < STATUS_NEXT_BYTES; i++)
        set_menu_status_record_tail_byte(g, i, menu_save_summary_byte(g, record + STATUS_FIRST_BYTES + i));
    set_menu_status_record_last(g, (uint16_t)(menu_save_summary_byte(g, record + STATUS_LAST)
                                              | menu_save_summary_byte(g, record + STATUS_LAST + 1) << 8));
}

/* The era the slot was saved in, which the load screen shows a label for. */
static uint8_t slot_era(const GameState *g)
{
    uint16_t location = (uint16_t)(menu_save_summary_byte(g, SUMMARY_LOCATION)
                                   | menu_save_summary_byte(g, SUMMARY_LOCATION + 1) << 8);
    return menu_era_of_location(g, location);
}

/* The era label's graphic goes to $7E:5E00, and its tiles two tilemap rows at
 * $7E:5248 and the row below. */
enum { ERA_LABEL_GRAPHIC = 0x5E00, ERA_LABEL_TILES = 0x5248, ERA_LABEL_FIRST_TILE = 1 };

/* Ask the menus' graphics loader for an era's label. */
static void request_era_label(GameState *g, uint8_t era)
{
    set_menu_save_slot_era(g, era);
    set_menu_load_request_entry(g, era);
    set_menu_load_destination(g, ERA_LABEL_GRAPHIC);
    set_menu_load_request_table(g, MENU_ERA_LABELS);
}

/* How many tiles wide an era label of `width` pixels is. */
static uint16_t tiles_across(uint8_t width) { return (uint16_t)((width + 7) / 8); }

/* ---- The bridges: the original's routines the load screen calls ------------------------------- */

/* The bridges below run on the menus' page with the data bank on work RAM's
 * ($7E), which the summary's copy leaves it on, and call the original's
 * routines with the widths the original has at each call. The original saves
 * P when it starts, and its code after a call the port can't go on from pulls
 * it at its end, so the bridges save it the same way: where a callee comes
 * back as the port doesn't expect, the port stops at the call's return and
 * the original goes on with its own frame. */
enum { JSR_SIZE = 3, WORK_RAM_BANK = 0x7E };

static void set_widths(Snes *s, bool a8, bool x8)
{
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_sep(s, (uint8_t)((a8 ? CPU_FLAG_M : 0) | (x8 ? CPU_FLAG_X : 0)));
}

static bool back_on_the_menus_page(Snes *s, uint32_t site, bool a8, bool x8)
{
    if (!s->cpu.e && m_m8(s) == a8 && m_x8(s) == x8 && s->cpu.db == WORK_RAM_BANK && m_direct_page(s) == 0)
        return true;
    m_stop_at(s, site + JSR_SIZE, 0);
    return false;
}

static bool call_original(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, bool a8, bool x8)
{
    set_widths(s, a8, x8);
    m_jsr_dc(s, site, routine, dont_care);
    return back_on_the_menus_page(s, site, a8, x8);
}

/* The window command lists ($C2:ED31) the slot's header and its member rows
 * are drawn with. Their interpreter dispatches through a table, so liveness
 * can't show the carry and overflow dead at the call; the game's lists never
 * read them, which scrambling them shows. */
static bool draw_window_list(Snes *s, uint32_t site)
{
    set_widths(s, false, false);
    /* dc-evidence: ct_fuzz --poison-dc menu_draw_save_slot:poison, seed 1: 500 states,
       3340 call-outs scrambled, 0 mismatched */
    m_jsr_dc(s, site, 0xC2ED31, DC_A | DC_Y | DC_N | DC_V | DC_Z | DC_C | DC_DB);
    return back_on_the_menus_page(s, site, false, false);
}

/* Registers the callees don't read (tools/dontcare_check.py checks each). */
enum {
    LOCATION_NAME_IGNORES = DC_A | DC_N | DC_V | DC_Z | DC_C,                       /* $C2:F28D */
    LOADER_IGNORES = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C | DC_D,         /* $C2:F90C */
    TILE_STRIP_IGNORES = DC_N | DC_V | DC_Z | DC_C | DC_D | DC_AH,                  /* $C2:FB97 */
    FOURTH_ROW_IGNORES = DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C | DC_D | DC_AL,    /* $C2:EAC2 */
};

/* The window command lists the load screen draws with: the slot's header, and
 * a member's row. */
enum { HEADER_LIST = 0xC15E, MEMBER_ROW_LIST = 0xC168 };
/* Where the location's name is drawn, and where it is read from. */
enum { LOCATION_NAME_TILES = 0x3310, LOCATION_NAME = 0x98C9 };
/* Where the member rows start in the slot's tilemap, and the summary. */
enum { MEMBER_ROWS_AT = 0x3200, SUMMARY_AT = 0x9890 };

static void queue_upload(Snes *s, uint16_t record)
{
    GameState *g = state_of(s);
    NATIVE_TIME_AS(s, 0xC28385, menu_queue_upload(g, menu_upload_record(g, record)));
}

// $C2:D36C, from $C2:D39E
/* The slot's header: the window command list's, the location's name, and
 * the era label, loaded when it isn't the one already there, drawn and
 * queued. */
static bool draw_header(Snes *s)
{
    GameState *g = state_of(s);
    s->cpu.x = HEADER_LIST;
    if (!draw_window_list(s, 0xC2D3A1))
        return false;
    s->cpu.x = LOCATION_NAME_TILES;
    s->cpu.y = LOCATION_NAME;
    if (!call_original(s, 0xC2D3AA, 0xC2F28D, LOCATION_NAME_IGNORES, false, false))
        return false;
    uint8_t era = slot_era(g);
    if (era != menu_save_slot_era(g)) {
        request_era_label(g, era);
        if (!call_original(s, 0xC2D3DF, 0xC2F90C, LOADER_IGNORES, true, false))
            return false;
        set_menu_save_era_width(g, menu_loaded_graphic_width(g));
    }
    uint8_t width = menu_save_era_width(g);
    if (width != 0) {
        s->cpu.a = ERA_LABEL_FIRST_TILE;
        s->cpu.x = ERA_LABEL_TILES;
        s->cpu.y = tiles_across(width);
        if (!call_original(s, 0xC2D3FB, 0xC2FB97, TILE_STRIP_IGNORES, true, false))
            return false;
    }
    queue_upload(s, MENU_UPLOAD_ERA_LABEL);
    return true;
}

// $C2:D36C, from $C2:D404
/* A row for each member in the party: the member's record where the window
 * command list draws it from, and the portrait. The rows' cells are what the
 * command list and the portrait read. */
static bool draw_member_rows(Snes *s)
{
    GameState *g = state_of(s);
    set_menu_draw_origin(g, MEMBER_ROWS_AT);
    set_menu_scratch_save_member_record(g, SUMMARY_AT);
    set_menu_save_member_row(g, 0);
    for (unsigned row = 0; row < MEMBER_ROWS; row++) {
        uint8_t member = menu_save_summary_byte(g, row * MEMBER_RECORD_BYTES);
        set_menu_scratch_save_member(g, (uint16_t)((menu_scratch_save_member(g) & 0xFF00) | member));
        if (member_present(g, row)) {
            show_member(g, row);
            s->cpu.x = MEMBER_ROW_LIST;
            if (!draw_window_list(s, 0xC2D43A))
                return false;
            NATIVE_TIME_AS(s, 0xC2D466, draw_member_portrait(g));
            set_menu_draw_origin(g, (uint16_t)(menu_draw_origin(g) + MEMBER_ROW_BYTES));
        }
        set_menu_scratch_save_member_record(g, (uint16_t)(SUMMARY_AT + (row + 1) * MEMBER_RECORD_BYTES));
        set_menu_save_member_row(g, (uint16_t)(row + 1));
    }
    return true;
}

/* The drawn slot asks for the palette's upload: the portraits are in it. */
static void palette_changed(GameState *g) { set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1)); }

// $C2:D36C
/* Draw the summary of the slot shown, a slot that holds a game: nothing for
 * the fourth row or an empty slot. */
static bool draw_save_slot(Snes *s)
{
    GameState *g = state_of(s);
    set_menu_member_marked(g, 0);
    uint8_t slot = menu_save_slot_shown(g);
    if (slot >= SAVE_SLOTS || menu_save_slot_in_use(g, slot) == 0)
        return true;
    copy_summary(g, slot);
    s->cpu.db = WORK_RAM_BANK;   /* the copy's block move leaves it there */
    if (menu_save_header_taken(g) == 0 && !draw_header(s))
        return false;
    if (!draw_member_rows(s))
        return false;
    palette_changed(g);
    return true;
}

/* The slots the cursor moves between, and the cursor's moves this frame. */
enum { SLOT_ROWS = 4, ROW_MASK = SLOT_ROWS - 1, FOURTH_ROW = 3 };
enum { MOVED_RIGHT = 0x01, MOVED_LEFT = 0x02, MOVED_UP = 0x08 };
/* The fourth row's two choices are $70 apart across the screen. */
enum { FOURTH_ROW_CHOICES_APART = 0x70 };

/* The P the slot's drawing saves as it starts: the original's compare with
 * the fourth row leaves its carry there (where the game is finished), and its
 * last load before the call its zero and negative clear. */
static void leave_fourth_row_compare(Snes *s, uint8_t slot) { m_set_flag(s, CPU_FLAG_C, slot >= FOURTH_ROW); }
static void leave_last_load(Snes *s)
{
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, false);
}

/* Off an empty slot: back up the list when the cursor moved up, and down it
 * otherwise, round the four rows. */
static uint8_t skip_empty_slots(GameState *g, uint8_t slot)
{
    bool up = (menu_cursor_moves(g) & MOVED_UP) != 0;
    while (menu_save_slot_in_use(g, slot) == 0)
        slot = (uint8_t)((up ? slot - 1 : slot + 1) & ROW_MASK);
    return slot;
}

/* The slot's tilemap and the era label's tiles cleared for the slot shown. */
enum { SLOT_TILEMAP_BYTES = 0x240, ERA_LABEL_ROWS = 2, ERA_LABEL_ROW_BYTES = 0x12 };

static void clear_slot_tilemap(GameState *g)
{
    for (unsigned i = 0; i < SLOT_TILEMAP_BYTES; i++)
        set_menu_save_slot_tilemap_byte(g, i, 0);
    for (unsigned row = 0; row < ERA_LABEL_ROWS; row++)
        for (unsigned i = 0; i < ERA_LABEL_ROW_BYTES; i++)
            set_menu_era_label_tiles(g, row, i, 0);
}

// $C2:E743
/* Show the slot the cursor is on: off the empty slots; on the fourth row, left
 * or right flips between its choices; then the slot rows' effect, and the
 * slot's summary drawn and queued. */
static bool show_save_slot(Snes *s)
{
    GameState *g = state_of(s);
    set_widths(s, true, true);   /* its own widths, which cut the index registers to a byte */
    uint8_t slot = skip_empty_slots(g, menu_cursor_index(g));
    set_menu_cursor_index(g, slot);
    bool finished = menu_saves_finished_game(g);
    if (finished)
        ORIGINAL_EFFECT(leave_fourth_row_compare(s, slot));
    if (finished && slot == FOURTH_ROW && (menu_cursor_moves(g) & (MOVED_RIGHT | MOVED_LEFT))) {
        uint8_t x = (uint8_t)(menu_cursor_places_x(g, FOURTH_ROW) ^ FOURTH_ROW_CHOICES_APART);
        set_menu_cursor_places_x(g, FOURTH_ROW, x);
        m_set_a8(s, x);
        s->cpu.x = slot;
        if (!call_original(s, 0xC2E777, 0xC2EAC2, FOURTH_ROW_IGNORES, true, true))
            return false;
    }
    set_menu_save_slot_shown(g, slot);
    set_menu_save_slot_next(g, (uint8_t)((slot + 1) & ROW_MASK));
    NATIVE_TIME_AS(s, 0xC2CFFB, load_slot_rows_effect(g));
    clear_slot_tilemap(g);
    s->cpu.db = WORK_RAM_BANK;   /* the clearing's block moves leave it there */
    set_widths(s, false, false);
    ORIGINAL_EFFECT(leave_last_load(s));
    return true;
}

/* ---- The entries ------------------------------------------------------------------------------ */

/* The original's frame: the P each routine saves as it starts and pulls as it
 * ends (which puts its caller's widths back). */
static void save_p(Snes *s) { m_push8(s, s->cpu.p); }
static void restore_p(Snes *s)
{
    s->cpu.p = m_pull8(s);
    if (m_x8(s)) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
}

/* What the queue's block move leaves after the last upload a routine queued
 * (menu/vram.c): the data bank on work RAM's, A's high byte $FF and its low
 * the queue's count, X past the record and Y past the queue's entry. */
static void leave_last_upload(Snes *s, uint16_t record)
{
    uint8_t queued = menu_vram_queue_bytes(state_of(s));
    s->cpu.db = WORK_RAM_BANK;
    s->cpu.a = (uint16_t)(0xFF00 | queued);
    s->cpu.x = (uint16_t)(record + 8);
    s->cpu.y = (uint16_t)(RAM_ABS(RAM_MENU_VRAM_QUEUE) + queued);
}

/* The block moves leave the data bank on work RAM's. */
static void leave_block_move_bank(Snes *s) { s->cpu.db = WORK_RAM_BANK; }

/* Where the sprite reset leaves X and Y: past the high table's fill, or with
 * the cursor placed, X at its place's entry. */
static void leave_sprite_reset(Snes *s, bool placed)
{
    GameState *g = state_of(s);
    s->cpu.db = WORK_RAM_BANK;
    uint16_t x = placed ? (uint16_t)(menu_cursor_index(g) * 6) : 0x0916;
    s->cpu.x = m_x8(s) ? (uint8_t)x : x;
    s->cpu.y = m_x8(s) ? 0x18 : 0x0918;
}

// $C2:E92D
void menu_reset_sprites(Snes *s)
{
    GameState *g = state_of(s);
    vars_peek(true);
    bool placed = menu_cursor_hidden(g) == 0;
    vars_peek(false);
    reset_sprites(g);
    ORIGINAL_EFFECT(leave_sprite_reset(s, placed));
    m_rts(s);
}

// $C2:CFFB
void menu_load_slot_rows_effect(Snes *s)
{
    load_slot_rows_effect(state_of(s));
    ORIGINAL_EFFECT(leave_block_move_bank(s));
    m_rts(s);
}

// $C2:D466
void menu_draw_member_portrait(Snes *s)
{
    draw_member_portrait(state_of(s));
    m_rts(s);
}

// $C2:D36C
void menu_draw_save_slot(Snes *s)
{
    save_p(s);
    if (!draw_save_slot(s))
        return;
    restore_p(s);
    m_rts(s);
}

// $C2:E743
void menu_show_save_slot(Snes *s)
{
    save_p(s);
    if (!show_save_slot(s))
        return;
    m_push16(s, 0xE7AC + 2);   /* the JSR to the slot's drawing, called as C */
    NATIVE_TIME_AS(s, 0xC2D36C, menu_draw_save_slot(s));
    if (s->cpu.pb != 0xC2 || s->cpu.pc != 0xE7AF)
        return;
    queue_upload(s, MENU_UPLOAD_SLOT_TILEMAP);
    queue_upload(s, MENU_UPLOAD_SLOT_TILEMAP_2);
    queue_upload(s, MENU_UPLOAD_SLOT_TILEMAP_3);
    ORIGINAL_EFFECT(leave_last_upload(s, MENU_UPLOAD_SLOT_TILEMAP_3));
    restore_p(s);
    m_rts(s);
}

/* ---- The guards ------------------------------------------------------------------------------- */

/* The load screen's routines run on the menus' page, reaching the slot flags
 * and the queue through a data bank on low RAM. What the game gives them
 * besides: a cursor on one of the four rows and at least one slot with a game
 * (else the skip of empty slots goes round for ever), whole records in the
 * upload queue, a palette change the drawing can count, and summaries whose
 * members are characters. Looked at without the machine's time. */
static bool on_the_menus_page(Snes *s) { return m_direct_page(s) == 0x0000; }

static bool members_are_characters(const GameState *g, uint8_t slot)
{
    for (unsigned row = 0; row < MEMBER_ROWS; row++) {
        uint8_t member = save_slot_summaries_members_byte(g, slot, row * MEMBER_RECORD_BYTES);
        if (!(member & NO_MEMBER) && member >= MENU_PORTRAITS)
            return false;
    }
    return true;
}

static bool slot_is_drawable(const GameState *g, uint8_t slot)
{
    return slot >= SAVE_SLOTS || menu_save_slot_in_use(g, slot) == 0 || members_are_characters(g, slot);
}

static bool drawing_counts(const GameState *g)
{
    return menu_vram_queue_bytes(g) % 8 == 0 && menu_palette_changed(g) != 0xFF;
}

bool menu_save_slot_is_drawable(Snes *s)
{
    if (!on_the_menus_page(s) || !m_bank_reaches_low_ram(s))
        return false;
    GameState *g = state_of(s);
    vars_peek(true);
    uint8_t slot = menu_save_slot_shown(g);
    bool ok = slot < SLOT_ROWS && slot_is_drawable(g, slot) && drawing_counts(g);
    vars_peek(false);
    return ok;
}

bool menu_save_slots_are_showable(Snes *s)
{
    if (!on_the_menus_page(s) || !m_bank_reaches_low_ram(s))
        return false;
    GameState *g = state_of(s);
    vars_peek(true);
    bool any = false, drawable = true;
    for (uint8_t slot = 0; slot < SLOT_ROWS; slot++) {
        any = any || menu_save_slot_in_use(g, slot) != 0;
        drawable = drawable && slot_is_drawable(g, slot);
    }
    bool ok = menu_cursor_index(g) < SLOT_ROWS && any && drawable && drawing_counts(g);
    vars_peek(false);
    return ok;
}

bool menu_portrait_is_drawable(Snes *s)
{
    if (!on_the_menus_page(s) || s->cpu.db != WORK_RAM_BANK)
        return false;
    GameState *g = state_of(s);
    vars_peek(true);
    bool ok = menu_save_member_row(g) < MEMBER_ROWS && (uint8_t)menu_scratch_save_member(g) < MENU_PORTRAITS;
    vars_peek(false);
    return ok;
}

bool menu_slot_rows_effect_is_loadable(Snes *s)
{
    if (!on_the_menus_page(s))
        return false;
    vars_peek(true);
    bool ok = menu_save_slot_next(state_of(s)) < SLOT_ROWS;
    vars_peek(false);
    return ok;
}

bool menu_sprites_are_resettable(Snes *s)
{
    if (!on_the_menus_page(s) || s->cpu.db != WORK_RAM_BANK)
        return false;
    vars_peek(true);
    bool ok = menu_cursor_index(state_of(s)) < RAM_MENU_CURSOR_PLACES_COUNT;
    vars_peek(false);
    return ok;
}
