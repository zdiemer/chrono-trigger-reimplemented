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
 * the story's and the place's say in what the lists offer is noted, the
 * party's techs are noted (menu/techs.c), and every character's figures
 * worked out (menu/figures.c). */
#include "menu/items.h"

#include "charge.h"
#include "core/program.h"
#include "cpu_io.h"
#include "menu/figures.h"
#include "menu/menu_data.h"
#include "menu/techs.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- Looking an item up -------------------------------------------------------------------- */

enum { LAST_CATEGORY = MENU_ITEM_CATEGORIES - 1 };

/* An item's category and its place in it. */
static MenuItemPlace place_of(const GameState *g, uint8_t item)
{
    uint8_t category = 0;
    while (category < LAST_CATEGORY && item >= menu_item_category_start(g, category + 1))
        category++;
    return (MenuItemPlace){ category, (uint8_t)(item - menu_item_category_start(g, category)) };
}

MenuItemCategory menu_item_category_of(const GameState *g, uint8_t item) { return (MenuItemCategory)place_of(g, item).category; }

MenuItemPlace menu_item_place(GameState *g, uint8_t item)
{
    MenuItemPlace p = place_of(g, item);
    set_menu_item_category(g, p.category);
    set_menu_item_in_category(g, p.index);
    return p;
}

/* The record of an item's place in a table of records `bytes` long from
 * `table`, found with the multiplier, which the original leaves so. */
static uint16_t record_of(uint16_t table, uint8_t bytes, uint8_t index)
{
    return (uint16_t)(table + index * bytes);
}

/* Where an item's data is in bank $CC. */
uint16_t menu_item_data_at_of(GameState *g, uint8_t item)
{
    MenuItemPlace p = menu_item_place(g, item);
    uint16_t at = record_of(menu_item_data_table(g, p.category), menu_item_data_record_bytes(g, p.category), p.index);
    set_menu_item_data_at(g, at);
    return at;
}

/* Where the item last looked up has its figures in bank $CC. */
uint16_t menu_item_figures_at_of_last(GameState *g)
{
    uint8_t category = (uint8_t)menu_item_category(g);
    uint16_t at = record_of(menu_item_figures_table(g, category), menu_item_figures_record_bytes(g, category),
                            (uint8_t)menu_item_in_category(g));
    set_menu_item_figures_at(g, at);
    return at;
}

/* ---- The inventory -------------------------------------------------------------------------- */

/* The first id of each category of equipment stands for nothing in its slot
 * (MENU_NO_WEAPON and the rest, items.h): the inventory never holds one, and
 * an empty place holds the first, 0. */
enum { INVENTORY_PLACES = 0x100, EMPTY = 0 };

bool menu_item_is_a_slots_nothing(uint8_t item)
{
    return item == MENU_NO_WEAPON || item == MENU_NO_ARMOUR || item == MENU_NO_HELMET || item == MENU_NO_ACCESSORY;
}

static bool is_an_item(uint8_t item) { return !menu_item_is_a_slots_nothing(item) && item < MENU_ITEMS; }

/* The inventory's places run on past its last into the next bytes, which the
 * clean-up and the searches below take as places too. */
uint8_t menu_inventory_item_at(const GameState *g, unsigned place)
{
    return place < INVENTORY_ITEMS_COUNT ? inventory_items(g, place) : menu_inventory_past_the_items(g, place - INVENTORY_ITEMS_COUNT);
}
uint8_t menu_inventory_count_at(const GameState *g, unsigned place)
{
    return place < INVENTORY_ITEMS_COUNT ? inventory_counts(g, place)
                                         : menu_inventory_counts_past_the_items(g, place - INVENTORY_ITEMS_COUNT);
}
static void set_item_at(GameState *g, unsigned place, uint8_t item)
{
    if (place < INVENTORY_ITEMS_COUNT)
        set_inventory_items(g, place, item);
    else
        set_menu_inventory_past_the_items(g, place - INVENTORY_ITEMS_COUNT, item);
}
static void set_count_at(GameState *g, unsigned place, uint8_t count)
{
    if (place < INVENTORY_ITEMS_COUNT)
        set_inventory_counts(g, place, count);
    else
        set_menu_inventory_counts_past_the_items(g, place - INVENTORY_ITEMS_COUNT, count);
}
static void empty_place(GameState *g, unsigned place)
{
    set_item_at(g, place, EMPTY);
    set_count_at(g, place, 0);
}

// $C2:9988
/* Every place of the inventory that holds none of an item, or no item there
 * is, emptied. */
static void clean_up_inventory(GameState *g)
{
    for (unsigned place = 0; place < INVENTORY_PLACES; place++)
        if (menu_inventory_count_at(g, place) == 0 || !is_an_item(menu_inventory_item_at(g, place)))
            empty_place(g, place);
}

/* No place: an item the party has none of, or an inventory with no room. */
enum { NO_PLACE = INVENTORY_PLACES };

// $C2:87FA
/* The place that holds an item the party has some of. */
static unsigned place_holding(GameState *g, uint8_t item)
{
    for (unsigned place = 0; place < INVENTORY_PLACES; place++)
        if (menu_inventory_item_at(g, place) == item && menu_inventory_count_at(g, place) != 0) {
            return place;
        }
    return NO_PLACE;
}

static unsigned first_empty_place(const GameState *g)
{
    for (unsigned place = 0; place < INVENTORY_PLACES; place++)
        if (menu_inventory_item_at(g, place) == EMPTY)
            return place;
    return NO_PLACE;
}

/* The party holds 99 of an item at most. The count is a byte, and a sum
 * past 255 wraps before it is held to the most, as the original's does: no
 * count the game makes gets there. */
enum { MOST_HELD = 99 };

static uint8_t held_to_the_most(uint8_t count) { return count >= MOST_HELD ? MOST_HELD : count; }

MenuItemsAdded menu_add_items(GameState *g, uint8_t item, uint8_t count)
{
    if (menu_item_is_a_slots_nothing(item))
        return MENU_ITEMS_NOTHING_TO_ADD;
    unsigned place = place_holding(g, item);
    if (place == NO_PLACE) {
        place = first_empty_place(g);
        if (place == NO_PLACE)
            return MENU_ITEMS_NO_ROOM;
        set_item_at(g, place, item);
        set_count_at(g, place, 0);
    }
    set_count_at(g, place, held_to_the_most((uint8_t)(menu_inventory_count_at(g, place) + count)));
    return MENU_ITEMS_ADDED;
}

MenuItemsTaken menu_take_items(GameState *g, uint8_t item, uint8_t count)
{
    if (item == EMPTY)
        return MENU_ITEMS_TAKEN;
    unsigned place = place_holding(g, item);
    if (place == NO_PLACE)
        return MENU_ITEMS_NOT_HELD;
    uint8_t held = menu_inventory_count_at(g, place);
    if (held < count)
        return MENU_ITEMS_TOO_FEW;
    if (held == count)
        set_item_at(g, place, EMPTY);
    set_count_at(g, place, (uint8_t)(held - count));
    return MENU_ITEMS_TAKEN;
}

// $C2:8E1E
uint8_t menu_items_held(GameState *g, uint8_t item)
{
    unsigned place = place_holding(g, item);
    return place == NO_PLACE ? 0 : menu_inventory_count_at(g, place);
}

uint8_t menu_power_of_item(const GameState *g, uint8_t item)
{
    return item < MENU_ITEM_POWERS_COUNT ? menu_item_powers(g, item) : menu_item_powers_unchecked(g, item);
}

bool menu_loaded_member_can_use(GameState *g, uint8_t item)
{
    uint8_t users = menu_item_users(g, item);
    set_menu_item_users_noted(g, users);
    return (users & menu_character_bit(g, menu_status_record_character(g))) != 0;
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
    charge_block_move(g, 2 * MENU_ITEMS - FIRST_OF_THE_REST - FIRST_ACCESSORY);
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

/* What the story holds the party to (bit 0, and bit 1 unset with it), the
 * story's notes on the game (bit 7) and the place (a location below $1F0):
 * each noted $FFFF where it allows what the lists offer. Item $C8's kind
 * loses bit 1 where the second place note is set. */
enum { FIRST_BIT = 0x01, SECOND_BIT = 0x02, PLACE_ITEMS_HELD = 0x80 };
enum { LAST_PLACE = 0x1F0, ALLOWED = 0xFFFF, NOT_ALLOWED = 0 };
enum { ITEM_THE_PLACE_CHANGES = 0xC8, KIND_THE_PLACE_TAKES = 0x02 };

bool menu_place_is_noted_as_a_location(const GameState *g) { return menu_place_items(g, 1) != NOT_ALLOWED; }

bool menu_place_note_is_known(const GameState *g)
{
    uint16_t note = menu_place_items(g, 1);
    return note == ALLOWED || note == NOT_ALLOWED;
}

static void note_story_and_place(GameState *g)
{
    uint8_t story = story_party_rules(g);
    set_menu_story_items(g, 0, (story & FIRST_BIT) ? ALLOWED : NOT_ALLOWED);
    set_menu_story_items(g, 1, (story & FIRST_BIT) && !(story & SECOND_BIT) ? ALLOWED : NOT_ALLOWED);
    bool near = location_id(g) < LAST_PLACE;
    set_menu_place_items(g, 0, near ? ALLOWED : NOT_ALLOWED);
    set_menu_place_items(g, 1, near && !(story_game_notes(g) & PLACE_ITEMS_HELD) ? ALLOWED : NOT_ALLOWED);
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
    if (menu_place_is_noted_as_a_location(g))
        set_menu_item_kinds(g, ITEM_THE_PLACE_CHANGES,
                            (uint8_t)(menu_item_kinds(g, ITEM_THE_PLACE_CHANGES) & ~KIND_THE_PLACE_TAKES));
}

/* The copy the menus take of the bytes after the characters' names. */
void menu_take_the_bytes_past_the_names(GameState *g) { set_menu_copy_past_the_names(g, menu_past_the_names(g)); }

// $C2:9875
/* The menus' item tables built as they open, the party's techs and
 * everyone's figures noted, and the bytes after the names copied. */
void menu_build_the_items(GameState *g)
{
    menu_build_item_tables(g);
    TIMED_AS(g, 0xFFF813, menu_note_party_techs(g));
    TIMED_AS(g, 0xC299B2, menu_note_everyones_figures(g));
    menu_take_the_bytes_past_the_names(g);
}

// $C2:88D8

/* The tables' name places are counted from the zero word the menus keep 0;
 * every character's figures and the party's techs are noted after them
 * (menu/figures.c and menu/techs.c, whose checks this takes). */
bool menu_items_can_be_built(const GameState *g)
{
    return menu_everyones_figures_can_be_noted(g) && menu_party_techs_can_be_noted(g) && menu_zero(g) == 0;
}

bool menu_items_built(GameState *g)
{
    if (!menu_items_can_be_built(g)) {
        program_breaks_a_contract(g, 0xC29875, "item tables the port doesn't build");
        return false;
    }
    menu_build_the_items(g);
    return true;
}

