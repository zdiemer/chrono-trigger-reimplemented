/* The items as the menus show them: the inventory cleaned up and the items'
 * tables built as the menus open ($C2:9875), and the lookups of an item's
 * data and figures the item screens use ($C2:8881, $C2:88AD, $C2:88D8).
 *
 * An item's id says its category (weapons, armour, helmets, accessories,
 * the rest) and its place in it; its data (kind, price, who can use it) and
 * its figures (attack, defence, an accessory's more) are records in the
 * ROM's tables for the category (menu/menu_data.h). As the menus open they
 * copy what their lists show of every item into tables of their own: the
 * kind, the price, who can use it, an amount, an attack or defence, where its
 * name is. The inventory loses the entries no item of the game's fills. Then
 * the story's and the place's say in what the lists offer is noted, the save
 * code's list of what the party carries is made ($FF:F813, still the
 * original's), and each member's figures worked out ($C2:99B2, the
 * original's too). */
#include "menu/items.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/bridge.h"
#include "menu/menu_data.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- Looking an item up -------------------------------------------------------------------- */

enum { LAST_CATEGORY = MENU_ITEM_CATEGORIES - 1 };

// $C2:8881
/* An item's category and its place in it. */
static MenuItemPlace place_of(const GameState *g, uint8_t item)
{
    uint8_t category = 0;
    while (category < LAST_CATEGORY && item >= menu_item_category_start(g, category + 1))
        category++;
    return (MenuItemPlace){ category, (uint8_t)(item - menu_item_category_start(g, category)) };
}

MenuItemPlace menu_item_place(GameState *g, uint8_t item)
{
    MenuItemPlace p = place_of(g, item);
    set_menu_item_category(g, p.category);
    set_menu_item_in_category(g, p.index);
    return p;
}

/* The record of an item's place in a table of records `bytes` long from
 * `table`, found with the multiplier, which the original leaves so. */
static uint16_t record_of(GameState *g, uint16_t table, uint8_t bytes, uint8_t index)
{
    ORIGINAL_EFFECT(hw_set_multiplicand(g, index));
    ORIGINAL_EFFECT((void)hw_multiply_by(g, bytes));
    return (uint16_t)(table + index * bytes);
}

// $C2:88AD
/* Where an item's data is in bank $CC. */
uint16_t menu_item_data_at_of(GameState *g, uint8_t item)
{
    MenuItemPlace p = menu_item_place(g, item);
    uint16_t at = record_of(g, menu_item_data_table(g, p.category), menu_item_data_record_bytes(g, p.category), p.index);
    set_menu_item_data_at(g, at);
    return at;
}

/* The same, worked out without noting anything. */
static uint16_t item_data_place(const GameState *g, uint8_t item)
{
    MenuItemPlace p = place_of(g, item);
    return (uint16_t)(menu_item_data_table(g, p.category) + p.index * menu_item_data_record_bytes(g, p.category));
}

// $C2:88D8
/* Where the item last looked up has its figures in bank $CC. */
uint16_t menu_item_figures_at_of_last(GameState *g)
{
    uint8_t category = (uint8_t)menu_item_category(g);
    uint16_t at = record_of(g, menu_item_figures_table(g, category), menu_item_figures_record_bytes(g, category),
                            (uint8_t)menu_item_in_category(g));
    set_menu_item_figures_at(g, at);
    return at;
}

/* ---- The inventory -------------------------------------------------------------------------- */

/* Ids no item has: none, and three the game leaves unused. */
enum { NO_ITEM = 0x00, UNUSED_A = 0x5A, UNUSED_B = 0x7B, UNUSED_C = 0x94 };
enum { INVENTORY_PLACES = 0x100 };

static bool is_an_item(uint8_t item)
{
    return item != NO_ITEM && item != UNUSED_A && item != UNUSED_B && item != UNUSED_C && item < MENU_ITEMS;
}

/* The inventory's places run on past its last into the next bytes, which the
 * clean-up takes as places too. */
static uint8_t item_at(const GameState *g, unsigned place)
{
    return place < INVENTORY_ITEMS_COUNT ? inventory_items(g, place) : menu_inventory_past_the_items(g, place - INVENTORY_ITEMS_COUNT);
}
static uint8_t count_at(const GameState *g, unsigned place)
{
    return place < INVENTORY_ITEMS_COUNT ? inventory_counts(g, place)
                                         : menu_inventory_counts_past_the_items(g, place - INVENTORY_ITEMS_COUNT);
}
static void empty_place(GameState *g, unsigned place)
{
    if (place < INVENTORY_ITEMS_COUNT) {
        set_inventory_items(g, place, 0);
        set_inventory_counts(g, place, 0);
    } else {
        set_menu_inventory_past_the_items(g, place - INVENTORY_ITEMS_COUNT, 0);
        set_menu_inventory_counts_past_the_items(g, place - INVENTORY_ITEMS_COUNT, 0);
    }
}

// $C2:9988
/* Every place of the inventory that holds none of an item, or no item there
 * is, emptied. */
static void clean_up_inventory(GameState *g)
{
    for (unsigned place = 0; place < INVENTORY_PLACES; place++)
        if (count_at(g, place) == 0 || !is_an_item(item_at(g, place)))
            empty_place(g, place);
}

/* ---- The items' tables ------------------------------------------------------------------------ */

/* An item's data: its kind in the first byte's bits 1-4, its price, who can
 * use it, an amount. */
enum { DATA_KIND = 0, DATA_PRICE = 1, DATA_USERS = 3, DATA_AMOUNT = 4, KIND_BITS = 0x1E };
/* An item's figures: the first its attack or defence; an accessory's third
 * an amount where its second has bit 6 set. */
enum { FIGURES_POWER = 0, FIGURES_FLAGS = 1, FIGURES_AMOUNT = 2, HAS_AN_AMOUNT = 0x40 };

enum { FIRST_ACCESSORY = 0x94, FIRST_OF_THE_REST = 0xBC, ARMED = 0x94 };

static uint16_t table_word(const GameState *g, uint16_t at)
{
    return (uint16_t)(menu_item_table_byte(g, at) | menu_item_table_byte(g, (uint16_t)(at + 1)) << 8);
}

static void copy_items_data(GameState *g)
{
    for (unsigned item = 0; item < MENU_ITEMS; item++) {
        uint16_t at = 0;
        TIMED_AS(g, 0xC288AD, at = menu_item_data_at_of(g, (uint8_t)item));
        set_menu_item_kinds(g, item, (uint8_t)(menu_item_table_byte(g, (uint16_t)(at + DATA_KIND)) & KIND_BITS));
        set_menu_item_users(g, item, menu_item_table_byte(g, (uint16_t)(at + DATA_USERS)));
        set_menu_item_amounts(g, item, menu_item_table_byte(g, (uint16_t)(at + DATA_AMOUNT)));
        set_menu_item_prices(g, item, table_word(g, (uint16_t)(at + DATA_PRICE)));
    }
}

/* Where each item's name is. The original counts the table from the menus'
 * zero word, which is 0. */
static void note_name_places(GameState *g)
{
    for (unsigned item = 0; item < MENU_ITEMS; item++)
        set_menu_item_name_places(g, item, (uint16_t)(MENU_ITEM_NAMES + item * MENU_NAME_TABLE_BYTES));
}

/* No one uses the rest's items as the lists count users, and nothing past
 * the helmets has an amount but what an accessory's figures give it. */
static void clear_past_the_equipment(GameState *g)
{
    for (unsigned item = FIRST_OF_THE_REST; item < MENU_ITEMS; item++)
        set_menu_item_users(g, item, 0);
    for (unsigned item = FIRST_ACCESSORY; item < MENU_ITEMS; item++)
        set_menu_item_amounts(g, item, 0);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * MENU_ITEMS - FIRST_OF_THE_REST - FIRST_ACCESSORY));
}

static uint16_t figures_of(GameState *g, uint8_t item)
{
    uint16_t at = 0;
    TIMED_AS(g, 0xC28881, (void)menu_item_place(g, item));
    TIMED_AS(g, 0xC288D8, at = menu_item_figures_at_of_last(g));
    return at;
}

static void copy_items_figures(GameState *g)
{
    for (unsigned item = FIRST_ACCESSORY; item < FIRST_OF_THE_REST; item++) {
        uint16_t at = figures_of(g, (uint8_t)item);
        if (menu_item_table_byte(g, (uint16_t)(at + FIGURES_FLAGS)) & HAS_AN_AMOUNT)
            set_menu_item_amounts(g, item, menu_item_table_byte(g, (uint16_t)(at + FIGURES_AMOUNT)));
    }
    for (unsigned item = 0; item < ARMED; item++)
        set_menu_item_powers(g, item, menu_item_table_byte(g, figures_of(g, (uint8_t)item)));
}

/* ---- What the story and the place allow ---------------------------------------------------------- */

/* The story's byte at $7F:00BA (bit 0, and bit 1 unset with it) and at
 * $7F:01CE (bit 15 of the word), and the place (a location below $1F0): each
 * noted $FFFF where it allows what the lists offer. Item $C8's kind loses
 * bit 1 where the second place note is set. */
enum { STORY_BYTE = 0xBA, STORY_WORD_HIGH = 0x1CF, FIRST_BIT = 0x01, SECOND_BIT = 0x02, SIGN_BIT = 0x80 };
enum { LAST_PLACE = 0x1F0, ALLOWED = 0xFFFF, NOT_ALLOWED = 0 };
enum { ITEM_THE_PLACE_CHANGES = 0xC8, KIND_THE_PLACE_TAKES = 0x02 };

static bool later_place(const GameState *g) { return menu_place_items(g, 1) != NOT_ALLOWED; }

static void note_story_and_place(GameState *g)
{
    uint8_t story = event_memory_byte(g, STORY_BYTE);
    set_menu_story_items(g, 0, (story & FIRST_BIT) ? ALLOWED : NOT_ALLOWED);
    set_menu_story_items(g, 1, (story & FIRST_BIT) && !(story & SECOND_BIT) ? ALLOWED : NOT_ALLOWED);
    bool near = location_id(g) < LAST_PLACE;
    set_menu_place_items(g, 0, near ? ALLOWED : NOT_ALLOWED);
    set_menu_place_items(g, 1, near && !(event_memory_byte(g, STORY_WORD_HIGH) & SIGN_BIT) ? ALLOWED : NOT_ALLOWED);
    set_menu_place_items(g, 2, ALLOWED);
}

// $C2:9875 (up to $C2:9973)
/* The inventory cleaned up, the story's and the place's notes, and the
 * items' tables. */
void menu_build_item_tables(GameState *g)
{
    clean_up_inventory(g);
    note_story_and_place(g);
    copy_items_data(g);
    note_name_places(g);
    clear_past_the_equipment(g);
    copy_items_figures(g);
    if (later_place(g))
        set_menu_item_kinds(g, ITEM_THE_PLACE_CHANGES,
                            (uint8_t)(menu_item_kinds(g, ITEM_THE_PLACE_CHANGES) & ~KIND_THE_PLACE_TAKES));
}

/* The copy the menus take of the bytes after the characters' names. */
void menu_copy_past_the_names_now(GameState *g) { set_menu_copy_past_the_names(g, menu_past_the_names(g)); }

/* ---- The entries ------------------------------------------------------------------------- */

/* What the tables leave before the save code's list is made: the widths
 * and bank it runs with, the count of the figures' loop in the menus' first
 * cells, and the flags of the second place note's test (and of item $C8's
 * kind where it is set), with the loop's carry. */
static void leave_tables_built(Snes *s)
{
    GameState *g = state_of(s);
    set_menu_scratch_graphics_count(g, ARMED);
    uint8_t note = (uint8_t)menu_place_items(g, 1);
    menu_leave_load(s, note);
    if (note != 0) {   /* the bit's test, as it was before it was cleared */
        uint8_t kind = menu_item_table_byte(g, (uint16_t)(item_data_place(g, ITEM_THE_PLACE_CHANGES) + DATA_KIND));
        menu_leave_load(s, KIND_THE_PLACE_TAKES);
        m_set_flag(s, CPU_FLAG_Z, (kind & KIND_THE_PLACE_TAKES) == 0);
    }
    m_set_flag(s, CPU_FLAG_C, true);
}

/* What the save code's list ($FF:F813) and each member's figures ($C2:99B2)
 * don't read (tools/dontcare_check.py checks both). */
enum { LIST_IGNORES = DC_A | DC_X | DC_Y, MEMBERS_IGNORES = DC_A | DC_X | DC_Y };

// $C2:9875
void menu_build_items(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_build_item_tables(g);
    menu_set_widths(s, true, false);
    s->cpu.db = MENU_DATA_BANK;   /* as the tables' block moves leave it */
    ORIGINAL_EFFECT(leave_tables_built(s));
    if (!menu_call_long(s, 0xC29973, 0xFFF813, LIST_IGNORES) || !menu_call(s, 0xC29977, 0xC299B2, MEMBERS_IGNORES))
        return;
    menu_copy_past_the_names_now(g);
    menu_restore_p(s);
    m_rts(s);
}

/* The lookup runs with 8-bit index registers, which leaves Y's high byte
 * clear (X it saves and puts back). */
static void leave_y_a_byte(Snes *s) { s->cpu.y &= 0x00FF; }

/* The lookups' entries: the item in A's low byte; the category comes back
 * there, the data's or the figures' place in A and X. */
// $C2:8881
void menu_look_up_item(Snes *s)
{
    MenuItemPlace p = menu_item_place(state_of(s), (uint8_t)s->cpu.a);
    m_set_a8(s, p.category);
    ORIGINAL_EFFECT(leave_y_a_byte(s));
    m_rts(s);
}

static void leave_found(Snes *s, uint16_t at)
{
    s->cpu.a = at;
    s->cpu.x = m_x8(s) ? (uint8_t)at : at;
}

// $C2:88AD
void menu_find_item_data(Snes *s)
{
    leave_found(s, menu_item_data_at_of(state_of(s), (uint8_t)s->cpu.a));
    ORIGINAL_EFFECT(leave_y_a_byte(s));   /* its lookup's */
    m_rts(s);
}

// $C2:88D8
void menu_find_item_figures(Snes *s)
{
    leave_found(s, menu_item_figures_at_of_last(state_of(s)));
    m_rts(s);
}

/* The tables run on the menus' page with the data bank on work RAM's, their
 * name places counted from the zero word the menus keep 0. */
bool menu_items_are_buildable(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_zero(state_of(s)) == 0;
    vars_peek(false);
    return ok;
}

/* The figures' lookup takes the item looked up last, which is of one of the
 * categories. */
bool menu_item_figures_are_findable(Snes *s)
{
    vars_peek(true);
    bool ok = menu_item_category(state_of(s)) < MENU_ITEM_CATEGORIES;
    vars_peek(false);
    return ok;
}
