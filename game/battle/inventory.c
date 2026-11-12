/* Battle: the party's inventory and gold (battle/inventory.h).
 *
 * The battle's search runs over 256 places, the fourteen bytes past the
 * inventory's own 242 included, as the original's does. */
#include "battle/inventory.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/equipment_data.h"
#include "charge.h"
#include "vars.h"

enum { INVENTORY_PLACES = 0x100, MOST_HELD = 99 };

static uint8_t item_at(const GameState *g, unsigned place)
{
    return place < INVENTORY_ITEMS_COUNT ? inventory_items(g, place) : inventory_items_unchecked(g, place);
}
static uint8_t held_at(const GameState *g, unsigned place)
{
    return place < INVENTORY_COUNTS_COUNT ? inventory_counts(g, place) : inventory_counts_unchecked(g, place);
}
static void set_place(GameState *g, unsigned place, uint8_t item, uint8_t held)
{
    if (place < INVENTORY_ITEMS_COUNT) {
        set_inventory_items(g, place, item);
        set_inventory_counts(g, place, held);
    } else {
        set_inventory_items_unchecked(g, place, item);
        set_inventory_counts_unchecked(g, place, held);
    }
}

/* Where the search found the item, and the first empty place it passed
 * before (the first place, where it passed none: an item not held then goes
 * there, over what the place holds, and one more of that is counted, as the
 * original's full inventory does). */
typedef struct {
    bool found;
    unsigned place, empty;
    bool passed_empty;
} Search;

static Search search_for(const GameState *g, uint8_t item)
{
    Search s = { false, 0, 0, false };
    for (unsigned place = 0; place < INVENTORY_PLACES; place++) {
        uint8_t there = item_at(g, place);
        if (there == NO_ITEM && !s.passed_empty) {
            s.empty = place;
            s.passed_empty = true;
        }
        if (there == item) {
            s.found = true;
            s.place = place;
            return s;
        }
    }
    return s;
}

// $C1:D005
uint8_t battle_inventory_add(GameState *g, uint8_t item)
{
    if (item == NO_ITEM) {
        return 0;
    }
    Search s = search_for(g, item);
    if (s.found) {
        uint8_t held = held_at(g, s.place);
        if (held >= MOST_HELD)
            return 0;
        set_place(g, s.place, item, (uint8_t)(held + 1));
        return (uint8_t)(held + 1);
    }
    uint8_t held = (uint8_t)(held_at(g, s.empty) + 1);
    set_place(g, s.empty, item, held);
    return held;
}

// $C1:CFE9
/* The search as a question: it keeps the item it looked for, as the
 * battle's inventory routines each do (scaffolding). */
bool battle_inventory_holds(GameState *g, uint8_t item)
{
    return item != NO_ITEM && search_for(g, item).found;
}

// $C1:D056
uint8_t battle_inventory_take(GameState *g, uint8_t item)
{
    if (item == NO_ITEM)
        return 0;
    Search s = search_for(g, item);
    if (!s.found || held_at(g, s.place) == 0)
        return 0;
    uint8_t held = (uint8_t)(held_at(g, s.place) - 1);
    set_place(g, s.place, held ? item : NO_ITEM, held);
    return held ? held : LAST_ONE_TAKEN;
}

/* ---- The gold -------------------------------------------------------------------------------- */

/* The most gold the battle gives the party to hold: 9,999,999, its high byte
 * and low word apart as the original tests them. */
enum { MOST_GOLD_HIGH = 0x98, MOST_GOLD_LOW = 0x967F };

static uint32_t gold(const GameState *g) { return (uint32_t)party_gold_high(g) << 16 | party_gold_low(g); }

static void set_gold(GameState *g, uint32_t amount)
{
    set_party_gold_low(g, (uint16_t)amount);
    set_party_gold_high(g, (uint8_t)(amount >> 16));
}

// $C1:D086
bool battle_gold_covers(GameState *g, uint16_t amount)
{
    return gold(g) >= amount;
}

// $C1:D0A2
/* The gold given, held to the most -- but the original holds the high byte
 * and the low word to theirs apart, and so a total of $990000 or more has its
 * high byte brought down to $98 and keeps its low word where that is within
 * $967F: the party ends with less than the most, not at it (gold of
 * 9,960,000 given 70,000 sums to $99:0BB0, 10,030,000, and is left
 * $98:0BB0, 9,964,464). False where the gold was held to a most. */
bool battle_give_gold(GameState *g, uint16_t amount)
{
    if (amount == 0)
        return true;
    set_gold(g, gold(g) + amount);
    if (party_gold_high(g) < MOST_GOLD_HIGH)
        return true;
    set_party_gold_high(g, MOST_GOLD_HIGH);
    if (party_gold_low(g) <= MOST_GOLD_LOW)
        return true;
    set_party_gold_low(g, MOST_GOLD_LOW);
    return false;
}

// $C1:D0ED
/* The gold taken, where the party has it; nothing taken, and false, where it
 * hasn't. The original works the low word out in the free place's cell first
 * (scaffolding). */
bool battle_take_gold(GameState *g, uint16_t amount)
{
    if (amount == 0)
        return true;
    if (gold(g) < amount)
        return false;
    set_gold(g, gold(g) - amount);
    return true;
}

/* ---- The battle's item list ------------------------------------------------------------------ */

/* The items the party can use in the battle, as its item menu lists them
 * (battle_item_list): a record for each inventory place, the item, its menu
 * detail and flags and how many are held -- but an item the battle doesn't
 * use has none, and the places after it move up. An empty place keeps its
 * record, clear. */
enum { LIST_RECORDS = BATTLE_ITEM_LIST_COUNT, LIST_RECORD_BYTES = 5 };

/* The direct page's cells the list is built in. The inventory place (twice),
 * the item, its number among the other items and how many are held are cells
 * later work reads as it finds them (battle.h), kept plainly; the record's
 * place and the item's battle record's are scaffolding. */
enum {
    PLACE_AT = BATTLE_CLOCK_READS,
    ITEM_AT = BATTLE_MEMBER_CELL,
    PLACES_LEFT = BATTLE_NUMBER_TESTED,
    ITEM_NUMBER_AT = BATTLE_STAT_OFFSET_LOW,
    HELD_AT = BATTLE_TECH_HITS_LEFT,
};

static void clear_list_record(GameState *g, unsigned record)
{
    set_battle_item_list_item(g, record, NO_ITEM);
    set_battle_item_list_menu_detail(g, record, 0);
    set_battle_item_list_flags(g, record, 0);
    set_battle_item_list_held(g, record, 0);
    set_battle_item_list_spare(g, record, 0);
}

// $FD:AF80
/* `held` of `item` given the list's record `record`, if the battle uses the
 * item: whether it did. One it doesn't is noted (battle_item_not_listed). */
static bool list_item(GameState *g, unsigned record, uint8_t item, uint8_t held)
{
    battle_keep_byte(g, ITEM_AT, item);
    bool listed = false;
    if (item >= FIRST_OTHER_ITEM) {
        uint8_t number = (uint8_t)(item - FIRST_OTHER_ITEM);
        battle_keep_word(g, ITEM_NUMBER_AT, number);
        ItemInBattle use = item_in_battle(g, item);
        if (use.used_in_battle) {
            set_battle_item_list_item(g, record, item);
            set_battle_item_list_menu_detail(g, record, use.menu_detail);
            set_battle_item_list_flags(g, record, use.flags);
            set_battle_item_list_held(g, record, held);
            listed = true;
        }
    }
    set_battle_item_not_listed(g, listed ? 0 : 1);
    return listed;
}

// $C1:CDFF
/* The list built afresh from the inventory's 256 places, as the battle
 * starts. */
static void build_item_list(GameState *g)
{
    for (unsigned record = 0; record < LIST_RECORDS; record++)
        clear_list_record(g, record);
    battle_keep_word(g, ITEM_NUMBER_AT, 0);
    unsigned record = 0;
    for (unsigned place = 0; place < INVENTORY_PLACES; place++) {
        uint8_t item = item_at(g, place);
        if (item == NO_ITEM) {
            record++;
            continue;
        }
        battle_keep_byte(g, HELD_AT, held_at(g, place));
        if (list_item(g, record, item, held_at(g, place)))
            record++;
    }
    /* the place counts come round to 0 as the last place is passed */
    battle_keep_word(g, PLACE_AT, 0);
    battle_keep_word(g, PLACES_LEFT, 0);
}

/* ---- The entry ------------------------------------------------------------------------------ */

/* The item list: built whole; and an item given a record, or an empty place
 * passed, by the list's builder or the steal's giving ($C1:F012), with the
 * record's place in its cell ($04), how many are held in its ($0E) and the
 * item in A, whose high byte the original indexes the item's records with
 * (the guard's; it comes back clear, which the steal's giving reads). Each
 * advances the list's place past the record it gave, and the builder's place
 * counts by one. */
void battle_list_usable_items(GameState *g) { build_item_list(g); }

/* ---- The items put back at the battle's end ------------------------------------------------ */

/* A member that chose an item has it taken off the list's count while it waits
 * for its turn (its choice's bit); the item goes back where the turn never
 * came. */
enum { ITEM_TAKEN_FOR_CHOICE = 0x40 };

// $C1:B6D1
void battle_return_chosen_item(GameState *g, uint8_t member)
{
    uint8_t choice = battle_member_actions_choice_flags(g, member);
    if (!(choice & ITEM_TAKEN_FOR_CHOICE))
        return;
    set_battle_member_actions_choice_flags(g, member, choice & (uint8_t)~ITEM_TAKEN_FOR_CHOICE);
    uint8_t item = battle_member_actions_what(g, member);
    for (unsigned record = 0; record < LIST_RECORDS; record++)
        if (battle_item_list_item(g, record) == item) {
            set_battle_item_list_held(g, record, (uint8_t)(battle_item_list_held(g, record) + 1));
            return;
        }
}

/* The original walks the list and the inventory with cells: the list's
 * record, the place it found the item in and the empty place it put it in. */
enum { RECORD_AT = 0x02 };

static unsigned place_holding(const GameState *g, uint8_t item)
{
    unsigned place = 0;
    while (place < INVENTORY_PLACES && item_at(g, place) != item)
        place++;
    return place;
}

/* What the walk leaves: the last place found and the last empty place filled,
 * where there were any, and the list's record it stopped at, the places' high
 * bytes cleared as it leaves. */
typedef struct {
    bool found, filled;
    unsigned found_at, filled_at, record;
} ListWalk;

/* The list's count of each item goes back into the inventory: where the
 * inventory holds the item, its count there is the list's (an item used up
 * leaves its place empty); where it doesn't, the item goes into the first
 * empty place, and where there is none, the rest stay as they are. */
static void put_list_back(GameState *g)
{
    ListWalk w = { false, false, 0, 0, 0 };
    for (; w.record < LIST_RECORDS; w.record++) {
        uint8_t item = battle_item_list_item(g, w.record), held = battle_item_list_held(g, w.record);
        if (item == NO_ITEM)
            continue;
        unsigned place = place_holding(g, item);
        if (place < INVENTORY_PLACES) {
            set_place(g, place, held == 0 ? NO_ITEM : item, held);
            w.found = true;
            w.found_at = place;
            continue;
        }
        place = place_holding(g, NO_ITEM);
        if (place == INVENTORY_PLACES)
            break;
        set_place(g, place, item, held);
        w.filled = true;
        w.filled_at = place;
    }
}

// $C1:B4E9
void battle_put_items_back(GameState *g)
{
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        TIMED_AS(g, 0xC1B6D1, battle_return_chosen_item(g, member));
    put_list_back(g);
}
