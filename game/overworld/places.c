/* The places on the world map, and the object that watches the ground under
 * the leader (overworld/places.h says what the lists are).
 *
 * Each frame the watcher notes whether an event runs (the walkers start
 * their steps over when it changes). With none running, when the leader's
 * last step ended on a place's ground the watcher looks the leader's map tile
 * up in the three lists: an exit shows the place's name on the sign (sign.c)
 * and lets the confirm button enter it (walk.c); an event starts its script,
 * which the leader and the followers wait on (they run the scripts the event
 * gives them); a halt stops the game. Once the leader is off the place's
 * ground the sign and what was found are forgotten. While an event runs the
 * watcher waits for its script to say it is over. */
#include "overworld/frames.h"
#include "overworld/places.h"

#include "charge.h"
#include "menu/map.h"
#include "menu/menu_data.h"
#include "menu/object.h"
#include "original_effects.h"
#include "ram.h"
#include "vars.h"

/* ---- The lists ---------------------------------------------------------------------------- */

/* A record's first byte is its map tile's column, with the top bit set while
 * it is open; the second its row. What was found is a record's place in its
 * list, in bytes, or none. */
enum { OPEN = 0x80, COLUMN_BITS = 0x7F, ROW_BITS = 0x3F, LIST_RECORDS = 128 };
enum { NOTHING_FOUND = 0xFFFF, FOUND_NOTHING_BIT = 0x8000 };

static const uint8_t RECORD_BYTES[PLACE_LISTS] = { [LIST_EXITS] = 7, [LIST_EVENTS] = 3, [LIST_HALTS] = 3 };

static uint8_t record_column(const GameState *g, PlaceList list, unsigned record)
{
    switch (list) {
    case LIST_EXITS: return overworld_exits_column(g, record);
    case LIST_EVENTS: return overworld_events_column(g, record);
    default: return overworld_halts_column(g, record);
    }
}

static uint8_t record_row(const GameState *g, PlaceList list, unsigned record)
{
    switch (list) {
    case LIST_EXITS: return overworld_exits_row(g, record);
    case LIST_EVENTS: return overworld_events_row(g, record);
    default: return overworld_halts_row(g, record);
    }
}

static bool found_one(const GameState *g, PlaceList list) { return !(overworld_found(g, list) & FOUND_NOTHING_BIT); }
static unsigned found_record(const GameState *g, PlaceList list) { return overworld_found(g, list) / RECORD_BYTES[list]; }

bool overworld_on_an_exit(const GameState *g) { return found_one(g, LIST_EXITS); }

// $C2:32CD
/* The first open record of the list on the map tile. */
static uint16_t look_up(const GameState *g, PlaceList list, uint8_t column, uint8_t row)
{
    unsigned length = overworld_list_lengths(g, list);
    for (unsigned record = 0; record < length; record++) {
        uint8_t c = record_column(g, list, record);
        if ((c & OPEN) && (c & COLUMN_BITS) == column && (record_row(g, list, record) & ROW_BITS) == row)
            return (uint16_t)(record * RECORD_BYTES[list]);
    }
    return NOTHING_FOUND;
}

static void forget_what_was_found(GameState *g)
{
    for (unsigned list = 0; list < PLACE_LISTS; list++)
        set_overworld_found(g, list, NOTHING_FOUND);
}

// $C2:3130-$C2:3145, $C2:3184-$C2:319C
void overworld_step_off_the_places(GameState *g)
{
    set_overworld_sign(g, 0);
    set_overworld_sign_again(g, 0);
    forget_what_was_found(g);
    set_overworld_lists_found(g, 0);
    set_overworld_ground_news(g, 0);
}

bool overworld_lists_are_loaded(const GameState *g)
{
    for (unsigned list = 0; list < PLACE_LISTS; list++) {
        uint8_t length = overworld_list_lengths(g, list);
        if (length == 0 || length > LIST_RECORDS)
            return false;
    }
    return true;
}

/* ---- Exits -------------------------------------------------------------------------------- */

ExitTo overworld_exit_here(const GameState *g)
{
    enum { LOCATION_BITS = 0x01FF };
    unsigned exit = found_record(g, LIST_EXITS);
    return (ExitTo){ (uint16_t)(overworld_exits_location(g, exit) & LOCATION_BITS), overworld_exits_arrival(g, exit),
                     overworld_exits_x(g, exit), overworld_exits_y(g, exit) };
}

uint16_t overworld_exit_here_event_script(const GameState *g)
{
    return overworld_event_script_entry(g, overworld_exits_event(g, found_record(g, LIST_EXITS)));
}

bool overworld_exit_here_is_in_range(const GameState *g)
{
    if (!found_one(g, LIST_EXITS))
        return true;
    if (found_record(g, LIST_EXITS) >= LIST_RECORDS || overworld_found(g, LIST_EXITS) % RECORD_BYTES[LIST_EXITS] != 0)
        return false;
    unsigned exit = found_record(g, LIST_EXITS);
    return overworld_exit_here(g).location != EXIT_TO_AN_EVENT
           || overworld_exits_event(g, exit) < OVERWORLD_EVENT_SCRIPTS_COUNT;
}

uint16_t overworld_event_script_entry(const GameState *g, unsigned entry) { return overworld_event_scripts(g, entry); }

// $C2:2470, $C2:1100
void overworld_leave_for(GameState *g, uint16_t location, uint8_t arrival, uint8_t x, uint8_t y)
{
    enum { FACINGS = 3 };
    uint16_t here = location_id(g);
    MenuMapTile stood = menu_map_camera_looks_at(g);
    set_location_previous_id(g, here);
    set_location_previous_x(g, stood.column);
    set_location_previous_y(g, stood.row);
    set_menu_saved_location_id(g, here);
    set_menu_saved_location_x(g, stood.column);
    set_menu_saved_location_y(g, stood.row);
    uint8_t flags = (uint8_t)(arrival >> 1);
    set_location_id(g, location);
    set_location_flags(g, flags);
    set_location_x(g, x);
    set_location_y(g, y);
    set_location_previous_flags(g, menu_return_flags(g, (uint8_t)(flags & FACINGS)));
}

/* ---- Events and halts --------------------------------------------------------------------- */

void overworld_start_event(GameState *g, uint16_t script)
{
    TIMED_AS(g, 0xC204D9, (void)menu_start_script(g, (uint32_t)EVENT_SCRIPTS_BANK << 16 | script, MENU_ORDINARY_SLOT));
    for (PartyPlace place = PARTY_LEADER; place < PARTY_PLACES; place++)
        set_menu_member_scripts(g, place, 0);
}

// $C2:33AA, $C2:33D4, $C2:626E
/* The event on the leader's tile starts, and closes so it runs only once.
 * The original doubles its script's entry in a byte, so the entry's top bit
 * is lost. Only where the event's tile is an exit's too does the event start
 * with its over flag cleared. */
static void start_the_event_here(GameState *g, bool on_an_exit)
{
    enum { ENTRY_BITS = 0x7F };
    unsigned event = found_record(g, LIST_EVENTS);
    uint16_t script = overworld_event_script_entry(g, overworld_events_script(g, event) & ENTRY_BITS);
    set_overworld_event_script(g, script);
    overworld_start_event(g, script);
    set_overworld_events_column(g, event, (uint8_t)(overworld_events_column(g, event) & ~OPEN));
    set_overworld_event(g, 1);
    if (on_an_exit)
        set_overworld_event_over(g, 0);
}

/* The halt's byte after its record, which nothing reads. */
static void note_the_halt(GameState *g)
{
    unsigned halt = found_record(g, LIST_HALTS);
    set_overworld_halt_note(g, overworld_halts_column_unchecked(g, halt + 1));
}

// $C2:6263
static void show_the_place_name(GameState *g)
{
    set_overworld_sign(g, overworld_exits_name(g, found_record(g, LIST_EXITS)));
}

/* What the original leaves of the look-up: the tile's row it looked for, and
 * which lists it found it on. The watcher also counts a step it doesn't use
 * where it found an exit or a halt and no event. */
static void leave_look_up(GameState *g, MenuObject self, uint16_t row, uint8_t lists, bool counts_a_step)
{
    set_overworld_scratch_place_row(g, row);
    set_overworld_scratch_lists_found(g, lists);
    if (counts_a_step)
        set_menu_object_slots_step(g, self, 1);
}

/* Which lists the tile is on (overworld_lists_found). */
enum { FOUND_EXIT = 0x01, FOUND_HALT = 0x02, FOUND_EVENT = 0x04 };

// $C2:3279, $C2:3349
/* The leader's map tile looked up in the lists, and what is there done: an
 * event starts (with the place's name, where it is an exit's tile too); else
 * an exit shows the place's name; else a halt stops the game. */
static void look_at_the_place(GameState *g, MenuObject self)
{
    enum { MAP_TILE_PIXELS = 16 };
    WorldSpot at = overworld_party_spot(g, PARTY_LEADER);
    uint8_t column = (uint8_t)(at.x / MAP_TILE_PIXELS), row = (uint8_t)(at.y / MAP_TILE_PIXELS);
    for (unsigned list = 0; list < PLACE_LISTS; list++)
        set_overworld_found(g, list, look_up(g, (PlaceList)list, column, row));
    uint8_t lists = (uint8_t)((found_one(g, LIST_EXITS) ? FOUND_EXIT : 0) | (found_one(g, LIST_HALTS) ? FOUND_HALT : 0)
                              | (found_one(g, LIST_EVENTS) ? FOUND_EVENT : 0));
    set_overworld_sign(g, 0);
    set_overworld_lists_found(g, lists);
    ORIGINAL_EFFECT(leave_look_up(g, self, (uint16_t)(at.y / MAP_TILE_PIXELS), lists,
                                  !(lists & FOUND_EVENT) && (lists & (FOUND_EXIT | FOUND_HALT))));
    if (lists & FOUND_EVENT) {
        if (lists & FOUND_EXIT)
            show_the_place_name(g);
        start_the_event_here(g, (lists & FOUND_EXIT) != 0);
    } else if (lists & FOUND_EXIT) {
        show_the_place_name(g);
    } else if (lists & FOUND_HALT) {
        ORIGINAL_EFFECT(note_the_halt(g));
        overworld_set_mode(g, OVERWORLD_HALTED);
    }
}

// $C2:3264, $C2:32B7
void overworld_watch_the_ground(GameState *g, MenuObject self)
{
    set_overworld_event_before(g, overworld_event(g));
    if (overworld_event_running(g)) {
        if (overworld_event_over(g)) {
            set_overworld_event(g, 0);
            forget_what_was_found(g);
        }
        return;
    }
    uint8_t news = overworld_ground_news(g);
    if (news & GROUND_NEWS_LOOK) {
        set_overworld_ground_news(g, (uint8_t)(news & ~GROUND_NEWS_LOOK));
        look_at_the_place(g, self);
    }
    if (!(overworld_ground_news(g) & GROUND_NEWS_ON_A_PLACE)) {
        set_overworld_sign(g, 0);
        forget_what_was_found(g);
    }
}
