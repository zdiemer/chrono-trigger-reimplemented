/* Battle: redrawing the battle window.
 *
 * The window is redrawn whole when the battle starts and whenever the menu
 * moves on to another member (menu_window.c). Its first six rows are cleared
 * and headed HP and MP over the columns the layout puts them in; then each
 * party member in the fight gets its line (window.h, MemberLine): its name in
 * shaded text, its HP, a slash and its maximum HP in the first layout, its
 * MP, and in the other layouts its gauge between two end caps. Beside the
 * party, each kind of enemy in the fight is named, a line a kind.
 *
 * While members are choosing what to do, each of them has its command window
 * (Attack, Tech or Combo, Item) at the window's left, a frame around it in the
 * frames tilemap, and the member whose menu is open has its name plain and its
 * command cursor drawn. While an action is aimed at a party member, the
 * window marks that member's name instead. */
#include "battle/window.h"

#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/window_data.h"
#include "battle/window_leaves.h"
#include "original_effects.h"
#include "vars.h"

/* ---- The layouts ------------------------------------------------------------------------ */

WindowLayout battle_window_layout_now(const GameState *g)
{
    switch (battle_window_layout(g)) {
    case 0: return LAYOUT_MAX_HP;
    case 1: return LAYOUT_NUMBERS_FIRST;
    default: return LAYOUT_NUMBERS_AFTER;
    }
}

/* A member's line is two rows, the first member's on the rows under the
 * headings, and each layout puts the line's parts in its own columns. */
enum { HEADING_ROW = 0, FIRST_LINE_ROW = 1, LINE_ROWS = 2 };

MemberLine battle_member_line(WindowLayout layout, unsigned member)
{
    static const struct {
        uint8_t name, hp, mp, gauge;
    } columns[] = {
        [LAYOUT_MAX_HP] = { 13, 20, 29, 0 },
        [LAYOUT_NUMBERS_FIRST] = { 20, 13, 17, 25 },
        [LAYOUT_NUMBERS_AFTER] = { 13, 19, 23, 25 },
    };
    unsigned row = FIRST_LINE_ROW + LINE_ROWS * member;
    return (MemberLine){ .name = window_cell(row, columns[layout].name),
                         .hp = window_cell(row, columns[layout].hp),
                         .mp = window_cell(row, columns[layout].mp),
                         .gauge = window_cell(row, columns[layout].gauge) };
}

/* ---- A member's line ------------------------------------------------------------------- */

/* The tiles of the headings, of the cell after the HP (a slash before the
 * maximum, or the edge of the HP's box), and of the gauge's end caps. */
enum { TILE_H = 0x64, TILE_M = 0x65, TILE_P = 0x66, TILE_SLASH = 0xE0, TILE_HP_EDGE = 0x5F };
enum { TILE_GAUGE_LEFT = 0x5D, TILE_GAUGE_RIGHT = 0x5E, GAUGE_CELLS = 4 };

static uint8_t shade(bool shaded) { return shaded ? TEXT_SHADED : TEXT_PLAIN; }

bool member_numbers_shaded(const GameState *g, unsigned member)
{
    return battle_stats_hp(g, member) == 0 || (combatant_hp_low(g, member) && battle_blink(g) != 0);
}

static void put_digits(GameState *g, WindowCell at, const uint8_t *tiles, unsigned n, uint8_t attributes)
{
    for (unsigned i = 0; i < n; i++)
        window_put(g, (WindowCell)(at + i), tiles[i], attributes);
}

static void put_three_digits(GameState *g, WindowCell at, uint16_t n, uint8_t attributes)
{
    Digits d = battle_digits_blanked(g, battle_digits_of(g, n, 3));
    const uint8_t tiles[3] = { d.hundreds, d.tens, d.ones };
    put_digits(g, at, tiles, 3, attributes);
}

void window_show_hp(GameState *g, WindowLayout layout, unsigned member, bool shaded)
{
    WindowCell at = battle_member_line(layout, member).hp;
    put_three_digits(g, at, battle_stats_hp(g, member), shade(shaded));
    window_set_attributes(g, (WindowCell)(at + 3), shade(shaded));
    if (layout == LAYOUT_MAX_HP)
        for (unsigned i = 0; i < 3; i++)
            window_set_attributes(g, (WindowCell)(at + MAX_HP_AFTER_HP + i), shade(shaded));
}

void window_show_mp(GameState *g, WindowLayout layout, unsigned member, bool shaded)
{
    Digits d = battle_digits_blanked(g, battle_digits_of(g, battle_stats_mp(g, member), 2));
    const uint8_t tiles[2] = { d.tens, d.ones };
    put_digits(g, battle_member_line(layout, member).mp, tiles, 2, shade(shaded));
}

/* A name is two rows tall: its lower half on the line's row, five tiles, and
 * its upper half on the row above; the party's names are kept as tiles, the
 * lower half first. */
enum { NAME_LENGTH = 5 };

static void put_name(GameState *g, WindowCell at, unsigned member)
{
    for (unsigned i = 0; i < NAME_LENGTH; i++) {
        window_put(g, (WindowCell)(at + i), battle_party_name_tiles(g, 2 * NAME_LENGTH * member + i), TEXT_SHADED);
        window_put(g, (WindowCell)(at - WINDOW_COLUMNS + i),
                   battle_party_name_tiles(g, 2 * NAME_LENGTH * member + NAME_LENGTH + i), TEXT_SHADED);
    }
}

/* The name of the member whose menu is open, or who is the target, shown
 * plain among the shaded ones. */
static void show_name_plain(GameState *g, WindowLayout layout, unsigned member)
{
    WindowCell at = battle_member_line(layout, member).name;
    for (unsigned i = 0; i < NAME_LENGTH; i++) {
        window_set_attributes(g, (WindowCell)(at - WINDOW_COLUMNS + i), TEXT_PLAIN);
        window_set_attributes(g, (WindowCell)(at + i), TEXT_PLAIN);
    }
}

void battle_leave_member_drawn(GameState *g, unsigned member, bool shaded)
{
    set_battle_member_stat_block(g, (uint16_t)(member * BATTLE_STAT_BLOCK_BYTES));
    set_battle_digit_highlight(g, shaded);
}

/* The name's tiles, counted down in a work cell. */
enum { NAME_COUNT = 0x8E };

static void leave_member_cells(GameState *g, unsigned member, bool shaded)
{
    battle_leave_work8(g, NAME_COUNT, 0);
    battle_leave_member_drawn(g, member, shaded);
}

// $C1:0311
/* A member's line: its name, its HP (then its maximum in the first layout),
 * its MP, and the gauge in the others. */
static void draw_member_line(GameState *g, WindowLayout layout, unsigned member)
{
    MemberLine line = battle_member_line(layout, member);
    bool shaded = member_numbers_shaded(g, member);
    put_name(g, line.name, member);
    window_show_hp(g, layout, member, shaded);
    if (layout == LAYOUT_MAX_HP) {
        window_set_tile(g, (WindowCell)(line.hp + 3), TILE_SLASH);
        put_three_digits(g, (WindowCell)(line.hp + MAX_HP_AFTER_HP), battle_stats_max_hp(g, member), shade(shaded));
    } else {
        window_set_tile(g, (WindowCell)(line.hp + 3), TILE_HP_EDGE);
    }
    window_show_mp(g, layout, member, shaded);
    if (layout_has_gauges(layout)) {
        window_put(g, line.gauge, TILE_GAUGE_LEFT, TEXT_PLAIN);
        window_put(g, (WindowCell)(line.gauge + 1 + GAUGE_CELLS), TILE_GAUGE_RIGHT, TEXT_PLAIN);
        battle_show_gauge(g, member);
    }
    ORIGINAL_EFFECT(leave_member_cells(g, member, shaded));
}

/* ---- The window's lists ------------------------------------------------------------------ */

enum { CLEARED_ROWS = 6 };

static void put_headings(GameState *g, WindowLayout layout)
{
    MemberLine first = battle_member_line(layout, 0);
    WindowCell hp = (WindowCell)(first.hp - WINDOW_COLUMNS), mp = (WindowCell)(first.mp - WINDOW_COLUMNS);
    window_set_tile(g, hp, TILE_H);
    window_set_tile(g, (WindowCell)(hp + 1), TILE_P);
    window_set_tile(g, mp, TILE_M);
    window_set_tile(g, (WindowCell)(mp + 1), TILE_P);
}

/* The enemy kinds' names, eleven tiles a half, each kind's lower half and
 * then its upper kept a tile apart, one line a kind from the window's second
 * column. */
enum { ENEMY_KINDS = 3, ENEMY_NAME_LENGTH = 11, ENEMY_NAME_TILES = 24, ENEMY_NAME_COLUMN = 1 };

static void put_enemy_name(GameState *g, WindowCell at, unsigned kind)
{
    for (unsigned i = 0; i < ENEMY_NAME_LENGTH; i++) {
        window_put(g, (WindowCell)(at + i), battle_enemy_name_tiles(g, ENEMY_NAME_TILES * kind + i), TEXT_PLAIN);
        window_put(g, (WindowCell)(at - WINDOW_COLUMNS + i),
                   battle_enemy_name_tiles(g, ENEMY_NAME_TILES * kind + ENEMY_NAME_LENGTH + 1 + i), TEXT_PLAIN);
    }
}

/* The original counts the lines through the list in its work cells: the loop's
 * index, the next line's place (bytes), and the tiles left of a name. */
enum { LIST_INDEX = 0x80, LIST_PLACE = 0x84, TILEMAP_BYTES_PER_CELL = 2 };

static void leave_enemy_list_cells(GameState *g, unsigned shown)
{
    battle_leave_work16(g, LIST_INDEX, ENEMY_KINDS);
    battle_leave_work16(g, LIST_PLACE,
                        (uint16_t)(TILEMAP_BYTES_PER_CELL * window_cell(FIRST_LINE_ROW + LINE_ROWS * shown, ENEMY_NAME_COLUMN)));
    if (shown)
        battle_leave_work8(g, NAME_COUNT, 0);
}

static void list_enemy_kinds(GameState *g)
{
    unsigned shown = 0;
    for (unsigned kind = 0; kind < ENEMY_KINDS; kind++) {
        if (battle_enemy_kind_counts(g, kind) == 0)
            continue;
        put_enemy_name(g, window_cell(FIRST_LINE_ROW + LINE_ROWS * shown, ENEMY_NAME_COLUMN), kind);
        shown++;
    }
    ORIGINAL_EFFECT(leave_enemy_list_cells(g, shown));
}

/* ---- The command windows --------------------------------------------------------------- */

/* A member's command window is seven cells by six, at the window's left, one
 * beside the other by party slot (so neighbours share their border column). */
enum { COMMAND_WINDOW_COLUMNS = 7, COMMAND_WINDOW_ROWS = 6, COMMAND_WINDOW_STEP = 6 };

static WindowCell command_window_cell(unsigned member, unsigned row, unsigned column)
{
    return window_cell(row, COMMAND_WINDOW_STEP * member + column);
}

/* Whether the member's window offers combination techs instead of its own:
 * when more than one member is choosing and the member has double or triple
 * techs. With Magus in the party only the three of them combine, so when two
 * are choosing, Magus has no Combo, and the other has one only when Magus is
 * not one of the two. */
static bool offers_combos(const GameState *g, unsigned member)
{
    if (!battle_combos_offered(g) || !(battle_double_techs(g, member) | battle_triple_techs(g, member)))
        return false;
    if (!battle_magus_present(g) || battle_menu_member_count(g) == BATTLE_PARTY_SIZE)
        return true;
    uint16_t magus = battle_magus_slot(g);
    if (member == (uint8_t)magus)   /* the original compares the slot's low byte */
        return false;
    return !menu_slot_has_member(g, magus);
}

/* The original counts a window's rows and cells down in two work cells, keeps
 * the next row's place (bytes), and the loop over the slots keeps its index. */
enum { COUNTERS = 0x80, ROW_PLACE = 0x82, SLOT_INDEX = 0x86 };

static void leave_command_window_cells(GameState *g, unsigned member)
{
    battle_leave_work16(g, COUNTERS, 0);
    battle_leave_work16(g, ROW_PLACE,
                        (uint16_t)(TILEMAP_BYTES_PER_CELL * command_window_cell(member, COMMAND_WINDOW_ROWS, 0)));
}

// $C1:0785
static void put_command_window(GameState *g, unsigned member)
{
    bool combo = offers_combos(g, member);
    for (unsigned row = 0; row < COMMAND_WINDOW_ROWS; row++)
        for (unsigned column = 0; column < COMMAND_WINDOW_COLUMNS; column++)
            window_put(g, command_window_cell(member, row, column),
                       battle_command_window_tile(g, combo, row * COMMAND_WINDOW_COLUMNS + column), TEXT_PLAIN);
    ORIGINAL_EFFECT(leave_command_window_cells(g, member));
}

/* The frames tilemap's cells: the frames are drawn by slot, a window beside a
 * drawn one leaving out its left border, which the other drew. */
static void put_frame_cell(GameState *g, WindowCell cell, unsigned from)
{
    set_battle_window_frames_tilemap_tile(g, cell, battle_command_frame_tile(g, from));
    set_battle_window_frames_tilemap_attributes(g, cell, battle_command_frame_attributes(g, from));
}

/* The same, and whether it leaves the border out. */
enum { LEAVES_BORDER_OUT = 0x84 };

static void leave_frame_cells(GameState *g, WindowCell first)
{
    battle_leave_work16(g, COUNTERS, 0);
    battle_leave_work16(g, ROW_PLACE, (uint16_t)(TILEMAP_BYTES_PER_CELL * (first + COMMAND_WINDOW_ROWS * WINDOW_COLUMNS)));
}

// $C1:0929
static void put_frame(GameState *g, unsigned slot, bool beside_another)
{
    unsigned first_column = beside_another ? 1 : 0;
    WindowCell first = window_cell(0, COMMAND_WINDOW_STEP * slot + first_column);
    for (unsigned row = 0; row < COMMAND_WINDOW_ROWS; row++)
        for (unsigned column = first_column; column < COMMAND_WINDOW_COLUMNS; column++)
            put_frame_cell(g, window_cell(row, COMMAND_WINDOW_STEP * slot + column), row * COMMAND_WINDOW_COLUMNS + column);
    ORIGINAL_EFFECT(leave_frame_cells(g, first));
}

// $C1:081E
static void put_frames(GameState *g)
{
    bool last_beside = false;
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++) {
        if (!menu_slot_has_member(g, slot))
            continue;
        last_beside = slot > 0 && menu_slot_has_member(g, slot - 1);
        put_frame(g, slot, last_beside);
    }
    /* The original notes a border left out for the third slot's frame last,
     * and clears it after the second's. */
    ORIGINAL_EFFECT(battle_leave_work8(g, LEAVES_BORDER_OUT, menu_slot_has_member(g, 2) && last_beside));
}

/* ---- The command cursor ----------------------------------------------------------------- */

enum { CURSOR_ROWS_PER_COMMAND = 2, MEMBER_CELLS = 0x80 };

/* The menu's slot whose window was last shown, set to none so that the menu
 * rebuilds the window when it next draws. */
enum { NO_SLOT_SHOWN = 0xFE };

void window_put_cursor(GameState *g, WindowCell cell)
{
    window_set_tile(g, cell, TILE_CURSOR);
    window_set_tile(g, (WindowCell)(cell + 1), TILE_CURSOR + 1);
    window_set_tile(g, (WindowCell)(cell + WINDOW_COLUMNS), TILE_CURSOR + 2);
    window_set_tile(g, (WindowCell)(cell + WINDOW_COLUMNS + 1), TILE_CURSOR + 3);
}

/* Where the original works out the cursor's place: the member's window's
 * offset, bytes, and then the slot's. */
static void leave_cursor_cells(GameState *g, unsigned member, unsigned slot, bool redrawn)
{
    if (redrawn)
        battle_leave_work16(g, MEMBER_CELLS, (uint16_t)(TILEMAP_BYTES_PER_CELL * COMMAND_WINDOW_STEP * slot));
    else
        battle_leave_work8(g, MEMBER_CELLS, (uint8_t)(TILEMAP_BYTES_PER_CELL * COMMAND_WINDOW_STEP * member));
}

// $C1:0872
/* The member whose menu is open loses the command window's cursor column, the
 * two cells inside its left border; then, when the cursor is to be redrawn
 * and no target is being chosen, it points at the command the member's
 * cursor is on, in the window of the menu's slot. Either way the redraw is
 * done. */
void battle_redraw_command_cursor(GameState *g)
{
    unsigned member = battle_menu_member(g);
    for (unsigned row = 0; row < COMMAND_WINDOW_ROWS; row++) {
        window_set_tile(g, command_window_cell(member, row, 0), TILE_NOTHING);
        window_set_tile(g, command_window_cell(member, row, 1), TILE_NOTHING);
    }
    bool redraw = battle_member_cells_redraw(g) != 0;
    unsigned slot = battle_menu_slot(g);
    if (redraw && !battle_targeting(g)) {
        unsigned command = battle_command_cursor(g, battle_menu_slot_members(g, slot));
        window_put_cursor(g, command_window_cell(slot, CURSOR_ROWS_PER_COMMAND * command, 0));
    }
    set_battle_member_cells_redraw(g, 0);
    ORIGINAL_EFFECT(leave_cursor_cells(g, member, slot, redraw));
}

/* The first menu slot with a member, which becomes the menu's when its own
 * slot has none (the entries' guard keeps one). */
static unsigned first_slot_with_member(const GameState *g)
{
    unsigned slot = 0;
    while (!menu_slot_has_member(g, slot))
        slot++;
    return slot;
}

/* The command windows of every member choosing, their frames, the plain name
 * of the member whose menu is open (the menu's slot, or the first with a
 * member), and its cursor. */
static void show_command_windows(GameState *g, WindowLayout layout)
{
    set_battle_combos_offered(g, (uint8_t)(battle_menu_member_count(g) - 1));
    if (!menu_slot_has_member(g, battle_menu_slot(g)))
        set_battle_menu_slot(g, (uint8_t)first_slot_with_member(g));
    uint8_t member = battle_menu_slot_members(g, battle_menu_slot(g));
    set_battle_menu_member(g, member);
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++)
        if (menu_slot_has_member(g, slot))
            put_command_window(g, battle_menu_slot_members(g, slot));
    ORIGINAL_EFFECT(battle_leave_work16(g, SLOT_INDEX, BATTLE_PARTY_SIZE));
    put_frames(g);
    show_name_plain(g, layout, member);
    /* The cursor is asked for as the menu asks for it, by counting the flag up. */
    set_battle_member_cells_redraw(g, (uint8_t)(battle_member_cells_redraw(g) + 1));
    battle_redraw_command_cursor(g);
}

// $C1:0299
void battle_redraw_window(GameState *g)
{
    WindowLayout layout = battle_window_layout_now(g);
    for (WindowCell cell = 0; cell < CLEARED_ROWS * WINDOW_COLUMNS; cell++)
        window_put(g, cell, TILE_NOTHING, TEXT_PLAIN);
    put_headings(g, layout);
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (battle_slot_present(g, member))
            draw_member_line(g, layout, member);
    list_enemy_kinds(g);

    if (battle_menu_member_count(g) == 0)
        return;
    if (battle_ally_target_page(g)) {
        uint8_t target = battle_targets(g, 0);
        if (target != battle_ally_target_marked(g)) {
            set_battle_menu_slot_shown(g, NO_SLOT_SHOWN);
            show_name_plain(g, layout, target);
        }
        return;
    }
    if (battle_menu_page(g) == PAGE_COMMANDS)
        show_command_windows(g, layout);
}

/* ---- The entry ------------------------------------------------------------------------- */

/* It leaves the carry as window_leaves.c works it out. */
void battle_draw_window(Snes *s)
{
    GameState *g = state_of(s);
    vars_peek(true);
    bool carry = window_redraw_carry(g);
    vars_peek(false);
    battle_redraw_window(g);
    m_set_flag(s, CPU_FLAG_C, carry);
    m_rts(s);
}

/* ---- The guards ------------------------------------------------------------------------ */

static bool names_member_or_nobody(uint8_t v) { return v < BATTLE_PARTY_SIZE || menu_nobody(v); }
static bool names_combatant_or_nobody(uint8_t v) { return v < BATTLE_COMBATANTS || menu_nobody(v); }

/* The item list's cursor rows. */
enum { ITEM_CURSOR_ROWS = 4 };

/* The menu's state as the game keeps it, which the window's routines take on
 * trust: the layout's high byte clear (the original reads the layout as a
 * byte in places and as a word in others); the menu's member a party member
 * or nobody, and while anyone is choosing, its slot a party slot and some slot
 * holding a member; the cursors on rows the menus have; the targets
 * combatants or nobody, and an ally while an action is aimed at the party. A
 * state that breaks these is the original's to run. */
static bool menu_state_kept(const GameState *g)
{
    if (battle_window_layout(g) > 0xFF || !names_member_or_nobody(battle_menu_member(g)))
        return false;
    for (unsigned i = 0; i < BATTLE_PARTY_SIZE; i++)
        if (battle_command_cursor(g, i) >= BATTLE_PARTY_SIZE || battle_tech_cursor(g, i) >= BATTLE_PARTY_SIZE)
            return false;
    uint8_t shown = battle_item_cursor_shown(g);
    if (battle_item_row(g) >= ITEM_CURSOR_ROWS || (shown >= ITEM_CURSOR_ROWS && !menu_nobody(shown)))
        return false;
    for (unsigned i = 0; i < BATTLE_TARGET_ENTRIES; i++)
        if (!names_combatant_or_nobody(battle_targets(g, i)))
            return false;
    if (battle_ally_target_page(g) && battle_targets(g, 0) >= BATTLE_PARTY_SIZE)
        return false;
    if (battle_target_cursor(g) >= BATTLE_TARGET_ENTRIES)
        return false;
    if (battle_menu_member_count(g) == 0)
        return true;
    if (battle_menu_slot(g) >= BATTLE_PARTY_SIZE)
        return false;
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++)
        if (menu_slot_has_member(g, slot))
            return true;
    return false;
}

bool battle_window_ready(Snes *s)
{
    vars_peek(true);
    bool ready = battle_on_page(s) && menu_state_kept(state_of(s));
    vars_peek(false);
    return ready;
}

/* The gauge's member, in its work cell, a party member. */
bool battle_window_member_ready(Snes *s)
{
    vars_peek(true);
    bool ready = battle_on_page(s) && battle_work16(state_of(s), BATTLE_GAUGE_MEMBER) < BATTLE_PARTY_SIZE;
    vars_peek(false);
    return ready;
}

/* The cursors also take the target's name in A, whose high byte the original
 * carries into its indexes: the game's is clear. */
bool battle_cursors_ready(Snes *s) { return battle_window_ready(s) && !(s->cpu.a & 0xFF00); }
