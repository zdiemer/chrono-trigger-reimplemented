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

/* The search's cells, which only the battle's inventory routines read, each
 * setting its own first (scaffolding). */

// $C1:D005
uint8_t battle_inventory_add(GameState *g, uint8_t item)
{
    if (item == NO_ITEM) {
        charge_machine_accesses(g, 2);
        return 0;
    }
    Search s = search_for(g, item);
    charge_machine_accesses(g, 5);
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
    charge_machine_accesses(g, 2);
    return item != NO_ITEM && search_for(g, item).found;
}

// $C1:D056
uint8_t battle_inventory_take(GameState *g, uint8_t item)
{
    charge_machine_accesses(g, 2);
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
    charge_machine_accesses(g, 2);
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
    charge_machine_accesses(g, 2);
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
    charge_machine_accesses(g, 2);
    if (amount == 0)
        return true;
    charge_machine_accesses(g, 4);
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
    PLACE_AT = BATTLE_CLOCK_READS, ITEM_RECORD_AT = 0x02, LIST_AT = 0x04, ITEM_AT = BATTLE_MEMBER_CELL,
    PLACES_LEFT = BATTLE_NUMBER_TESTED, ITEM_NUMBER_AT = BATTLE_STAT_OFFSET_LOW, HELD_AT = BATTLE_TECH_HITS_LEFT,
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
        charge_machine_accesses(g, 2);
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
    charge_machine_accesses(g, 2);
}

/* ---- What the rest of the game asks for ------------------------------------------------------- */

/* $C1:8003's requests, by number (A); the ones past these are other
 * routines of the battle's code's, the original's (the entry's guard). Each
 * hands back a byte: whether an item is held, as the item itself or 0; how
 * many are held after an item is added or taken (battle_inventory_add,
 * battle_inventory_take); or for the gold $FF for yes and 0 for no. */
typedef enum {
    REQUEST_HOLDS, REQUEST_ADD, REQUEST_TAKE, REQUEST_GOLD_COVERS, REQUEST_GIVE_GOLD, REQUEST_TAKE_GOLD, REQUESTS,
} PartyRequest;

enum { YES = 0xFF, NO = 0x00 };

static uint8_t answer(bool yes) { return yes ? YES : NO; }

static uint8_t party_request(GameState *g, PartyRequest request, uint16_t what)
{
    switch (request) {
    case REQUEST_HOLDS: return battle_inventory_holds(g, (uint8_t)what) ? (uint8_t)what : NO;
    case REQUEST_ADD: return battle_inventory_add(g, (uint8_t)what);
    case REQUEST_TAKE: return battle_inventory_take(g, (uint8_t)what);
    case REQUEST_GOLD_COVERS: return answer(battle_gold_covers(g, what));
    case REQUEST_GIVE_GOLD: return answer(battle_give_gold(g, what));
    case REQUEST_TAKE_GOLD:
    default: return answer(battle_take_gold(g, what));
    }
}

/* ---- The entry ------------------------------------------------------------------------------ */

/* The item or the gold in Y, a word: for an item, a high byte the port
 * refuses (the original would look for the item it makes, no item among
 * them). The answer comes back in A's low byte. */
enum { ITEM_MOST = 0xFF };

static bool item_request_ready(Snes *s) { return battle_on_page(s) && s->cpu.y <= ITEM_MOST; }

static void request_entry(Snes *s, PartyRequest request)
{
    m_set_a8(s, party_request(state_of(s), request, s->cpu.y));
    m_rts(s);
}

bool battle_add_to_inventory_ready(Snes *s) { return item_request_ready(s); }
void battle_add_to_inventory(Snes *s) { request_entry(s, REQUEST_ADD); }

void battle_give_gold_entry(Snes *s) { request_entry(s, REQUEST_GIVE_GOLD); }

// $C1:8003
/* The rest of the game's request, by number in A's low byte with the item or
 * the gold in Y, from any direct page and data bank: the answer in A's low
 * byte, everything else of its caller's as it was (the original saves and
 * puts back its caller's registers around the request). */

/* Each request is timed as the routine of the battle's that answers it. */

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

/* The original keeps the item it puts back in a cell. */
enum { RETURNING_ITEM_AT = 0x0E };

// $C1:B6D1
void battle_return_chosen_item(GameState *g, uint8_t member)
{
    uint8_t choice = battle_member_actions_choice_flags(g, member);
    if (!(choice & ITEM_TAKEN_FOR_CHOICE))
        return;
    set_battle_member_actions_choice_flags(g, member, choice & (uint8_t)~ITEM_TAKEN_FOR_CHOICE);
    uint8_t item = battle_member_actions_what(g, member);
    charge_machine_accesses(g, 1);
    for (unsigned record = 0; record < LIST_RECORDS; record++)
        if (battle_item_list_item(g, record) == item) {
            set_battle_item_list_held(g, record, (uint8_t)(battle_item_list_held(g, record) + 1));
            return;
        }
}

/* The original walks the list and the inventory with cells: the list's
 * record, the place it found the item in and the empty place it put it in. */
enum { FOUND_AT = 0x00, RECORD_AT = 0x02, EMPTY_AT = 0x04 };

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

static void leave_list_walked(GameState *g, ListWalk w)
{
    charge_machine_accesses(g, 2);
    charge_machine_accesses(g, 2);
    if (w.filled)
        charge_machine_accesses(g, 2);
    else
        charge_machine_accesses(g, 1);
}

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
    leave_list_walked(g, w);
}

// $C1:B4E9
void battle_put_items_back(GameState *g)
{
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        TIMED_AS(g, 0xC1B6D1, battle_return_chosen_item(g, member));
    charge_machine_accesses(g, 1);
    put_list_back(g);
}
