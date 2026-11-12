/* The save slots' summaries: read from the cartridge as the menus open, and
 * shown beside the slots on the two screens that list them, the load screen
 * (menu/load_screen.c) and a save point's save screen (menu/saving.c), as the
 * cursor moves.
 *
 * As the menus open, each of the three slots is checked on the cartridge (by
 * the save code, menu/saves.c); a slot that holds a game
 * gets a summary ($7E:7600, save_slot_summaries): a record for each party
 * member -- the character, the start of its figures, its name -- and a header
 * -- how many times the game was saved, where the party is, its gold, the
 * time played, how many times the game had been finished -- and a label for
 * the chapter of the story it was saved in; one that doesn't gets the empty
 * slot's label. Every slot gets its name label. The labels are set in the
 * dialog's font by the typesetter (typeset.c's menu_set_label, $C2:F90C).
 *
 * Both screens show the summary of the slot under the cursor: its header (the
 * time played and the rest, drawn by the window command lists), a label for
 * the era it was saved in, and a row for each party member with the member's
 * portrait (menu_draw_the_slot_shown). The screen copies the summary shown to
 * its working copy and draws from that. Behind the rows a per-line effect sets
 * the slot rows apart and marks one of them. On the load screen the cursor
 * skips the slots that hold no game, the row of the slot after the one shown
 * is marked, and once the game has been finished a fourth row offers two
 * choices, which left and right flip between ($C2:E743). On the save screen
 * the cursor may rest on an empty slot, which shows nothing; its own row is
 * marked, and a tick sounds as it comes to another slot ($C2:CFA2). */
#include "menu/save_screen.h"

#include "charge.h"
#include "core/program.h"
#include "cpu_io.h"
#include "menu/cursor.h"
#include "menu/load_screen.h"
#include "menu/saving.h"
#include "menu/menu_data.h"
#include "menu/saves.h"
#include "menu/text.h"
#include "menu/typeset.h"
#include "menu/windows.h"
#include "menu/vram.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* ---- The slot rows' per-line effect ------------------------------------------------------------ */

/* The slot rows' values ($C2:D044, 33 bytes), where the channel reads its
 * table from, and the marked row's band: 32 lines of the values' last. */
enum { ROW_VALUES = 0x21, EFFECT_TABLE = 0x9300, MARKED_LINES = 0x20, MARKED_VALUES = 0x9740 };
enum { TABLE_BANDS = 5 };
/* menu_line_effects: the block move the vblank makes, and the channel of the
 * load screen's per-line effect. */
enum { BLOCK_MOVE = 0x80, SLOT_ROWS_CHANNEL = 0x40 };

/* Set the per-line effect up for the next frame: the rows' values, the table
 * as the screen's setup built it with the row of the slot after the one shown
 * marked, and the vblank asked to move the table into place. */
void menu_put_slot_rows_effect(GameState *g)
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
    unsigned marked = menu_save_slot_marked(g) + 1u;
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

/* A member's portrait on the member's row of the slot's tilemap, which starts
 * at `origin`: the character's tiles, and its colours in the row's palette.
 * The original keeps the character in its cell as a word. */
static void draw_member_portrait(GameState *g, uint16_t origin, uint16_t row, uint8_t character)
{
    static const uint16_t corners[4] = { 0, 2, TILEMAP_ROW, TILEMAP_ROW + 2 };
    uint8_t attribute = (uint8_t)((uint8_t)(row << PALETTE_SHIFT) | FIRST_PORTRAIT_PALETTE | UPPER_TILES);
    for (unsigned i = 0; i < 4; i++)
        put_tilemap_byte(g, origin, (uint16_t)(PORTRAIT_AT + corners[i] + 1), attribute);
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

/* Set an era's label, and note it as the one the header shows; its width. */
static uint8_t set_era_label(GameState *g, uint8_t era)
{
    set_menu_save_slot_era(g, era);
    set_menu_load_destination(g, ERA_LABEL_GRAPHIC);
    uint8_t width;
    TIMED_AS(g, 0xC2F90C, width = menu_set_label(g, MENU_ERA_LABELS, era, ERA_LABEL_GRAPHIC));
    return width;
}

/* How many tiles wide an era label of `width` pixels is. */
static uint16_t tiles_across(uint8_t width) { return (uint16_t)((width + 7) / 8); }

/* ---- The slot's drawing ------------------------------------------------------------------------ */

/* The window command lists the load screen draws with: the slot's header, and
 * a member's row. */
enum { HEADER_LIST = 0xC15E, MEMBER_ROW_LIST = 0xC168 };
/* Where the time played is drawn, and where its digits are in the summary. */
enum { TIME_PLAYED_AT = 0x3310, TIME_PLAYED_DIGITS = 0x98C9 };
/* Where the member rows start in the slot's tilemap, and the summary. */
enum { MEMBER_ROWS_AT = 0x3200, SUMMARY_AT = 0x9890 };

/* The header's window, with the time played. */
static void draw_header_window(GameState *g, Platform *pf)
{
    menu_draw_windows(g, pf, HEADER_LIST);
    TIMED_AS(g, 0xC2F28D, (void)menu_draw_time_played(g, TIME_PLAYED_AT, TIME_PLAYED_DIGITS));
}

/* The era label's tiles, as wide as the label, and the header queued. */
static void finish_header(GameState *g)
{
    uint8_t width = menu_save_era_width(g);
    if (width != 0)
        TIMED_AS(g, 0xC2FB97, menu_lay_out_label(g, ERA_LABEL_TILES, tiles_across(width), ERA_LABEL_FIRST_TILE));
    menu_queue_fixed_upload(g, MENU_UPLOAD_ERA_LABEL);
}

// $C2:D36C, from $C2:D39E
/* The slot's header: its window with the time played, and the era label,
 * loaded when it isn't the one already there, drawn and queued. */
static void draw_header(GameState *g, Platform *pf)
{
    draw_header_window(g, pf);
    uint8_t era = slot_era(g);
    if (era != menu_save_slot_era(g))
        set_menu_save_era_width(g, set_era_label(g, era));
    finish_header(g);
}

// $C2:D36C, from $C2:D404
/* A row for each member in the party: the member's record where the window
 * command list draws it from, and the portrait, one row of tilemap below the
 * other. The list draws at the drawing origin; the loop keeps its own place,
 * where the original reads it back from the origin's cell after each row. */
/* The row's cells, which the portrait's own routine ($C2:D466) takes its row
 * and member from. */
static void note_member_row(GameState *g, unsigned row, uint8_t member)
{
    set_menu_scratch_save_member_record(g, (uint16_t)(SUMMARY_AT + row * MEMBER_RECORD_BYTES));
    set_menu_save_member_row(g, (uint16_t)row);
    set_menu_scratch_save_member_character(g, member);
}

static void draw_member_rows(GameState *g, Platform *pf)
{
    uint16_t origin = MEMBER_ROWS_AT;
    set_menu_draw_origin(g, origin);
    for (unsigned row = 0; row < MEMBER_ROWS; row++) {
        uint8_t member = menu_save_summary_byte(g, row * MEMBER_RECORD_BYTES);
        note_member_row(g, row, member);
        if (member_present(g, row)) {
            show_member(g, row);
            menu_draw_windows(g, pf, MEMBER_ROW_LIST);
            TIMED_AS(g, 0xC2D466, draw_member_portrait(g, origin, row, member));
            origin = (uint16_t)(origin + MEMBER_ROW_BYTES);
            set_menu_draw_origin(g, origin);
        }
    }
    set_menu_scratch_save_member_record(g, (uint16_t)(SUMMARY_AT + MEMBER_ROWS * MEMBER_RECORD_BYTES));
    set_menu_save_member_row(g, MEMBER_ROWS);
}

// $C2:D410
/* A slot drawn without its header forgets the era label's width, so the
 * next header drawn over the same era draws no label until one is loaded:
 * the original clears the row count a word wide, over the width after it. */
static void forget_era_label(GameState *g) { set_menu_save_era_width(g, 0); }

/* The drawn slot asks for the palette's upload: the portraits are in it. */
static void palette_changed(GameState *g) { set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1)); }

// $C2:D36C
/* Draw the summary of the slot shown, a slot that holds a game: nothing for
 * the fourth row or an empty slot. */
void menu_draw_the_slot_shown(GameState *g, Platform *pf)
{
    set_menu_member_marked(g, 0);
    uint8_t slot = menu_save_slot_shown(g);
    if (slot >= SAVE_SLOTS || menu_save_slot_in_use(g, slot) == 0)
        return;
    copy_summary(g, slot);
    if (menu_save_header_taken(g) == 0)
        draw_header(g, pf);
    else
        forget_era_label(g);
    draw_member_rows(g, pf);
    palette_changed(g);
}

/* The slots the cursor moves between, and the cursor's moves this frame. */
enum { SLOT_ROWS = 4, ROW_MASK = SLOT_ROWS - 1, FOURTH_ROW = 3 };
enum { MOVED_RIGHT = 0x01, MOVED_LEFT = 0x02, MOVED_UP = 0x08 };
/* The fourth row's two choices are $70 apart across the screen. */
enum { FOURTH_ROW_CHOICES_APART = 0x70 };

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
    for (unsigned i = 0; i < SLOT_TILEMAP_BYTES / 2; i++)
        set_menu_save_slot_tilemap(g, i, 0);
    for (unsigned row = 0; row < ERA_LABEL_ROWS; row++)
        for (unsigned i = 0; i < ERA_LABEL_ROW_BYTES; i++)
            set_menu_era_label_tiles(g, row, i, 0);
}

/* The rows' effect with the slot's row marked, and its tilemap cleared for
 * its summary. */
static void make_room_for_the_slot(GameState *g)
{
    TIMED_AS(g, 0xC2CFFB, menu_put_slot_rows_effect(g));
    clear_slot_tilemap(g);
}

/* The slot's tilemap and the era label queued for the next vblank. */
static void queue_the_slot(GameState *g, const uint16_t order[3])
{
    for (unsigned i = 0; i < 3; i++)
        menu_queue_fixed_upload(g, order[i]);
}

/* The two screens queue the same three uploads, the slot's tilemap in three
 * parts, and only the order of the first two differs. Nothing on screen tells
 * the orders apart: the vblank sends the whole queue at once
 * (menu_send_upload_queue), and the three parts go to places of their own in
 * video memory, none overlapping another. The orders are the original's two
 * routines', kept because the queue's records in work RAM are compared. */
static const uint16_t LOAD_SCREEN_UPLOADS[3] = { MENU_UPLOAD_SLOT_TILEMAP, MENU_UPLOAD_SLOT_TILEMAP_2,
                                                 MENU_UPLOAD_SLOT_TILEMAP_3 };
static const uint16_t SAVE_SCREEN_UPLOADS[3] = { MENU_UPLOAD_SLOT_TILEMAP_2, MENU_UPLOAD_SLOT_TILEMAP,
                                                 MENU_UPLOAD_SLOT_TILEMAP_3 };

// $C2:E743
/* Show the slot the cursor is on: off the empty slots; on the fourth row, left
 * or right flips between its choices, with a tick; then the slot rows'
 * effect, and the slot's summary drawn and queued. */
bool menu_show_save_slot_with(GameState *g, Platform *pf)
{
    uint8_t slot = skip_empty_slots(g, menu_cursor_index(g));
    set_menu_cursor_index(g, slot);
    bool finished = saves_finished(pf, PF_AT(0xC2E75D));
    if (finished && slot == FOURTH_ROW && (menu_cursor_moves(g) & (MOVED_RIGHT | MOVED_LEFT))) {
        uint8_t x = (uint8_t)(menu_cursor_places_x(g, FOURTH_ROW) ^ FOURTH_ROW_CHOICES_APART);
        set_menu_cursor_places_x(g, FOURTH_ROW, x);
        menu_tick(g, pf);
    }
    set_menu_save_slot_shown(g, slot);
    set_menu_save_slot_marked(g, (uint8_t)((slot + 1) & ROW_MASK));
    make_room_for_the_slot(g);
    TIMED_AS(g, 0xC2D36C, menu_draw_the_slot_shown(g, pf));
    queue_the_slot(g, LOAD_SCREEN_UPLOADS);
    return true;
}

/* ---- The save screen's frame ------------------------------------------------------------------- */

/* The save screen shows the slot under the cursor, which it keeps as the
 * slot both shown and marked in the rows' effect (the load screen marks the
 * one after). The slot marked until now, which the cursor has come from if
 * it isn't this one. */
static uint8_t show_the_slot_under_the_cursor(GameState *g, uint8_t slot)
{
    uint8_t was = menu_save_slot_marked(g);
    set_menu_save_slot_shown(g, slot);
    set_menu_save_slot_marked(g, slot);
    return was;
}

// $C2:CFA2
/* Every frame the save screen waits for a choice, the slot under the cursor
 * drawn afresh: a tick where the cursor came to it from another slot, the
 * slot's row marked, its summary drawn when it holds a game, and the whole
 * queued. */
bool menu_move_among_the_slots_with(GameState *g, Platform *pf)
{
    uint8_t slot = menu_cursor_index(g);
    uint8_t was = show_the_slot_under_the_cursor(g, slot);
    if (slot != was) {
        menu_tick(g, pf);
    }
    make_room_for_the_slot(g);
    if (menu_save_slot_in_use(g, slot) != 0)
        TIMED_AS(g, 0xC2D36C, menu_draw_the_slot_shown(g, pf));
    queue_the_slot(g, SAVE_SCREEN_UPLOADS);
    return true;
}

/* ---- Reading the slots as the menus open ------------------------------------------------------ */

/* A saved game on the cartridge, from its slot's start: the party's places,
 * each character's record ($50 bytes; the summary takes its first nine and
 * the word at $12), the characters' names (six bytes each), how many times
 * the game was saved and finished, the second byte of the settings, the time
 * played and the rest of the header's block, the location, the chapter the
 * story is in, and a flag that makes the summary show no gold. */
enum {
    SAVED_CHARACTERS = 0x200, CHARACTER_BYTES = 0x50, SAVED_FIGURE = 0x12, SAVED_RECORD_START = 9,
    SAVED_PARTY = 0x580, SAVED_SETTINGS_SECOND = 0x591, SAVED_TIMES_SAVED = 0x59C, SAVED_TIMES_FINISHED = 0x59E,
    SAVED_NAMES = 0x5B0, NAME_BYTES = 6, SAVED_HEADER_BLOCK = 0x5E0, HEADER_BLOCK_BYTES = 10,
    SAVED_LOCATION = 0x5F3, SAVED_CHAPTER = 0x603, SAVED_FLAGS = 0x793,
};
enum { LOCATION_BITS = 0x01FF, MOST_TIMES_SAVED = 999, NO_GOLD_SHOWN = 0x80, EMPTY_RECORD = 0xFF };

/* Where the slot the save code has just checked keeps its game, in the save
 * memory: the check leaves it for the summary's reading. */
/* The span of a game the summary reads from, which its check keeps within
 * save memory. */
enum { SUMMARY_SPAN = 0x800 };

static uint16_t checked_game(const GameState *g) { return saves_game_in_memory(menu_slot_game_at(g)); }

static uint8_t saved_byte(Platform *pf, uint16_t game, uint16_t at) { return sv_read8(pf, PF_AT(0xC2D1C1), (uint16_t)(game + at)); }
/* A member's place in the saved party: the original steps from the game's
 * start a byte at a time, which wraps within its page; the port keeps that. */
static uint16_t party_place(uint16_t game, unsigned row)
{
    return (uint16_t)(SAVED_PARTY + ((game & 0xFF00) | (uint8_t)(game + row)));
}

static uint8_t saved_member(Platform *pf, uint16_t game, unsigned row) { return saved_byte(pf, 0, party_place(game, row)); }

static uint16_t saved_word(Platform *pf, uint16_t game, uint16_t at)
{
    return (uint16_t)(saved_byte(pf, game, at) | saved_byte(pf, game, (uint16_t)(at + 1)) << 8);
}

/* The summary's header, after the members: times saved, the location, the
 * header block (its gold at 4, three bytes, and the time played's digits at
 * 9), times finished. */
enum {
    HEADER_TIMES_SAVED = 0,
    HEADER_LOCATION = 2,
    HEADER_BLOCK = 4,
    HEADER_GOLD = 4,
    GOLD_BYTES = 3,
    HEADER_TIMES_FINISHED = 14,
};

static void put_summary_byte(GameState *g, uint8_t slot, unsigned at, uint8_t v)
{
    if (at < SUMMARY_MEMBER_BYTES)
        set_save_slot_summaries_members_byte(g, slot, at, v);
    else
        set_save_slot_summaries_header_byte(g, slot, at - SUMMARY_MEMBER_BYTES, v);
}
static void put_summary_word(GameState *g, uint8_t slot, unsigned at, uint16_t v)
{
    put_summary_byte(g, slot, at, (uint8_t)v);
    put_summary_byte(g, slot, at + 1, (uint8_t)(v >> 8));
}

// $C2:D19F
/* A slot's summary from its game on the cartridge. A member's record takes
 * the word at $12 of the character's record at its fifteenth byte, a word
 * wide, which the next record's first byte (or the header's) then takes back. */
static void read_summary(GameState *g, Platform *pf, uint8_t slot)
{
    uint16_t game = checked_game(g);
    for (unsigned row = 0; row < MEMBER_ROWS; row++) {
        unsigned record = row * MEMBER_RECORD_BYTES;
        put_summary_byte(g, slot, record, EMPTY_RECORD);
        uint8_t character = saved_member(pf, game, row);
        if (character & NO_MEMBER)
            continue;
        uint16_t from = (uint16_t)(SAVED_CHARACTERS + character * CHARACTER_BYTES);
        put_summary_word(g, slot, record + STATUS_LAST, saved_word(pf, game, (uint16_t)(from + SAVED_FIGURE)));
        for (unsigned i = 0; i < SAVED_RECORD_START; i++)
            put_summary_byte(g, slot, record + i, saved_byte(pf, game, (uint16_t)(from + i)));
        for (unsigned i = 0; i < NAME_BYTES; i++)
            put_summary_byte(g, slot, record + SAVED_RECORD_START + i,
                             saved_byte(pf, game, (uint16_t)(SAVED_NAMES + character * NAME_BYTES + i)));
    }
    unsigned header = SUMMARY_MEMBER_BYTES;
    uint16_t saved = saved_word(pf, game, SAVED_TIMES_SAVED);
    put_summary_word(g, slot, header + HEADER_TIMES_SAVED, saved > MOST_TIMES_SAVED ? MOST_TIMES_SAVED : saved);
    put_summary_word(g, slot, header + HEADER_LOCATION, (uint16_t)(saved_word(pf, game, SAVED_LOCATION) & LOCATION_BITS));
    for (unsigned i = 0; i < HEADER_BLOCK_BYTES; i++)
        put_summary_byte(g, slot, header + HEADER_BLOCK + i, saved_byte(pf, game, (uint16_t)(SAVED_HEADER_BLOCK + i)));
    put_summary_word(g, slot, header + HEADER_TIMES_FINISHED, saved_word(pf, game, SAVED_TIMES_FINISHED));
    if (saved_byte(pf, game, SAVED_FLAGS) & NO_GOLD_SHOWN)
        for (unsigned i = 0; i < GOLD_BYTES; i++)
            put_summary_byte(g, slot, header + HEADER_GOLD + i, 0);
}

static void charge_summary_read(GameState *g, Platform *pf)
{
    uint16_t game = checked_game(g);
    for (unsigned row = 0; row < MEMBER_ROWS; row++)
        (void)saved_member(pf, game, row);
}

/* ---- The labels --------------------------------------------------------------------------------- */

/* The labels are set in the dialog's font by the typesetter's label setting
 * (menu_set_label, $C2:F90C) from a table of strings into the slots' labels'
 * graphic (menu_data.h): the slots' names ($FF:D3FE, the first slot's $0E),
 * and the chapters' (the empty slot's the one after the last chapter,
 * $FF:D03E). */
enum { SLOT_NAMES = 0xFFD3FE, FIRST_SLOT_NAME = 0x0E };
enum { CHAPTER_LABELS = 0xFFD03E, EMPTY_SLOT_LABEL = MENU_CHAPTERS };

/* Set a label of `table` into its place at `at`, noted as where the menus'
 * graphic goes. */
static void set_label_at(GameState *g, uint32_t table, uint8_t entry, uint16_t at)
{
    set_menu_load_destination(g, at);
    TIMED_AS(g, 0xC2F90C, (void)menu_set_label(g, table, entry, at));
}

// $C2:D306
/* A two-tile label moved two pixels right in its tiles: each line of its
 * left tile and its right tile, both bitplanes, and its lower tiles too. */
enum { TILE_LINE_BYTES = 16, RIGHT_TILE = 0x10, LOWER_TILES = 0x100, PIXELS_A_MOVE = 2 };

static void move_label_right(GameState *g, uint16_t at)
{
    static const uint16_t halves[2] = { 0, LOWER_TILES };
    for (unsigned half = 0; half < 2; half++)
        for (unsigned i = 0; i < TILE_LINE_BYTES; i++) {
            uint32_t left = 0x7E0000u + (uint16_t)(at + halves[half] + i), right = left + RIGHT_TILE;
            uint16_t line = (uint16_t)(wram_read8(g, left) << 8 | wram_read8(g, right));
            line = (uint16_t)(line >> PIXELS_A_MOVE);
            wram_write8(g, left, (uint8_t)(line >> 8));
            wram_write8(g, right, (uint8_t)line);
        }
}

/* The chapter the story was in when a game was saved: the last whose start
 * the story's progress has reached. */
static uint8_t chapter_of(const GameState *g, uint8_t progress)
{
    uint8_t chapter = MENU_CHAPTERS;
    do
        chapter--;
    while (progress < menu_chapter_start(g, chapter));
    return chapter;
}

/* The window design is the low bits of the settings' second byte. */
enum { DESIGN_BITS = 0x07 };

/* ---- The labels and summaries as the menus open ------------------------------------------------- */

/* The routines reading the slots call as their own -- a slot's chapter label,
 * its name label, its summary, its window design and chapter -- each run
 * only on the state it handles (slot_routine, below), false where it went
 * no further. */
typedef enum { CHAPTER_LABEL, SLOT_NAME, SLOT_SUMMARY, DESIGN_AND_CHAPTER } SlotRoutine;
static bool slot_routine(GameState *g, Platform *pf, SlotRoutine routine);

// $C2:D296
/* A slot's chapter label (or the empty slot's, whichever its caller asked
 * for), drawn into its place, cleared first. */
static void draw_chapter_label(GameState *g)
{
    uint16_t at = (uint16_t)(MENU_CHAPTER_LABELS_AT + (menu_save_slot_shown(g) & 3) * MENU_CHAPTER_LABEL_BYTES);
    wram_fill8(g, 0x7E0000u | at, MENU_CHAPTER_LABEL_BYTES, 0);
    charge_block_move(g, MENU_CHAPTER_LABEL_BYTES);
    set_label_at(g, CHAPTER_LABELS, menu_text_string(g), at);
}

// $C2:D2C4
/* A slot's name label, drawn and moved right to sit in its place. */
static void draw_slot_name(GameState *g)
{
    uint8_t slot = menu_save_slot_shown(g);
    uint16_t at = (uint16_t)(MENU_SLOT_NAMES_AT + slot * MENU_SLOT_NAME_BYTES);
    set_label_at(g, SLOT_NAMES, (uint8_t)(FIRST_SLOT_NAME + slot), at);
    /* a shift of 0 is taken as 256 moves */
    uint8_t moves = menu_slot_name_shift(g, slot);
    do
        move_label_right(g, at);
    while (--moves != 0);
}

// $C2:D266
/* A slot with a game: the window design it was saved with, and its chapter's
 * label. */
static bool read_design_and_chapter(GameState *g, Platform *pf)
{
    uint8_t slot = menu_save_slot_shown(g);
    uint16_t game = checked_game(g);
    set_menu_save_slot_design(g, slot, (uint8_t)(saved_byte(pf, game, SAVED_SETTINGS_SECOND) & DESIGN_BITS));
    uint8_t progress = saved_byte(pf, game, SAVED_CHAPTER);
    uint8_t chapter = chapter_of(g, progress);
    set_menu_text_string(g, chapter);
    return slot_routine(g, pf, CHAPTER_LABEL);
}

// $C2:D19F
/* A slot with a game: its summary, then its design and chapter. */
static bool read_slot_summary(GameState *g, Platform *pf)
{
    uint8_t slot = menu_save_slot_shown(g);
    read_summary(g, pf, slot);
    /* the drawing origin left on the summary's header, past its members,
     * where the reading got to: the cell the window lists draw from */
    set_menu_draw_origin(g, (uint16_t)(RAM_ABS(RAM_SAVE_SLOT_SUMMARIES) + slot * SUMMARY_BYTES + SUMMARY_MEMBER_BYTES));
    charge_summary_read(g, pf);
    return slot_routine(g, pf, DESIGN_AND_CHAPTER);
}

enum { NO_FOURTH_ROW_YET = 0xFF };

// $C2:D156
/* The three slots, as the menus open: checked, their summaries and labels,
 * and the labels queued. */
static bool read_save_slots(GameState *g, Platform *pf)
{
    for (uint8_t slot = 0; slot < MENU_SAVE_SLOTS; slot++)
        set_menu_save_slot_design(g, slot, 0);
    set_menu_save_slot_in_use(g, FOURTH_ROW, NO_FOURTH_ROW_YET);
    for (uint8_t slot = 0; slot < MENU_SAVE_SLOTS; slot++) {
        set_menu_save_slot_shown(g, slot);
        set_menu_save_slot_in_use(g, slot, 0);
        SavedGameCheck check = SAVED_GAME_NONE;
        TIMED_AS(g, 0xFFF9C4, check = saves_check_slot_noting(g, pf, SAVE_CHECK_MENU, slot));
        bool holds_a_game = check == SAVED_GAME_HELD;
        if (holds_a_game) {
            set_menu_save_slot_in_use(g, slot, 1);
            if (!slot_routine(g, pf, SLOT_SUMMARY))
                return false;
        } else {
            set_menu_text_string(g, EMPTY_SLOT_LABEL);
            if (!slot_routine(g, pf, CHAPTER_LABEL))
                return false;
        }
        if (!slot_routine(g, pf, SLOT_NAME))
            return false;
    }
    set_menu_save_slot_shown(g, MENU_SAVE_SLOTS);   /* past the last, as the count leaves it */
    TIMED_AS(g, 0xC28385, menu_queue_upload(g, menu_upload_record(g, MENU_UPLOAD_SLOT_NAMES)));
    return true;
}

static bool chapter_label_drawable(const GameState *g);
static bool labels_drawable(const GameState *g);
static bool summary_readable(const GameState *g, Platform *pf);
static bool slots_readable(const GameState *g);

/* Each routine, run only on the state it handles, timed as its own;
 * otherwise the program goes no further there. */
static const struct {
    uint32_t routine;
    const char *what;
} slot_routines[] = {
    [CHAPTER_LABEL] = { 0xC2D296, "a chapter label the port doesn't draw" },
    [SLOT_NAME] = { 0xC2D2C4, "a slot's name the port doesn't draw" },
    [SLOT_SUMMARY] = { 0xC2D19F, "a slot's summary the port doesn't read" },
    [DESIGN_AND_CHAPTER] = { 0xC2D266, "a slot's summary the port doesn't read" },
};

static bool slot_routine_takes(const GameState *g, Platform *pf, SlotRoutine routine)
{
    switch (routine) {
    case CHAPTER_LABEL: return chapter_label_drawable(g);
    case SLOT_NAME: return labels_drawable(g);
    case SLOT_SUMMARY:
    case DESIGN_AND_CHAPTER: return summary_readable(g, pf);
    }
    return false;
}

static bool slot_routine(GameState *g, Platform *pf, SlotRoutine routine)
{
    if (!slot_routine_takes(g, pf, routine)) {
        program_breaks_a_contract(g, slot_routines[routine].routine, slot_routines[routine].what);
        return false;
    }
    bool back = true;
    switch (routine) {
    case CHAPTER_LABEL: TIMED_AS(g, 0xC2D296, draw_chapter_label(g)); break;
    case SLOT_NAME: TIMED_AS(g, 0xC2D2C4, draw_slot_name(g)); break;
    case SLOT_SUMMARY: TIMED_AS(g, 0xC2D19F, back = read_slot_summary(g, pf)); break;
    case DESIGN_AND_CHAPTER:
        TIMED_AS(g, 0xC2D266, back = read_design_and_chapter(g, pf));
        break;
    }
    return back;
}

/* A slot's summary read back after a save, where the state is one it
 * handles. */
bool menu_slot_summary_read_back(GameState *g, Platform *pf)
{
    return slot_routine(g, pf, SLOT_SUMMARY);
}

bool menu_save_slots_read(GameState *g, Platform *pf)
{
    if (!slots_readable(g)) {
        program_breaks_a_contract(g, 0xC2D156, "save slots the port doesn't read");
        return false;
    }
    return read_save_slots(g, pf);
}

// $C2:D28D
/* The empty slot's label, drawn in the chapter label's place; false where
 * the slot shown is none it handles, the program going no further there. */
bool menu_draw_empty_label(GameState *g)
{
    if (!labels_drawable(g)) {
        program_breaks_a_contract(g, 0xC2D28D, "an empty slot's label the port doesn't draw");
        return false;
    }
    set_menu_text_string(g, EMPTY_SLOT_LABEL);
    draw_chapter_label(g);
    return true;
}

/* ---- What the steps need ----------------------------------------------------------------------- */

/* What the game gives the load screen's routines: a cursor on one of the four rows and at least one slot with a game
 * (else the skip of empty slots goes round for ever), whole records in the
 * upload queue, a palette change the drawing can count, numbers converted
 * from the first power of two (the word the conversion starts from is 0
 * whenever the menus draw, menu/text.c), and summaries whose members are
 * characters and whose time played is in decimal digits. Looked at without
 * charging time. */

static bool members_are_characters(const GameState *g, uint8_t slot)
{
    for (unsigned row = 0; row < MEMBER_ROWS; row++) {
        uint8_t member = save_slot_summaries_members_byte(g, slot, row * MEMBER_RECORD_BYTES);
        if (!(member & NO_MEMBER) && member >= MENU_PORTRAITS)
            return false;
    }
    return true;
}

/* The time played the summary's header shows, where its copy is drawn from:
 * digits the clock keeps (menu/text.c), which the cartridge's games have. */
static bool time_is_decimal(const GameState *g, uint8_t slot)
{
    uint16_t summary = (uint16_t)(RAM_ABS(RAM_SAVE_SLOT_SUMMARIES) + slot * SUMMARY_BYTES);
    return menu_time_digits_are_decimal(g, (uint16_t)(summary + (TIME_PLAYED_DIGITS - SUMMARY_AT)));
}

/* The place the party is in, which the summary keeps as the reading masks it
 * (to a location's number): the era's lookup by it stays in its table. */
static bool location_is_a_place(const GameState *g, uint8_t slot)
{
    uint16_t location = (uint16_t)(save_slot_summaries_header_byte(g, slot, HEADER_LOCATION)
                                   | save_slot_summaries_header_byte(g, slot, HEADER_LOCATION + 1) << 8);
    return location <= LOCATION_BITS;
}

static bool slot_is_drawable(const GameState *g, uint8_t slot)
{
    return slot >= SAVE_SLOTS || menu_save_slot_in_use(g, slot) == 0
           || (members_are_characters(g, slot) && time_is_decimal(g, slot) && location_is_a_place(g, slot));
}

static bool drawing_counts(const GameState *g)
{
    return menu_vram_queue_bytes(g) % 8 == 0 && menu_palette_changes_have_room(g) && menu_zero(g) == 0;
}

bool menu_save_slots_can_show(const GameState *g)
{
    bool any = false, drawable = true;
    for (uint8_t slot = 0; slot < SLOT_ROWS; slot++) {
        any = any || menu_save_slot_in_use(g, slot) != 0;
        drawable = drawable && slot_is_drawable(g, slot);
    }
    return any && drawable && drawing_counts(g);
}

bool menu_load_screen_slot_can_show(const GameState *g)
{
    return menu_cursor_index(g) < SLOT_ROWS && menu_save_slots_can_show(g);
}

/* The save screen's frame: a cursor on one of the four rows, the slot read
 * as a byte (the original reads it a word wide with the cell after it, which
 * the slots' reading clears as the menus open), a summary the drawing can
 * take, and the drawing's counts. */
bool menu_slot_under_the_cursor_shows(const GameState *g)
{
    vars_peek(true);
    uint8_t slot = menu_cursor_index(g);
    bool ok = slot < SLOT_ROWS && menu_after_slot_shown(g) == 0 && slot_is_drawable(g, slot) && drawing_counts(g);
    vars_peek(false);
    return ok;
}

/* The slots' reading starts its count of the slots from the menus' zero
 * word (which the game keeps 0); each label and summary is of one of the three slots. */
static bool slots_readable(const GameState *g) { return menu_zero(g) == 0; }

static bool labels_drawable(const GameState *g) { return menu_save_slot_shown(g) < SAVE_SLOTS; }

/* The chapter label its caller named, one the label setting can take. */
static bool chapter_label_drawable(const GameState *g)
{
    uint16_t at = (uint16_t)(MENU_CHAPTER_LABELS_AT + (menu_save_slot_shown(g) & 3) * MENU_CHAPTER_LABEL_BYTES);
    return labels_drawable(g) && menu_label_can_be_set(g, CHAPTER_LABELS, menu_text_string(g), at);
}

/* A summary is read from a game the check found within the save memory,
 * whose party holds characters (the save memory looked at without charging
 * time), and with the menus' zero word 0: the gold
 * a summary hides is written from it. */
static bool saved_party_is_characters(Platform *pf, uint16_t at)
{
    for (unsigned row = 0; row < MEMBER_ROWS; row++) {
        uint8_t member = sv_peek8(pf, party_place(saves_game_in_memory(at), row));
        if (!(member & NO_MEMBER) && member >= MENU_PORTRAITS)
            return false;
    }
    return true;
}

static bool summary_readable(const GameState *g, Platform *pf)
{
    uint16_t at = menu_slot_game_at(g);
    return labels_drawable(g) && menu_zero(g) == 0 && at >= SAVES_ON_THE_BUS
           && (uint32_t)at + SUMMARY_SPAN <= SAVES_ON_THE_BUS + SAVE_SIZE && saved_party_is_characters(pf, at);
}
