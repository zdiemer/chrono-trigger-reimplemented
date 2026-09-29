/* Battle: the party's inventory and gold (battle/inventory.h).
 *
 * The battle's search runs over 256 places, the fourteen bytes past the
 * inventory's own 242 included, as the original's does. */
#include "battle/inventory.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/equipment_data.h"
#include "battle/fight.h"
#include "original_effects.h"
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
static void leave_search(GameState *g, uint8_t item, const Search *s)
{
    set_battle_inventory_item(g, item);
    set_battle_inventory_free_place(g, (uint16_t)s->empty);
    set_battle_inventory_free_seen(g, s->passed_empty ? 1 : 0);
}

// $C1:D005
uint8_t battle_inventory_add(GameState *g, uint8_t item)
{
    if (item == NO_ITEM) {
        ORIGINAL_EFFECT(set_battle_inventory_item(g, item));
        return 0;
    }
    Search s = search_for(g, item);
    ORIGINAL_EFFECT(leave_search(g, item, &s));
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
    ORIGINAL_EFFECT(set_battle_inventory_item(g, item));
    return item != NO_ITEM && search_for(g, item).found;
}

// $C1:D056
uint8_t battle_inventory_take(GameState *g, uint8_t item)
{
    ORIGINAL_EFFECT(set_battle_inventory_item(g, item));
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
    ORIGINAL_EFFECT(set_battle_inventory_item(g, amount));
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
    ORIGINAL_EFFECT(set_battle_inventory_item(g, amount));
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
    ORIGINAL_EFFECT(set_battle_inventory_item(g, amount));
    if (amount == 0)
        return true;
    ORIGINAL_EFFECT(set_battle_inventory_free_place(g, (uint16_t)(party_gold_low(g) - amount)));
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
        ORIGINAL_EFFECT(battle_leave_word(g, ITEM_RECORD_AT, (uint8_t)(number * 3)));
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
    ORIGINAL_EFFECT(battle_leave_word(g, LIST_AT, (uint16_t)(record * LIST_RECORD_BYTES)));
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
bool battle_inventory_holds_ready(Snes *s) { return item_request_ready(s); }
void battle_inventory_holds_entry(Snes *s) { request_entry(s, REQUEST_HOLDS); }
bool battle_inventory_take_ready(Snes *s) { return item_request_ready(s); }
void battle_inventory_take_entry(Snes *s) { request_entry(s, REQUEST_TAKE); }
void battle_gold_covers_entry(Snes *s) { request_entry(s, REQUEST_GOLD_COVERS); }
void battle_give_gold_entry(Snes *s) { request_entry(s, REQUEST_GIVE_GOLD); }
void battle_take_gold_entry(Snes *s) { request_entry(s, REQUEST_TAKE_GOLD); }

// $C1:8003
/* The rest of the game's request, by number in A's low byte with the item or
 * the gold in Y, from any direct page and data bank: the answer in A's low
 * byte, everything else of its caller's as it was (the original saves and
 * puts back its caller's registers around the request). */
bool battle_party_request_ready(Snes *s)
{
    PartyRequest request = (PartyRequest)(uint8_t)s->cpu.a;
    bool item = request == REQUEST_HOLDS || request == REQUEST_ADD || request == REQUEST_TAKE;
    return request < REQUESTS && (!item || s->cpu.y <= ITEM_MOST);
}

/* Each request is timed as the routine of the battle's that answers it. */
static const uint32_t answered_by[REQUESTS] = { 0xC1CFE9, 0xC1D005, 0xC1D056, 0xC1D086, 0xC1D0A2, 0xC1D0ED };

void battle_party_request(Snes *s)
{
    PartyRequest request = (PartyRequest)(uint8_t)s->cpu.a;
    uint8_t answer = 0;
    NATIVE_TIME_AS(s, answered_by[request], answer = party_request(state_of(s), request, s->cpu.y));
    m_set_a8(s, answer);
    m_rtl(s);
}

/* The item list: built whole; and an item given a record, or an empty place
 * passed, by the list's builder or the steal's giving ($C1:F012), with the
 * record's place in its cell ($04), how many are held in its ($0E) and the
 * item in A, whose high byte the original indexes the item's records with
 * (the guard's; it comes back clear, which the steal's giving reads). Each
 * advances the list's place past the record it gave, and the builder's place
 * counts by one. */
void battle_build_item_list_now(GameState *g) { build_item_list(g); }

void battle_build_item_list(Snes *s)
{
    build_item_list(state_of(s));
    m_rts(s);
}

static void count_place(GameState *g, bool advance)
{
    if (advance)
        battle_leave_word(g, LIST_AT, (uint16_t)(battle_cell_word(g, LIST_AT) + LIST_RECORD_BYTES));
    battle_keep_byte(g, PLACE_AT, (uint8_t)(battle_low_scratch(g, PLACE_AT) + 1));
    battle_keep_byte(g, PLACES_LEFT, (uint8_t)(battle_low_scratch(g, PLACES_LEFT) + 1));
}

bool battle_list_item_ready(Snes *s)
{
    vars_peek(true);
    uint16_t at = battle_cell_word(state_of(s), LIST_AT);
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && at % LIST_RECORD_BYTES == 0 && at / LIST_RECORD_BYTES < LIST_RECORDS;
}

void battle_list_item(Snes *s)
{
    GameState *g = state_of(s);
    unsigned record = battle_cell_word(g, LIST_AT) / LIST_RECORD_BYTES;
    count_place(g, list_item(g, record, (uint8_t)s->cpu.a, battle_low_scratch(g, HELD_AT)));
    m_rtl(s);
}

void battle_pass_empty_place(Snes *s)
{
    count_place(state_of(s), true);
    s->cpu.a = 0;
    m_rtl(s);
}
