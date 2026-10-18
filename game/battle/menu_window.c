/* Battle: the menu window, the cursors over the battlefield, and which
 * members' combination techs still stand.
 *
 * The menu window shows the page of the battle menu that is open for the
 * member choosing (battle_menu_member, battle_menu_page). On the command page
 * it is the battle window with the members' command windows (window.c):
 * moving on to another member rebuilds it, and otherwise each frame keeps the
 * party's numbers current and redraws the command cursor when asked. The
 * tech page lists the member's techs three rows at a time, shaded where one
 * can't be used now; beside the list, each member's MP and what the selected
 * tech costs each of them; the hand at the selected row; and, where the
 * settings ask for it, the tech's description. The item page's list is the
 * original's; here only its cursor and scroll arrows.
 *
 * The cursors over the battlefield are four sprites: on the targets being
 * chosen (a lone target also named, with its HP where the party can see it),
 * or on the members of the selected combination tech.
 *
 * Between menus the battle calls off a member's combination tech when one of
 * the members in it can no longer act.
 *
 * The description and the target's name are battle messages (battle/
 * message.c), run as the original calls them; the item page's list and
 * asking for the tilemaps' uploads are still the original's, which the
 * entries call between their own parts. */
#include "battle/window.h"

#include "battle/bridge.h"
#include "battle/choosing.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/message.h"
#include "battle/window_data.h"
#include "battle/window_leaves.h"
#include "original_effects.h"
#include "port/native.h"
#include "vars.h"


/* ---- The menu window's tilemap ---------------------------------------------------------- */

static void menu_set_tile(GameState *g, WindowCell cell, uint8_t tile) { set_battle_menu_tilemap_tile(g, cell, tile); }
static void menu_set_attributes(GameState *g, WindowCell cell, uint8_t attributes)
{
    set_battle_menu_tilemap_attributes(g, cell, attributes);
}

void menu_put_cursor(GameState *g, WindowCell cell)
{
    menu_set_tile(g, cell, TILE_CURSOR);
    menu_set_tile(g, (WindowCell)(cell + 1), TILE_CURSOR + 1);
    menu_set_tile(g, (WindowCell)(cell + WINDOW_COLUMNS), TILE_CURSOR + 2);
    menu_set_tile(g, (WindowCell)(cell + WINDOW_COLUMNS + 1), TILE_CURSOR + 3);
}

/* ---- Partners ------------------------------------------------------------------------ */

/* A combination tech's partners, and those a member's action chose, are a
 * party slot in each half of a byte (battle.h, first_partner). */
static bool has_second_partner(uint8_t partners) { return second_partner(partners) != NO_PARTNER; }

/* ---- Which techs can be used now -------------------------------------------------------- */

/* Each member has twenty tech records and a list of twenty entries naming
 * them, shown three rows at a time from its scroll. Entries whose id's top
 * four bits are all set are marks: the start of the double techs, of the
 * triple techs, or a blank row. */
enum { TECHS_PER_MEMBER = 20, TECH_ROWS = 3 };
enum { LIST_MARK = 0xF0, DOUBLE_TECHS_START = 0xFC, TRIPLE_TECHS_START = 0xFD, BLANK_ROW = 0xFF };
enum { TECH_DIMMED = 0x80, NO_COST = 0x80 };

/* The list entry of a member's row: its list's start, its scroll and the row,
 * counted in a byte as the original counts them. */
static uint8_t list_entry(const GameState *g, unsigned member, unsigned row)
{
    return (uint8_t)(member * TECHS_PER_MEMBER + battle_tech_scroll(g, member) + row);
}

/* The record a member's list entry names. (The original finds its place in
 * a table of the records' offsets, which an entry past the twenty would read
 * past its end; no list has one, and the entry's guard holds to that.) */
static unsigned tech_record(unsigned member, uint8_t entry) { return member * TECHS_PER_MEMBER + entry; }

/* What the tech costs the member in each party slot. */
static uint8_t cost_to(const GameState *g, unsigned record, unsigned slot)
{
    switch (slot) {
    case 0: return battle_tech_records_cost(g, record);
    case 1: return battle_tech_records_second_cost(g, record);
    default: return battle_tech_records_third_cost(g, record);
    }
}

static bool affordable(uint8_t cost, uint16_t mp) { return (cost & NO_COST) || mp >= cost; }

/* One tech needs Frog to hold one sword: the first of the bytes the battle
 * keeps for him (battle_frog_slot counts them, five a member). */
enum { TECH_NEEDING_FROGS_SWORD = 0x74, FROGS_SWORD = 0x42, MEMBER_CARRIED_BYTES = 5 };

/* The original's work cells for the check: where the record is (bytes), the
 * partners, and the partner it looked at last. */
enum { RECORD_PLACE = 0x88, PARTNERS = 0x81, PARTNER = 0x80, TECH_RECORD_BYTES = 7, MEMBER_RECORDS_BYTES = 140 };

/* A partner and its member are read as the original indexes them: a record
 * naming a partner past the three slots (none a battle holds) reads on into
 * the tables after. */
static uint8_t partner_member(const GameState *g, uint8_t partner)
{
    return partner < BATTLE_MENU_SLOT_MEMBERS_COUNT ? battle_menu_slot_members(g, partner)
                                                    : battle_menu_slot_members_unchecked(g, partner);
}

static bool member_disabled(const GameState *g, uint8_t member)
{
    return (member < BATTLE_MEMBER_DISABLED_COUNT ? battle_member_disabled(g, member)
                                                  : battle_member_disabled_unchecked(g, member)) != 0;
}

static bool partner_out(GameState *g, uint8_t partner)
{
    ORIGINAL_EFFECT(battle_leave_work8(g, PARTNER, partner));
    uint8_t member = partner_member(g, partner);
    return menu_nobody(member) || member_disabled(g, member);
}

/* A tech can be used when every member it costs MP has enough, its partners
 * are choosing and free to act, the member itself is free to use techs, and
 * Frog holds the sword the one tech needs. */
static bool tech_usable(GameState *g, unsigned member, unsigned record)
{
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++)
        if (!affordable(cost_to(g, record, slot), battle_stats_mp(g, slot)))
            return false;
    uint8_t partners = battle_tech_records_partners(g, record);
    ORIGINAL_EFFECT(battle_leave_work8(g, PARTNERS, partners));
    if (partners != BLANK_ROW) {
        if (partner_out(g, first_partner(partners)))
            return false;
        if (has_second_partner(partners) && partner_out(g, second_partner(partners)))
            return false;
    }
    if (battle_member_disabled(g, member))
        return false;
    return battle_tech_records_id(g, record) != TECH_NEEDING_FROGS_SWORD
           || battle_member_carried_weapon(g, battle_frog_slot(g) / MEMBER_CARRIED_BYTES) == FROGS_SWORD;
}

/* A section of combination techs can be used when enough members are
 * choosing. */
static bool section_usable(const GameState *g, uint8_t mark)
{
    switch (mark) {
    case DOUBLE_TECHS_START: return battle_menu_member_count(g) >= 2;
    case TRIPLE_TECHS_START: return battle_menu_member_count(g) >= 3;
    default: return true;
    }
}

/* A row of the list is two lines of the menu window, eighteen cells from the
 * list's column. */
enum { TECH_LIST_COLUMN = 3, TECH_ROW_CELLS = 18 };

static WindowCell tech_row_cell(unsigned row, unsigned line) { return window_cell(2 * row + line, TECH_LIST_COLUMN); }

static void shade_tech_row(GameState *g, unsigned row, uint8_t attributes)
{
    for (unsigned line = 0; line < 2; line++)
        for (unsigned i = 0; i < TECH_ROW_CELLS; i++)
            menu_set_attributes(g, (WindowCell)(tech_row_cell(row, line) + i), attributes);
}

/* The original's cells: where the member's records start, a word, and its
 * party's MP, a word a slot; then a row at a time, the row's place (bytes),
 * its attributes, the next entry and the rows done. */
enum { PARTY_MP = 0x96, ROW_PLACE = 0x84, ROW_ATTRIBUTES = 0x94, ENTRY = 0x86, ROWS_DONE = 0x82 };

static void leave_tech_check_cells(GameState *g, unsigned member)
{
    set_battle_product(g, (uint16_t)(member * MEMBER_RECORDS_BYTES));   /* the multiplier's cell, reused */
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++)
        battle_leave_work16(g, (uint8_t)(PARTY_MP + 2 * slot), battle_stats_mp(g, slot));
}

static void leave_row_cells(GameState *g, unsigned row, uint8_t entry, uint8_t attributes)
{
    battle_leave_work16(g, ROW_PLACE, (uint16_t)(2 * tech_row_cell(row, 0)));
    battle_leave_work8(g, ROW_ATTRIBUTES, attributes);
    battle_leave_work16(g, ENTRY, (uint8_t)(entry + 1));
    battle_leave_work16(g, ROWS_DONE, (uint16_t)(row + 1));
}

// $C1:0EE1
/* Shade the rows of the member's techs that can't be used now, and mark their
 * records so. */
static void shade_unusable_techs(GameState *g)
{
    unsigned member = battle_menu_member(g);
    ORIGINAL_EFFECT(leave_tech_check_cells(g, member));
    for (unsigned row = 0; row < TECH_ROWS; row++) {
        uint8_t entry = list_entry(g, member, row);
        uint8_t id = battle_tech_list_ids(g, entry);
        bool usable;
        if ((id & LIST_MARK) == LIST_MARK) {
            usable = section_usable(g, id);
        } else {
            unsigned record = tech_record(member, battle_tech_list(g, entry));
            ORIGINAL_EFFECT(battle_leave_work16(g, RECORD_PLACE, (uint16_t)(TECH_RECORD_BYTES * record)));
            usable = tech_usable(g, member, record);
            uint8_t flags = battle_tech_records_flags(g, record) & (uint8_t)~TECH_DIMMED;
            set_battle_tech_records_flags(g, record, usable ? flags : flags | TECH_DIMMED);
        }
        uint8_t attributes = usable ? TEXT_PLAIN : TEXT_SHADED;
        shade_tech_row(g, row, attributes);
        ORIGINAL_EFFECT(leave_row_cells(g, row, entry, attributes));
    }
    set_battle_tech_list_dimmed(g, 1);
}

/* ---- The tech page ------------------------------------------------------------------------ */

/* Beside the list, a column nine cells wide: each member's MP on its line, and
 * what the selected tech costs it. */
enum { COLUMN_START = 21, COLUMN_CELLS = 9, PAGE_ROWS = 6, MENU_CELLS = PAGE_ROWS * WINDOW_COLUMNS };
enum { MP_COLUMN = 24, SEPARATOR_COLUMN = 27, COST_COLUMN = 29 };

static WindowCell slot_line_cell(unsigned slot, unsigned column) { return window_cell(1 + 2 * slot, column); }

/* The original counts the column's rows and cells in two work cells. */
enum { ROWS_COUNTED = 0x80, CELLS_COUNTED = 0x81 };

static void leave_column_cells(GameState *g)
{
    battle_leave_work8(g, ROWS_COUNTED, PAGE_ROWS);
    battle_leave_work8(g, CELLS_COUNTED, COLUMN_CELLS);
}

/* The list the original laid out, and the column beside it. */
static void show_tech_list(GameState *g)
{
    for (WindowCell cell = 0; cell < MENU_CELLS; cell++)
        menu_set_tile(g, cell, battle_menu_list_tiles(g, cell));
    for (unsigned row = 0; row < PAGE_ROWS; row++)
        for (unsigned i = 0; i < COLUMN_CELLS; i++)
            menu_set_tile(g, window_cell(row, COLUMN_START + i), battle_tech_page_column_tile(g, row, i));
    ORIGINAL_EFFECT(leave_column_cells(g));
}

/* Each member's MP, its cost cells cleared; a slot with nobody has the mark
 * between the MP and the cost cleared instead. */
static void show_party_mp(GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++) {
        if (!battle_slot_present(g, slot)) {
            menu_set_tile(g, slot_line_cell(slot, SEPARATOR_COLUMN), TILE_BLANK);
            continue;
        }
        Digits d = battle_digits_blanked(g, battle_digits_of(g, battle_stats_mp(g, slot), 2));
        menu_set_tile(g, slot_line_cell(slot, MP_COLUMN), d.tens);
        menu_set_tile(g, slot_line_cell(slot, MP_COLUMN + 1), d.ones);
        menu_set_tile(g, slot_line_cell(slot, COST_COLUMN), TILE_BLANK);
        menu_set_tile(g, slot_line_cell(slot, COST_COLUMN + 1), TILE_BLANK);
    }
}

/* The selected tech: a copy of its record, or every byte $FF on a blank row. */
static void select_tech(GameState *g, unsigned record)
{
    set_battle_selected_tech_id(g, battle_tech_records_id(g, record));
    set_battle_selected_tech_menu_detail(g, battle_tech_records_menu_detail(g, record));
    set_battle_selected_tech_flags(g, battle_tech_records_flags(g, record));
    set_battle_selected_tech_cost(g, battle_tech_records_cost(g, record));
    set_battle_selected_tech_partners(g, battle_tech_records_partners(g, record));
    set_battle_selected_tech_second_cost(g, battle_tech_records_second_cost(g, record));
    set_battle_selected_tech_third_cost(g, battle_tech_records_third_cost(g, record));
}

static void select_no_tech(GameState *g)
{
    set_battle_selected_tech_id(g, BLANK_ROW);
    set_battle_selected_tech_menu_detail(g, BLANK_ROW);
    set_battle_selected_tech_flags(g, BLANK_ROW);
    set_battle_selected_tech_cost(g, BLANK_ROW);
    set_battle_selected_tech_partners(g, BLANK_ROW);
    set_battle_selected_tech_second_cost(g, BLANK_ROW);
    set_battle_selected_tech_third_cost(g, BLANK_ROW);
}

static uint8_t selected_cost_to(const GameState *g, unsigned slot)
{
    switch (slot) {
    case 0: return battle_selected_tech_cost(g);
    case 1: return battle_selected_tech_second_cost(g);
    default: return battle_selected_tech_third_cost(g);
    }
}

/* A cost shows in two cells from the left: its tens and ones, or a lone ones
 * digit and a blank. */
static void show_cost(GameState *g, unsigned slot, uint8_t cost)
{
    Digits d = battle_digits_of(g, cost, 2);
    bool one_digit = digit_is_zero(d.tens);
    menu_set_tile(g, slot_line_cell(slot, COST_COLUMN), one_digit ? d.ones : d.tens);
    menu_set_tile(g, slot_line_cell(slot, COST_COLUMN + 1), one_digit ? TILE_BLANK : d.ones);
}

/* Where the original works out the record: the place of the member's
 * records, or the record's (bytes, in the multiplier's cell again), and the
 * record's among the member's. */
enum { RECORD_OFFSET = 0x80 };

static void leave_selected_cells(GameState *g, unsigned member, bool blank, uint8_t entry)
{
    uint16_t records = (uint16_t)(member * MEMBER_RECORDS_BYTES);
    if (blank) {   /* only the list's start, over the low byte */
        set_battle_product(g, (uint16_t)((records & 0xFF00) | (uint8_t)(member * TECHS_PER_MEMBER)));
        return;
    }
    set_battle_product(g, (uint16_t)(records + entry * TECH_RECORD_BYTES));
    battle_leave_work8(g, RECORD_OFFSET, (uint8_t)(entry * TECH_RECORD_BYTES));
}

/* The tech under the member's cursor, and what it costs each member. A blank
 * row selects no tech (and clears the second member's costs). */
static void show_tech_costs(GameState *g, unsigned member)
{
    uint8_t entry = battle_tech_list(g, (uint8_t)(list_entry(g, member, 0) + battle_tech_cursor(g, member)));
    if (entry == BLANK_ROW) {
        ORIGINAL_EFFECT(leave_selected_cells(g, member, true, entry));
        menu_set_tile(g, slot_line_cell(1, COST_COLUMN), TILE_BLANK);
        menu_set_tile(g, slot_line_cell(1, COST_COLUMN + 1), TILE_BLANK);
        select_no_tech(g);
        return;
    }
    ORIGINAL_EFFECT(leave_selected_cells(g, member, false, entry));
    select_tech(g, tech_record(member, entry));
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++) {
        uint8_t cost = selected_cost_to(g, slot);
        if (!(cost & NO_COST))
            show_cost(g, slot, cost);
    }
}

/* The hand points at the member's selected row, in the list's cursor column,
 * unless a target is being chosen. */
enum { TECH_CURSOR_COLUMN = 1, CURSOR_ROW = 0x80 };

static void clear_tech_cursor_column(GameState *g)
{
    for (unsigned row = 0; row < PAGE_ROWS; row++) {
        menu_set_tile(g, window_cell(row, TECH_CURSOR_COLUMN), TILE_BLANK);
        menu_set_tile(g, window_cell(row, TECH_CURSOR_COLUMN + 1), TILE_BLANK);
    }
}

static void show_tech_cursor(GameState *g, unsigned member)
{
    uint8_t row = battle_tech_cursor(g, member);
    ORIGINAL_EFFECT(battle_leave_work8(g, CURSOR_ROW, row));
    clear_tech_cursor_column(g);
    if (!battle_targeting(g))
        menu_put_cursor(g, window_cell(2 * row, TECH_CURSOR_COLUMN));
}

/* Ask for the frames tilemap to be sent in the next vblank. */
static void request_frames_upload(GameState *g)
{
    set_battle_frames_upload_request(g, (uint8_t)(battle_frames_upload_request(g) + 1));
}

/* The frames tilemap as the tech page wants it, when asked, and sent. */
static void reload_tech_page_frames(GameState *g)
{
    for (WindowCell cell = 0; cell < MENU_CELLS; cell++) {
        set_battle_window_frames_tilemap_tile(g, cell, battle_tech_page_frames_tile(g, cell));
        set_battle_window_frames_tilemap_attributes(g, cell, battle_tech_page_frames_attributes(g, cell));
    }
    request_frames_upload(g);
    set_battle_frames_reload_request(g, 0);
}

// $C1:1C3A
void battle_reset_frames(GameState *g)
{
    for (unsigned cell = 0; cell < BATTLE_WINDOW_FRAMES_TILEMAP_COUNT; cell++) {
        set_battle_window_frames_tilemap_tile(g, cell, battle_first_frames_tile(g, cell));
        set_battle_window_frames_tilemap_attributes(g, cell, battle_first_frames_attributes(g, cell));
    }
}

/* ---- The pages, with the original's parts ------------------------------------------------ */

/* The calls to the original's code, the uploads of the window's tilemaps
 * ($CF:FD02, $CF:FD36), read only what the window left in memory. (A tech's
 * description, $CD:0027, is the battle message's, battle/message.c, which
 * takes the tech in A.) */
enum { CALLEE_READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* The settings' bit that asks for the descriptions. */
enum { DESCRIPTIONS_ON = 0x80 };

// $C1:0C49
/* The window's upload request passes the carry on to the menu's callers, so
 * it is handed the carry the original has there: the redraw's, the party
 * status's, or the command cursor's (clear) (window_leaves.h). */
static bool show_command_page(Snes *s, GameState *g)
{
    bool carry;
    if (battle_menu_slot(g) != battle_menu_slot_shown(g)) {
        set_battle_menu_slot_shown(g, battle_menu_slot(g));
        NATIVE_TIME_AS(s, 0xC11C3A, battle_reset_frames(g));
        vars_peek(true);
        carry = window_redraw_carry(g);
        vars_peek(false);
        NATIVE_TIME_AS(s, 0xC10299, battle_redraw_window(g));
    } else {
        vars_peek(true);
        PartyStatusNote note = note_party_status(g);
        vars_peek(false);
        NATIVE_TIME_AS(s, 0xC105A7, battle_show_party_status(g));
        vars_peek(true);
        carry = party_status_carry(g, note);
        vars_peek(false);
        if (battle_member_cells_redraw(g) && !menu_nobody(battle_menu_slot(g))) {
            battle_redraw_command_cursor(g);
            carry = false;
        }
    }
    /* the frames' reset ends on a compare that leaves the carry set */
    if (menu_nobody(battle_menu_member(g))) {
        NATIVE_TIME_AS(s, 0xC11C3A, battle_reset_frames(g));
        carry = true;
    }
    m_set_flag(s, CPU_FLAG_C, carry);
    if (!battle_call_long(s, 0xC10C79, 0xCFFD02, CALLEE_READS_NOTHING))
        return false;
    request_frames_upload(g);
    return true;
}

/* The carry the original has after the page's own drawing, which the
 * description and the upload request pass on: clear from placing the hand,
 * or where a target is being chosen, set on a blank row and else the last
 * cost's tens compared with the zero (clear where no cost shows). */
static bool tech_page_carry(const GameState *g, unsigned member)
{
    if (!battle_targeting(g))
        return false;
    if (battle_tech_list(g, (uint8_t)(list_entry(g, member, 0) + battle_tech_cursor(g, member))) == BLANK_ROW)
        return true;
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++)
        if (!(selected_cost_to(g, slot) & NO_COST))
            return battle_digits(g, BATTLE_DIGIT_TENS) >= TILE_DIGIT_ZERO;
    return false;
}

/* What laying out the list leaves in A, which the page hands on where
 * nothing after loads it: the list tilemap's place after its four rows. */
enum { TECH_LIST_LAID_OUT_A = 0x0200 };

// $C1:0C83
static bool show_tech_page(Snes *s, GameState *g)
{
    shade_unusable_techs(g);
    NATIVE_TIME_AS(s, 0xC10A88, battle_lay_out_tech_list(g));
    s->cpu.a = TECH_LIST_LAID_OUT_A;
    unsigned member = battle_menu_member(g);
    show_tech_list(g);
    show_party_mp(g);
    show_tech_costs(g, member);
    show_tech_cursor(g, member);
    vars_peek(true);
    m_set_flag(s, CPU_FLAG_C, tech_page_carry(g, member));
    vars_peek(false);
    if (battle_show_descriptions(g) & DESCRIPTIONS_ON) {
        s->cpu.a = battle_selected_tech_id(g);
        if (!battle_run_long(s, 0xC10E95, 0xCD0027, battle_describe))
            return false;
    }
    if (!battle_call_long(s, 0xC10E99, 0xCFFD36, CALLEE_READS_NOTHING))
        return false;
    if (battle_frames_reload_request(g)) {
        reload_tech_page_frames(g);
        m_set_flag(s, CPU_FLAG_C, true);   /* the reload's last count, and its A */
        s->cpu.a = 0;
    }
    return true;
}

/* The tech lists as the game keeps them, which the tech page takes on trust:
 * each member's scroll leaves its three rows, and the cursor's, inside its
 * twenty entries, and each entry names one of its records, or is blank on a
 * row marked so. */
static bool tech_lists_kept(const GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        if (battle_tech_scroll(g, member) > TECHS_PER_MEMBER - TECH_ROWS)
            return false;
        for (unsigned i = 0; i < TECHS_PER_MEMBER; i++) {
            unsigned at = member * TECHS_PER_MEMBER + i;
            uint8_t entry = battle_tech_list(g, at);
            bool marked = (battle_tech_list_ids(g, at) & LIST_MARK) == LIST_MARK;
            if (entry >= TECHS_PER_MEMBER && !(entry == BLANK_ROW && marked))
                return false;
        }
    }
    return true;
}

/* The stat block is noted with the whole of A as the member, its high byte
 * clear wherever the game draws the menu window. */
bool battle_menu_window_ready_a_clear(Snes *s) { return battle_menu_window_ready(s) && !(s->cpu.a & 0xFF00); }

bool battle_menu_window_ready(Snes *s)
{
    if (!battle_window_ready(s))
        return false;
    vars_peek(true);
    bool kept = tech_lists_kept(state_of(s));
    vars_peek(false);
    return kept;
}

// $C1:0C2D
/* Draw the menu window for the page that is open, the member's stat block
 * noted first, which leaves A clear. A page past the techs leaves the carry
 * of comparing it with the tech page's. */
void battle_draw_menu_window(Snes *s)
{
    GameState *g = state_of(s);
    NATIVE_TIME_AS(s, 0xCFFD9E, battle_note_menu_stat_block(g, battle_menu_member(g)));
    s->cpu.a = 0;
    switch (battle_menu_page(g)) {
    case PAGE_COMMANDS:
        if (!show_command_page(s, g))
            return;
        break;
    case PAGE_TECHS:
        if (!show_tech_page(s, g))
            return;
        break;
    default:
        m_set_flag(s, CPU_FLAG_C, true);
        break;
    }
    m_rts(s);
}

/* ---- The cursors over the battlefield --------------------------------------------------- */

/* Four sprites, the first four of the OAM buffer, a hand each. A cursor
 * points at a combatant's cursor point from its left. The high table's bit
 * that puts a sprite past the screen's right edge hides it. */
enum { CURSORS = 4, CURSOR_SPRITE_TILE = 0x00, CURSOR_SPRITE_ATTRIBUTES = 0x32, CURSOR_OFF_SCREEN_Y = 0xF0 };
enum { HAND_WIDTH = 16, CURSORS_HIGH_BYTE = 0, ALL_CURSORS_PAST_THE_EDGE = 0x55 };

static void hide_cursors(GameState *g)
{
    set_oam_high_table_byte(g, CURSORS_HIGH_BYTE, oam_high_table_byte(g, CURSORS_HIGH_BYTE) | ALL_CURSORS_PAST_THE_EDGE);
}

static void show_cursor(GameState *g, unsigned cursor)
{
    set_oam_high_table_byte(g, CURSORS_HIGH_BYTE,
                            oam_high_table_byte(g, CURSORS_HIGH_BYTE) & (uint8_t)~(1u << (2 * cursor)));
}

static void set_cursor_look(GameState *g, unsigned cursor)
{
    set_oam_buffer_tile(g, cursor, CURSOR_SPRITE_TILE);
    set_oam_buffer_attributes(g, cursor, CURSOR_SPRITE_ATTRIBUTES);
}

static void point_cursor_at(GameState *g, unsigned cursor, unsigned slot)
{
    set_oam_buffer_x(g, cursor, (uint8_t)(combatant_cursor_x(g, slot) - HAND_WIDTH));
    set_oam_buffer_y(g, cursor, combatant_cursor_y(g, slot));
}

void battle_move_cursors_away(GameState *g)
{
    hide_cursors(g);
    for (unsigned cursor = 0; cursor < CURSORS; cursor++)
        set_oam_buffer_y(g, cursor, CURSOR_OFF_SCREEN_Y);
}

// $C1:17FE
/* A combination tech's members: the member choosing and its one or two
 * partners, a cursor each; the fourth stays away. */
static void place_combo_cursors(GameState *g)
{
    uint8_t partners = battle_selected_tech_partners(g);
    battle_move_cursors_away(g);
    for (unsigned cursor = 0; cursor < BATTLE_PARTY_SIZE; cursor++)
        set_cursor_look(g, cursor);
    point_cursor_at(g, 0, battle_menu_member(g));
    point_cursor_at(g, 1, first_partner(partners));
    show_cursor(g, 0);
    show_cursor(g, 1);
    if (has_second_partner(partners)) {
        point_cursor_at(g, 2, second_partner(partners));
        show_cursor(g, 2);
    }
}

/* The single techs' ids are below the combination techs', and a tech with no
 * partners is the member's own too. */
enum { FIRST_COMBINATION_TECH = 0x39 };

static bool combination_tech_selected(const GameState *g)
{
    return battle_selected_tech_id(g) >= FIRST_COMBINATION_TECH && battle_selected_tech_partners(g) != NO_PARTNERS;
}

/* The target entries, and how many of them a round of the group's cursors
 * takes. */
enum { TARGETS_A_ROUND = 3 };

/* The original counts the targets and the cursors in two work cells. */
enum { TARGET_COUNTED = 0x80, CURSOR_COUNTED = 0x82 };

static void leave_group_cells(GameState *g, unsigned target, unsigned cursor)
{
    battle_leave_work16(g, TARGET_COUNTED, (uint16_t)target);
    battle_leave_work16(g, CURSOR_COUNTED, (uint16_t)cursor);
}

// $C1:1A7B
/* A group of targets: the first always has a cursor, and the other three go
 * round the rest from where they stopped last time, a round at a time, so a
 * group larger than the cursors has each shown in turn. */
static void place_group_cursors(GameState *g)
{
    uint8_t first = battle_targets(g, 0);
    if (!menu_nobody(first)) {
        point_cursor_at(g, 0, first);
        show_cursor(g, 0);
    }
    unsigned target = battle_target_cursor(g), cursor = 1;
    for (;;) {
        uint8_t slot = battle_targets(g, target);
        if (!menu_nobody(slot)) {
            point_cursor_at(g, cursor, slot);
            show_cursor(g, cursor);
        }
        target++;
        if (target == TARGETS_A_ROUND || target == BATTLE_TARGET_ENTRIES || ++cursor == CURSORS)
            break;
    }
    ORIGINAL_EFFECT(leave_group_cells(g, target, cursor));
    set_battle_target_cursor(g, (uint8_t)(target == BATTLE_TARGET_ENTRIES ? 0 : target));
}

/* A lone enemy target is named ($CD:0030), with its HP where the party can
 * see it ($CD:002D, from battle_target_display); some enemies go unnamed. */
enum { UNNAMED = 0xFF, TARGET_STAT_BLOCK = 0x80 };

static bool enemy_hp_shown(const GameState *g, unsigned slot)
{
    return battle_enemy_hp_seen(g) && battle_enemy_hp_visible(g, slot - BATTLE_FIRST_ENEMY);
}

/* Set out what the target display shows; the name to show. */
static uint8_t describe_target(GameState *g, unsigned slot)
{
    uint8_t enemy = (uint8_t)battle_enemy_numbers(g, slot - BATTLE_FIRST_ENEMY);
    set_battle_target_display(g, 0, enemy);
    ORIGINAL_EFFECT(battle_leave_work16(g, TARGET_STAT_BLOCK, (uint16_t)(slot * BATTLE_STAT_BLOCK_BYTES)));
    if (enemy_hp_shown(g, slot)) {
        uint16_t hp = battle_stats_hp(g, slot), max_hp = battle_stats_max_hp(g, slot);
        set_battle_target_display(g, 1, (uint8_t)hp);
        set_battle_target_display(g, 2, (uint8_t)(hp >> 8));
        set_battle_target_display(g, 3, (uint8_t)max_hp);
        set_battle_target_display(g, 4, (uint8_t)(max_hp >> 8));
    }
    return battle_enemy_name_hidden(g, enemy) ? UNNAMED : (uint8_t)slot;
}

/* When the player moves a lone target and the frame may change, a cursor near
 * the screen's top brings the other frame on and one near its bottom takes it
 * off. */
enum { NEAR_THE_TOP = 0x2D, NEAR_THE_BOTTOM = 0x9C };

static void follow_moved_target(GameState *g)
{
    if (!battle_targets_changed(g))
        return;
    set_battle_targets_changed(g, 0);
    if (!battle_target_moves_frame(g))
        return;
    uint8_t y = oam_buffer_y(g, 0);
    if (!battle_alternate_frame(g)) {
        if (y < NEAR_THE_TOP)
            set_battle_alternate_frame(g, 1);
    } else if (y >= NEAR_THE_BOTTOM) {
        set_battle_alternate_frame(g, 0);
    }
}

/* ---- The item list's cursor --------------------------------------------------------------- */

/* The item list is in the battle window from its eighth row, with a scroll
 * arrow at each end of its right edge: the upper one blank at the list's
 * top, the lower one at its end. */
enum { ITEM_LIST_ROW = 7, ITEM_LIST_TOP = 0, ITEM_LIST_END = 0xFA, TILE_ARROW = 0x7F };
enum { ARROW_COLUMN = 30, UP_ARROW_ROW = 8, DOWN_ARROW_ROW = 12 };

static void show_scroll_arrows(GameState *g, uint8_t top)
{
    bool at_top = top == ITEM_LIST_TOP, at_end = top == ITEM_LIST_END;
    window_put(g, window_cell(UP_ARROW_ROW, ARROW_COLUMN), at_top ? TILE_BLANK : TILE_ARROW,
               at_top ? TEXT_PLAIN_UPSIDE_DOWN : TEXT_PLAIN);
    window_put(g, window_cell(DOWN_ARROW_ROW, ARROW_COLUMN), at_end ? TILE_BLANK : TILE_ARROW,
               at_end ? TEXT_PLAIN : TEXT_PLAIN_UPSIDE_DOWN);
}

static WindowCell item_cursor_cell(const GameState *g, uint8_t row)
{
    return (WindowCell)(window_cell(ITEM_LIST_ROW, 0) + battle_item_cursor_cell(g, row));
}

static void clear_item_cursor(GameState *g, WindowCell cell)
{
    for (unsigned i = 0; i < 4; i++)
        window_set_tile(g, (WindowCell)(cell + i / 2 * WINDOW_COLUMNS + i % 2), TILE_BLANK);
}

static void draw_item_cursor(GameState *g, WindowCell cell)
{
    window_put_cursor(g, cell);
    for (unsigned i = 0; i < 4; i++)
        window_set_attributes(g, (WindowCell)(cell + i / 2 * WINDOW_COLUMNS + i % 2), TEXT_PLAIN);
}

/* The original counts what it drew in a work cell. */
enum { DRAWN = 0x80 };

// $C1:18B0
/* Redraw the scroll arrows when the list scrolled and the cursor when it moved
 * (or the list scrolled under it); true if anything was drawn, for the
 * window's upload. The list's top is noted as shown only when the cursor
 * stayed on its row, so a scroll that also moved the cursor has the arrows
 * drawn again next time. */
static bool show_item_cursor(GameState *g)
{
    uint8_t drawn = 0;
    uint8_t top = battle_item_top(g), row = battle_item_row(g);
    if (top != battle_item_arrows_shown(g)) {
        show_scroll_arrows(g, top);
        drawn++;
    }
    bool move = row != battle_item_cursor_shown(g);
    if (!move && top != battle_item_arrows_shown(g)) {
        set_battle_item_arrows_shown(g, top);
        move = true;
    }
    if (move) {
        uint8_t shown = battle_item_cursor_shown(g);
        clear_item_cursor(g, item_cursor_cell(g, menu_nobody(shown) ? 0 : shown));
        set_battle_item_cursor_shown(g, row);
        draw_item_cursor(g, item_cursor_cell(g, row));
        drawn++;
    }
    ORIGINAL_EFFECT(battle_leave_work8(g, DRAWN, drawn));
    return drawn != 0;
}

/* ---- Placing the cursors, with the original's parts --------------------------------------- */

/* The carry the original leaves from placing a cursor: adding the
 * combatant's cursor offset to its Y. */
static bool cursor_carry(const GameState *g, unsigned slot)
{
    return combatant_y(g, slot) + combatant_cursor_offset_y(g, slot) > 0xFF;
}

/* What the callees read of the machine: $CD:002D and $CD:0030 the name in
 * A's low byte. */
enum { CALLEE_READS_A = DC_AH | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* What the frame around the member leaves in A: the high table's byte it
 * wrote, its high byte clear. */
enum { CHOOSING_FRAME_LEFT_A = 0x00AA };

// $C1:19B4
/* A lone target: its cursor, and on the command page an enemy's name. The
 * carry the original leaves is its last compare's: of the cursor's height
 * where the moved target's frame is followed, else the callee's where the
 * name was shown, else of the target against the enemies on the command page,
 * else of placing the cursor. */
static bool place_lone_target_cursor(Snes *s, GameState *g, uint8_t target)
{
    set_battle_single_target(g, target);
    point_cursor_at(g, 0, target);
    bool carry = cursor_carry(g, target), named = false;
    if (battle_menu_page(g) == PAGE_COMMANDS) {
        carry = !battle_slot_is_party(target);
        if (carry) {
            bool with_hp = enemy_hp_shown(g, target);
            s->cpu.a = describe_target(g, target);
            m_set_flag(s, CPU_FLAG_C, false);   /* the original's doubling of the target, passed on */
            if (!battle_run_long(s, with_hp ? 0xC11A47 : 0xC11A16, with_hp ? 0xCD002D : 0xCD0030,
                                 with_hp ? battle_name_a_combatant_with_hp : battle_name_a_combatant))
                return false;
            named = true;
        }
    }
    show_cursor(g, 0);
    bool followed = battle_targets_changed(g) && battle_target_moves_frame(g);
    bool frame_on = battle_alternate_frame(g) != 0;
    follow_moved_target(g);
    if (followed)
        m_set_flag(s, CPU_FLAG_C, oam_buffer_y(g, 0) >= (frame_on ? NEAR_THE_BOTTOM : NEAR_THE_TOP));
    else if (!named)
        m_set_flag(s, CPU_FLAG_C, carry);
    return true;
}

// $C1:1979
/* While targets are chosen the cursors go on them, unless there were none to
 * choose; on the tech page the list's hand goes first. A group's round leaves
 * the carry set, and A clear, where it went round. */
static bool place_target_cursors(Snes *s, GameState *g)
{
    if (menu_nobody(battle_no_targets(g)))
        return true;
    if (battle_menu_page(g) == PAGE_TECHS)
        clear_tech_cursor_column(g);
    hide_cursors(g);
    for (unsigned cursor = 0; cursor < CURSORS; cursor++)
        set_cursor_look(g, cursor);
    if (!menu_nobody(battle_targets(g, 1))) {
        place_group_cursors(g);
        bool went_round = battle_target_cursor(g) == 0;
        m_set_flag(s, CPU_FLAG_C, went_round);
        if (went_round)
            s->cpu.a = 0;
        return true;
    }
    uint8_t target = battle_targets(g, 0);
    if (menu_nobody(target))
        return true;
    return place_lone_target_cursor(s, g, target);
}

// $C1:17DD
/* Place the cursors for what the member is choosing: none without a member,
 * the targets while they are chosen, on the tech page a combination tech's
 * members or, for a tech of its own, the frame around the member, and the
 * item list's cursor, sending the window when it changed. Placing them takes
 * the buttons the menu gathered. The carry the original leaves: set where a
 * combination has two members (from its end mark), the third's placing where
 * it has three, set after the frame (its loop's last compare), and set where
 * the item list was left as it was. The frame leaves A its high table's
 * byte, its high byte clear. */
void battle_place_menu_cursors(Snes *s)
{
    GameState *g = state_of(s);
    if (menu_nobody(battle_menu_member(g))) {
        battle_move_cursors_away(g);
    } else if (battle_targeting(g)) {
        if (!place_target_cursors(s, g))
            return;
    } else if (battle_menu_page(g) == PAGE_TECHS) {
        if (combination_tech_selected(g)) {
            place_combo_cursors(g);
            uint8_t partners = battle_selected_tech_partners(g);
            m_set_flag(s, CPU_FLAG_C, !has_second_partner(partners) || cursor_carry(g, second_partner(partners)));
        } else {
            NATIVE_TIME_AS(s, 0xC11115, battle_frame_member_choosing(g));
            s->cpu.a = CHOOSING_FRAME_LEFT_A;
            m_set_flag(s, CPU_FLAG_C, true);
        }
    } else if (battle_menu_page(g) != PAGE_COMMANDS) {
        if (show_item_cursor(g)) {
            NATIVE_TIME_AS(s, 0xCFFD6A, battle_send_lower_window_rows(g));
            s->cpu.a = BATTLE_PAGE;               /* the request's leaving */
            m_set_flag(s, CPU_FLAG_C, false);   /* the cursor row's doubling, passed on */
        } else {
            m_set_flag(s, CPU_FLAG_C, true);
        }
    }
    set_battle_menu_buttons(g, 0);
    m_rts(s);
}

/* ---- Combination techs called off ------------------------------------------------------- */

/* Each member's row of the combination table: the member itself, then the
 * others in turn, each in its own column where the member chose it as a
 * partner (in either half of its action's partners). */
enum { COMBO_COLUMNS = 3, NOT_IN_IT = 0xFF };

static unsigned member_in_column(unsigned member, unsigned column) { return (member + column) % BATTLE_PARTY_SIZE; }

static void set_combo_pair(GameState *g, unsigned member, unsigned column, uint8_t v)
{
    set_battle_combo_pairs(g, member * COMBO_COLUMNS + column, v);
}
static bool in_combo(const GameState *g, unsigned member, unsigned column)
{
    return !menu_nobody(battle_combo_pairs(g, member * COMBO_COLUMNS + column));
}

static bool chose_partner(const GameState *g, unsigned member, unsigned partner)
{
    uint8_t partners = battle_member_actions_partners(g, member);
    return first_partner(partners) == partner || second_partner(partners) == partner;
}

static void find_combo_partners(GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_combo_pair(g, member, 0, (uint8_t)member);
        for (unsigned column = 1; column < COMBO_COLUMNS; column++) {
            unsigned partner = member_in_column(member, column);
            set_combo_pair(g, member, column, chose_partner(g, member, partner) ? (uint8_t)partner : NOT_IN_IT);
        }
        set_battle_combo_called_off(g, member, 0);
    }
}

/* A member can't take part while it can't act (combatant_cant_act: the
 * original's $FD:A8A5, which the check calls for each). */
static bool cant_join(GameState *g, unsigned member)
{
    bool out = combatant_cant_act(g, member);
    ORIGINAL_EFFECT(set_battle_combo_member_out(g, out));
    return out;
}

/* A member's combination tech is called off, for it and the members in it,
 * as soon as one of them can't take part. */
static void call_off_combos(GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        for (unsigned column = 0; column < COMBO_COLUMNS; column++) {
            if (!in_combo(g, member, column) || !cant_join(g, member_in_column(member, column)))
                continue;
            for (unsigned other = 0; other < COMBO_COLUMNS; other++)
                if (in_combo(g, member, other))
                    set_battle_combo_called_off(g, member_in_column(member, other), 1);
            break;
        }
}

// $C1:B575
/* Work out whose combination techs are called off, and drop what they had
 * queued (battle/party_turns.c). A's high byte is clear, as the check's
 * calls leave it. */
void battle_update_combo_members(Snes *s)
{
    GameState *g = state_of(s);
    find_combo_partners(g);
    call_off_combos(g);
    s->cpu.a = 0;
    if (!battle_run_long(s, 0xC1B6CC, 0xFDA95F, battle_call_off_waiting_combination))
        return;
    m_rts(s);
}

/* It leaves A's high byte the direct page's (a TDC) and the carry of its
 * loop's last compare, set. */
void battle_reset_window_frames(Snes *s)
{
    battle_reset_frames(state_of(s));
    s->cpu.a = m_direct_page(s) & 0xFF00;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}
