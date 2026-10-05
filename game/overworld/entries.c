/* The overworld on the original's machine: the objects' handlers and the main
 * loop as their registered entries, what each leaves the original's code,
 * and the guards that keep them to the states the game makes.
 *
 * Each object's handler is entered from the mode's object loop
 * (menu/objects.c) with its slot in X and an 8-bit accumulator, and hands
 * back the carry clear (never done); the only other thing its caller sees is
 * the accumulator's width, which the leader's leaves 16 bits wide while it is
 * in the middle of a step on foot. */
#include "overworld/entries.h"

#include "menu/map.h"
#include "menu/objects.h"
#include "menu/sprites.h"
#include "menu/vram.h"
#include "overworld/frames.h"
#include "overworld/main_loop.h"
#include "overworld/music.h"
#include "overworld/overworld.h"
#include "overworld/places.h"
#include "overworld/sign.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- What the handlers leave --------------------------------------------------------------- */

/* The leader's step on foot ends with the accumulator 16 bits wide, every
 * other way through with it 8 bits wide. */
static bool leader_mid_step(const GameState *g, MenuObject leader)
{
    return overworld_place_taken(g, PARTY_LEADER) && overworld_party_travel(g) == TRAVEL_ON_FOOT
           && overworld_on_the_map(g) && !overworld_event_running(g) && overworld_step(g, leader) == LEADER_WALKS;
}

static void handler_goes_on(Snes *s, bool a_word_wide)
{
    m_set_flag(s, CPU_FLAG_C, false);
    if (a_word_wide)
        m_rep(s, CPU_FLAG_M);
    else
        m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:3404
void overworld_lead_entry(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject leader = menu_object_running(g);
    overworld_lead(g, leader);
    handler_goes_on(s, leader_mid_step(g, leader));
}

// $C2:3AE2
void overworld_follow_entry(Snes *s)
{
    GameState *g = state_of(s);
    overworld_follow(g, menu_object_running(g));
    handler_goes_on(s, false);
}

// $C2:3264
void overworld_watch_the_ground_entry(Snes *s)
{
    GameState *g = state_of(s);
    overworld_watch_the_ground(g, menu_object_running(g));
    handler_goes_on(s, false);
}

// $C2:2F94
void overworld_region_song_entry(Snes *s)
{
    GameState *g = state_of(s);
    overworld_play_the_region_song(g, menu_object_running(g));
    handler_goes_on(s, false);
}

// $C2:5628
void overworld_sign_entry(Snes *s)
{
    GameState *g = state_of(s);
    overworld_show_the_sign(g, menu_object_running(g));
    handler_goes_on(s, false);
}

// $C2:23CF
void overworld_main_loop_entry(Snes *s) { overworld_run_a_frame(s); }

// $C2:255A
void overworld_back_from_the_menus_entry(Snes *s) { overworld_come_back_from_the_menus(s); }

/* ---- The guards ---------------------------------------------------------------------------- */

static bool peek(Snes *s, MenuObject *self)
{
    if (!menu_object_handler_runs(s))
        return false;
    vars_peek(true);
    *self = menu_object_running(state_of(s));
    vars_peek(false);
    return true;
}

/* A walker's step after the frame's start over. */
static OverworldStep step_to_run(const GameState *g, MenuObject walker)
{
    return overworld_walkers_start_over(g) ? 0 : overworld_step(g, walker);
}

/* A walker's animations are ones the sprite scripts read (the one it has
 * too, unless it sets off, given its standing one first), and the sprite list
 * and the upload queue are as the mode keeps them. */
static bool animates(const GameState *g, MenuObject walker, PartyPlace place, bool sets_off)
{
    return menu_sprite_list_is_sound(g) && menu_object_queue_is_whole(g)
           && (sets_off || menu_sprite_script_is_well_formed(g, walker))
           && overworld_animations_are_sound(g, place, overworld_character_in(g, place));
}

/* The leader moves the map with it: four scrolls a frame at most, a strip of
 * the map each across and two each down; and it turns with whole-pixel paces
 * it sets, its velocity with no fraction (the game sets none). */
static bool the_map_can_follow(const GameState *g, MenuObject leader)
{
    unsigned records = 2 * menu_map_scroll_records(MENU_ACROSS, 1) + 2 * menu_map_scroll_records(MENU_DOWN, 1);
    MenuPlace v = menu_object_velocity(g, leader);
    return menu_map_can_scroll(g, 1, records) && menu_map_can_scroll(g, 2, records) && (uint16_t)v.x == 0
           && (uint16_t)v.y == 0;
}

/* A walker of the party that is there walks by the original's tables: a
 * travel they have, a step of its table (after the frame's start over), a
 * vehicle the game boards (the tables have a second that nothing sets), a
 * facing of the four; it reads the leader's trail as the game keeps it, and
 * animates. On foot on the map an event's script for it is the original's
 * still (the script interpreter on the machine), so it is turned away. */
typedef struct {
    OverworldStep on_foot, boarding;
    bool moves_the_map;
} WalkerTables;

static bool walker_runs(const GameState *g, MenuObject walker, PartyPlace place, WalkerTables tables)
{
    enum { NOT_FACING_BITS = 0x7C };
    Travel travel = overworld_party_travel(g);
    if (travel >= TRAVELS || overworld_event(g) > 1)
        return false;
    if (!overworld_place_taken(g, place))
        return true;
    OverworldStep step = step_to_run(g, walker);
    Vehicle vehicle = overworld_vehicle_boarded(g);
    bool boards_the_game_s = vehicle == NO_VEHICLE || vehicle == VEHICLE_EPOCH || vehicle == VEHICLE_DACTYL;
    bool sets_off = travel == TRAVEL_ON_FOOT && overworld_on_the_map(g) && step == 0;
    bool walks = !(menu_object_slots_facing(g, walker) & NOT_FACING_BITS) && overworld_trail_is_kept(g)
                 && animates(g, walker, place, sets_off) && (!tables.moves_the_map || the_map_can_follow(g, walker));
    switch (travel) {
    case TRAVEL_ON_FOOT:
        if (!overworld_on_the_map(g))
            return animates(g, walker, place, false);
        return !overworld_event_running(g) && step < tables.on_foot && walks;
    case TRAVEL_BOARDING:
        return step < tables.boarding && boards_the_game_s && walks;
    case TRAVEL_GETTING_OFF:
        return boards_the_game_s;
    default:
        return true;
    }
}

bool overworld_leader_runs(Snes *s)
{
    MenuObject self;
    if (!peek(s, &self))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = walker_runs(g, self, PARTY_LEADER, (WalkerTables){ LEADER_STEPS_ON_FOOT, LEADER_STEPS_BOARDING, true });
    vars_peek(false);
    return ok;
}

bool overworld_follower_runs(Snes *s)
{
    MenuObject self;
    if (!peek(s, &self))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    PartyPlace place = (PartyPlace)menu_object_slots_party_place(g, self);
    bool ok = (place == PARTY_FIRST_MEMBER || place == PARTY_SECOND_MEMBER)
              && walker_runs(g, self, place, (WalkerTables){ FOLLOWER_STEPS_ON_FOOT, FOLLOWER_STEPS_BOARDING, false });
    vars_peek(false);
    return ok;
}

/* The watcher looks the leader's tile up in lists as a map's setup loads
 * them, and keeps to the original's table of what an event can be. */
bool overworld_watcher_runs(Snes *s)
{
    MenuObject self;
    if (!peek(s, &self))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = overworld_event(g) <= 1 && overworld_lists_are_loaded(g);
    vars_peek(false);
    return ok;
}

bool overworld_region_song_runs(Snes *s)
{
    MenuObject self;
    if (!peek(s, &self))
        return false;
    vars_peek(true);
    bool ok = overworld_region_song_can_play(state_of(s), self);
    vars_peek(false);
    return ok;
}

bool overworld_sign_runs(Snes *s)
{
    MenuObject self;
    if (!peek(s, &self))
        return false;
    vars_peek(true);
    bool ok = overworld_sign_can_show(state_of(s), self);
    vars_peek(false);
    return ok;
}

/* The loop runs on the mode's page with the data bank on low RAM. */
bool overworld_main_loop_runs(Snes *s) { return menu_objects_on_page(s); }
