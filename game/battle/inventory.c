/* Battle: the party's inventory, as the battle adds to it
 * (battle/inventory.h).
 *
 * The battle's search runs over 256 places, the fourteen bytes past the
 * inventory's own 242 included, as the original's does. */
#include "battle/inventory.h"

#include "battle/bridge.h"
#include "battle/entries.h"
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

/* ---- The entry ------------------------------------------------------------------------------ */

/* The item in Y, a word: a high byte the port refuses (the original would look
 * for the item it makes, no item among them). How many are held comes back in
 * A's low byte. */
bool battle_add_to_inventory_ready(Snes *s) { return battle_on_page(s) && s->cpu.y <= 0xFF; }

void battle_add_to_inventory(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t item = (uint8_t)s->cpu.y;
    m_set_a8(s, battle_inventory_add(g, item));
    m_rts(s);
}
