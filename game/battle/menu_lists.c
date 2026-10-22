/* Battle: the menu's two lists laid out as tiles (battle/choosing.h).
 *
 * The tech list is laid out in a tilemap of its own (battle_menu_list_tiles),
 * which the tech page copies into the menu window each frame (menu_window.c),
 * shading the techs that can't be used: four rows from the member's scroll,
 * two lines a row. A tech's row is its name in two lines of tiles; the
 * sections of combination techs start with a heading row; a blank row is
 * blank. Every cell is laid out shaded; the page lights what can be used.
 *
 * The item list is laid out straight into the battle window, under the party,
 * three rows from the item the list is scrolled to: each item the party holds
 * with its name, how many there are, and a word after them. An item only
 * some members may use is shaded for the others.
 *
 * Laying out works in the original's direct-page cells, and converts names in
 * its name buffer; their final values are left as scaffolding at the end of
 * each (the leave_* functions). */
#include "battle/choosing.h"

#include <string.h>

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/setup_data.h"
#include "battle/window_data.h"
#include "original_effects.h"
#include "vars.h"

/* ---- The tech list ------------------------------------------------------------------------------- */

/* The list's entries: a tech's id, or one of the marks (choosing.h's list of
 * twenty entries a member, menu_window.c's view of the same). */
enum {
    UNNAMED_ENTRY = 0xFB,
    DOUBLE_TECHS_HEADING = 0xFC,
    TRIPLE_TECHS_HEADING = 0xFD,
    FIRST_BLANK_ENTRY = 0xFE,
};

bool battle_tech_entry_is_a_tech(uint8_t entry) { return entry < UNNAMED_ENTRY; }

static bool is_tech(uint8_t entry) { return battle_tech_entry_is_a_tech(entry); }

/* Four rows are laid out, the three the page shows and the one below. A row
 * is two lines of the list's tilemap; its parts are in columns: a gap of three
 * before the name, the name's eleven, a gap of four after; a heading spans all
 * eighteen from the gap. */
enum { ROWS_LAID_OUT = 4, GAP_COLUMN = 3, GAP_CELLS = 3, NAME_COLUMN = 6, AFTER_NAME_COLUMN = 17, AFTER_NAME_CELLS = 4 };

static void list_put(GameState *g, unsigned line, unsigned column, uint8_t tile)
{
    WindowCell cell = window_cell(line, column);
    set_battle_menu_list_tiles(g, cell, tile);
    set_battle_menu_list_attributes(g, cell, TEXT_SHADED);
}

static void list_blank(GameState *g, unsigned line, unsigned column, unsigned cells)
{
    for (unsigned i = 0; i < cells; i++)
        list_put(g, line, column + i, TILE_BLANK);
}

/* A tech's name, in the buffer as the original puts it there: its eleven
 * characters, the end, and the buffer's blanks after. */
static void tech_name_text(const GameState *g, uint8_t tech, uint8_t text[NAME_TILES])
{
    for (unsigned i = 0; i < NAME_TILES; i++)
        text[i] = TILE_BLANK;
    for (unsigned i = 0; i < BATTLE_NAME_CHARACTERS; i++)
        text[i] = battle_tech_name_char(g, tech, i);
    text[BATTLE_NAME_CHARACTERS] = NAME_END;
}

static void lay_out_tech_row(GameState *g, unsigned row, uint8_t entry)
{
    unsigned marks_line = 2 * row, glyphs_line = marks_line + 1;
    list_blank(g, marks_line, GAP_COLUMN, GAP_CELLS);
    list_blank(g, glyphs_line, GAP_COLUMN, GAP_CELLS);
    if (entry == DOUBLE_TECHS_HEADING || entry == TRIPLE_TECHS_HEADING) {
        for (unsigned i = 0; i < BATTLE_TECH_HEADING_TILES; i++)
            list_put(g, glyphs_line, GAP_COLUMN + i, battle_tech_heading_tile(g, entry == TRIPLE_TECHS_HEADING, i));
        return;
    }
    if (entry >= FIRST_BLANK_ENTRY) {
        list_blank(g, glyphs_line, GAP_COLUMN, BATTLE_TECH_HEADING_TILES);
        return;
    }
    NameTiles name;
    if (is_tech(entry)) {
        uint8_t text[NAME_TILES];
        tech_name_text(g, entry, text);
        name = battle_name_tiles(text);
    } else {
        memset(&name, TILE_BLANK, sizeof name);   /* an entry with no name */
    }
    for (unsigned i = 0; i < BATTLE_NAME_CHARACTERS; i++) {
        list_put(g, glyphs_line, NAME_COLUMN + i, name.glyphs[i]);
        list_put(g, marks_line, NAME_COLUMN + i, name.marks[i]);
    }
    list_blank(g, glyphs_line, AFTER_NAME_COLUMN, AFTER_NAME_CELLS);
    list_blank(g, marks_line, AFTER_NAME_COLUMN, AFTER_NAME_CELLS);
}

/* Where a member's list starts among the lists (battle_tech_list_ids), and an
 * entry of it, counted in a byte as the original counts it. */
static uint8_t list_start(const GameState *g) { return setup_member_tech_list_start(g, battle_menu_member(g)); }

/* The cells the original lays the list out in: the list's start (over the
 * multiplier's product), where it is in the lists and its rows, in bytes of
 * the tilemap; and of the last tech it named, its id and four times it. */
enum { CELL_AT = 0x80, CELL_TILEMAP_AT = 0x82, CELL_SCROLL = 0x86, CELL_ROWS_LEFT = 0x87, CELL_ROWS_DONE = 0x88,
       CELL_ID = 0x8E, CELL_ID_TIMES_FOUR = 0x90, ROW_BYTES = 0x80 };

static void leave_tech_list_cells(GameState *g, uint8_t start, uint8_t scroll)
{
    set_battle_product(g, (uint16_t)((battle_product(g) & 0xFF00) | start));
    battle_leave_work16(g, CELL_AT, (uint8_t)(start + scroll + ROWS_LAID_OUT));
    battle_leave_work16(g, CELL_TILEMAP_AT, ROWS_LAID_OUT * ROW_BYTES);
    battle_leave_work8(g, CELL_SCROLL, scroll);
    battle_leave_work8(g, CELL_ROWS_LEFT, 0);
    battle_leave_work16(g, CELL_ROWS_DONE, (uint8_t)(scroll + ROWS_LAID_OUT));
}

/* The buffer holds what the last row put there: a tech's name converted, a
 * heading's tiles over blanks, or blanks; the converted text is the last
 * tech's. */
static void leave_tech_list_buffer(GameState *g, uint8_t last_entry, int last_tech)
{
    if (last_tech >= 0) {
        uint8_t text[NAME_TILES];
        tech_name_text(g, (uint8_t)last_tech, text);
        NameTiles name = battle_name_tiles(text);
        battle_leave_name_buffer(g, text, &name);
        battle_leave_work16(g, CELL_ID, (uint8_t)last_tech);
        battle_leave_work8(g, CELL_ID_TIMES_FOUR + 1, (uint8_t)((4 * last_tech) >> 8));
    }
    if (is_tech(last_entry))
        return;
    for (unsigned i = 0; i < BATTLE_NAME_BUFFER_COUNT; i++)
        set_battle_name_buffer(g, i, TILE_BLANK);
    if (last_entry == DOUBLE_TECHS_HEADING || last_entry == TRIPLE_TECHS_HEADING)
        for (unsigned i = 0; i < BATTLE_TECH_HEADING_TILES; i++)
            set_battle_name_buffer(g, i, battle_tech_heading_tile(g, last_entry == TRIPLE_TECHS_HEADING, i));
}

// $C1:0A88, $C1:0B0B
void battle_lay_out_tech_list(GameState *g)
{
    uint8_t start = list_start(g);
    uint8_t scroll = battle_tech_scroll(g, battle_menu_member(g));
    uint8_t entry = 0;
    int last_tech = -1;
    for (unsigned row = 0; row < ROWS_LAID_OUT; row++) {
        entry = battle_tech_list_ids(g, (uint8_t)(start + scroll + row));
        lay_out_tech_row(g, row, entry);
        if (is_tech(entry))
            last_tech = entry;
    }
    ORIGINAL_EFFECT(leave_tech_list_cells(g, start, scroll));
    ORIGINAL_EFFECT(leave_tech_list_buffer(g, entry, last_tech));
    /* The last tech's id times four stays in the cell a jump to the script's
     * point takes its lift from (battle.h). */
    if (last_tech >= 0)
        battle_leave_jump_lift(g, (uint8_t)(4 * last_tech));
}

// $C1:0AD3
void battle_mark_tech_rows(GameState *g)
{
    uint8_t start = list_start(g);
    for (unsigned i = 0; i < TECHS_A_MEMBER; i++)
        set_battle_tech_rows_selectable(g, i, is_tech(battle_tech_list_ids(g, start + i)));
    ORIGINAL_EFFECT(battle_leave_work8(g, CELL_AT, 0));
}

/* ---- The item list ------------------------------------------------------------------------------- */

/* The list's rows in the battle window: from its eighth line, a row of two
 * lines an item; six lines cleared first. A row: the name from its fourth
 * column, a colon and how many from its fifteenth, and the word "Item" from
 * its twenty-third. An item's name in the ROM starts with its icon, which
 * the list leaves out: it takes the eleven bytes after it (the last the next
 * name's icon), and shows ten of the name's tiles and eleven of the marks
 * above them. */
enum { ITEM_LIST_LINE = 7, ITEM_LIST_LINES = 6, ITEM_ROWS = 3, ITEM_NAME_COLUMN = 3, ITEM_NAME_GLYPHS = 10,
       ITEM_COLON_COLUMN = 14, ITEM_COUNT_COLUMN = 15, ITEM_WORD_COLUMN = 22, TILE_COLON = 0x5F, NAME_ICON = 1 };
static const uint8_t ITEM_WORD[] = { 0xA8, 0xCD, 0xBE, 0xC6 };

/* An item whose record's flags have the top bit is for some members only:
 * those whose bits its last byte holds (no item the list is built with has
 * it, inventory.c). */
enum { FOR_SOME_MEMBERS = 0x80 };

static bool usable_by(const GameState *g, unsigned record, uint8_t member)
{
    return !(battle_item_list_flags(g, record) & FOR_SOME_MEMBERS)
           || (battle_item_list_spare(g, record) & battle_member_bit(g, member));
}

static bool listed(const GameState *g, unsigned record)
{
    return battle_item_list_held(g, record) != 0 && battle_item_list_item(g, record) != 0;
}

/* An item's name past its icon, and the end. */
static void item_name_text(const GameState *g, uint8_t item, uint8_t text[NAME_TILES])
{
    memset(text, NAME_END, NAME_TILES);
    for (unsigned i = 0; i < BATTLE_NAME_CHARACTERS; i++)
        text[i] = battle_item_name_char(g, item, NAME_ICON + i);
}

static void lay_out_item_row(GameState *g, unsigned row, unsigned record, uint8_t attributes)
{
    unsigned marks_line = ITEM_LIST_LINE + 2 * row, glyphs_line = marks_line + 1;
    uint8_t text[NAME_TILES];
    item_name_text(g, battle_item_list_item(g, record), text);
    NameTiles name = battle_name_tiles(text);
    for (unsigned i = 0; i < ITEM_NAME_GLYPHS; i++)
        window_put(g, window_cell(glyphs_line, ITEM_NAME_COLUMN + i), name.glyphs[i], attributes);
    for (unsigned i = 0; i < BATTLE_NAME_CHARACTERS; i++)
        window_put(g, window_cell(marks_line, ITEM_NAME_COLUMN + i), name.marks[i], attributes);
    window_put(g, window_cell(glyphs_line, ITEM_COLON_COLUMN), TILE_COLON, attributes);
    Digits held = battle_digits_blanked(g, battle_digits_of(g, battle_item_list_held(g, record), 2));
    window_put(g, window_cell(glyphs_line, ITEM_COUNT_COLUMN), held.tens, attributes);
    window_put(g, window_cell(glyphs_line, ITEM_COUNT_COLUMN + 1), held.ones, attributes);
    for (unsigned i = 0; i < sizeof ITEM_WORD; i++)
        window_put(g, window_cell(glyphs_line, ITEM_WORD_COLUMN + i), ITEM_WORD[i], attributes);
}

/* The cells the original lays the list out in: the member, the record's place
 * (five bytes a record), the window's place in bytes, the rows done; and of
 * the last item it listed, the item, four times it and its attributes (the
 * list's top in the item's cell where it listed none). */
enum { CELL_RECORD_AT = 0x80, CELL_WINDOW_AT = 0x82, CELL_ITEM = 0x84, CELL_ROW = 0x86, CELL_ITEM_TIMES_FOUR = 0x8E,
       CELL_MEMBER = 0x96, CELL_ATTRIBUTES = 0x98 };

/* The buffer holds the last item's name converted. The original copies the
 * name in without blanking the buffer first, so the text it copies out has,
 * past the name's end, what the buffer held: blanks after an item converted
 * before it in the same list, else what the last conversion left. */
static void leave_item_list_buffer(GameState *g, int last_item, unsigned listed)
{
    if (last_item < 0)
        return;
    uint8_t text[NAME_TILES];
    for (unsigned i = 0; i < NAME_TILES; i++)
        text[i] = listed > 1 ? TILE_BLANK : battle_name_buffer(g, i);
    for (unsigned i = 0; i < BATTLE_NAME_CHARACTERS; i++)
        text[i] = battle_item_name_char(g, (uint8_t)last_item, NAME_ICON + i);
    text[BATTLE_NAME_CHARACTERS] = NAME_END;
    NameTiles name = battle_name_tiles(text);
    battle_leave_name_buffer(g, text, &name);
}

static void leave_item_list_cells(GameState *g, uint8_t top, uint8_t member, int last_item, uint8_t attributes)
{
    battle_leave_work16(g, CELL_MEMBER, member);
    battle_leave_work16(g, CELL_RECORD_AT, (uint16_t)((top + ITEM_ROWS) * ITEM_LIST_RECORD_BYTES));
    battle_leave_work16(g, CELL_WINDOW_AT, ITEM_ROWS * ROW_BYTES);
    battle_leave_work16(g, CELL_ROW, ITEM_ROWS);
    if (last_item < 0) {
        battle_leave_work16(g, CELL_ITEM, top);
        return;
    }
    battle_leave_work16(g, CELL_ITEM, (uint16_t)last_item);
    battle_leave_work16(g, CELL_ITEM_TIMES_FOUR, (uint16_t)(4 * last_item));
    battle_leave_work8(g, CELL_ATTRIBUTES, attributes);
}

// $C1:095D, $C1:09B0
void battle_lay_out_item_list(GameState *g, uint8_t top)
{
    uint8_t member = battle_menu_member(g);
    for (unsigned line = ITEM_LIST_LINE; line < ITEM_LIST_LINE + ITEM_LIST_LINES; line++)
        for (unsigned column = 0; column < WINDOW_COLUMNS; column++)
            window_put(g, window_cell(line, column), TILE_NOTHING, 0);
    int last_item = -1;
    unsigned rows_listed = 0;
    uint8_t attributes = 0;
    for (unsigned row = 0; row < ITEM_ROWS; row++) {
        unsigned record = top + row;
        if (!listed(g, record))
            continue;
        attributes = usable_by(g, record, member) ? TEXT_PLAIN : TEXT_SHADED;
        lay_out_item_row(g, row, record, attributes);
        last_item = battle_item_list_item(g, record);
        rows_listed++;
    }
    ORIGINAL_EFFECT(leave_item_list_cells(g, top, member, last_item, attributes));
    ORIGINAL_EFFECT(leave_item_list_buffer(g, last_item, rows_listed));
}

/* ---- The entries --------------------------------------------------------------------------------- */

/* They work for the member whose menu is open; an item list from the top the
 * original's caller hands over in a cell. What they leave in the registers no
 * caller reads (the registry's dont_care). */
void battle_lay_out_tech_list_entry(Snes *s)
{
    battle_lay_out_tech_list(state_of(s));
    m_rts(s);
}

/* The top the original's callers hand the item list in its first cell (a
 * byte: it clears the one after itself). */
static uint8_t item_list_top_handed(const GameState *g) { return (uint8_t)battle_work16(g, CELL_RECORD_AT); }

/* The item list leaves A's high byte clear and the carry of its last
 * row's count. */
void battle_lay_out_item_list_entry(Snes *s)
{
    GameState *g = state_of(s);
    battle_lay_out_item_list(g, item_list_top_handed(g));
    s->cpu.a &= 0x00FF;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* The lists as the layouts take them on trust: the member choosing one of the
 * three, its list's entries inside the lists, and an item list's rows inside
 * its records. The tech list's layout finds the member's list, and the item
 * list leaves the member, with the whole of A, whose high byte is clear
 * wherever the menu runs; the rows' marking clears it itself. */
bool battle_tech_list_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t member = battle_menu_member(g);
    bool fits = member < BATTLE_PARTY_SIZE
                && setup_member_tech_list_start(g, member) + battle_tech_scroll(g, member) + ROWS_LAID_OUT
                       <= BATTLE_TECH_LIST_IDS_COUNT;
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && fits;
}

bool battle_tech_rows_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t member = battle_menu_member(g);
    bool fits = member < BATTLE_PARTY_SIZE
                && setup_member_tech_list_start(g, member) + TECHS_A_MEMBER <= BATTLE_TECH_LIST_IDS_COUNT;
    vars_peek(false);
    return battle_on_page(s) && fits;
}

bool battle_item_list_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool fits = item_list_top_handed(g) + ITEM_ROWS <= BATTLE_ITEM_LIST_COUNT;
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && fits;
}
