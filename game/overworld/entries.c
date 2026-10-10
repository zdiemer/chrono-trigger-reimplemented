/* The overworld on the original's machine: the objects' handlers and the main
 * loop as their registered entries, what each leaves the original's code,
 * and the guards that keep them to the states the game makes.
 *
 * Each object's handler is entered from the mode's object loop
 * (menu/objects.c) with its slot in X and an 8-bit accumulator, and hands
 * back the carry: done or not, the only thing the loop reads of what it
 * leaves (the registry declares the rest, the accumulator's width among
 * them). A walker runs an event's script through the script
 * interpreter's bridge (menu/script_bridge.h), called as from the walker's
 * own call to it. */
#include "overworld/entries.h"

#include "menu/map.h"
#include "menu/objects.h"
#include "menu/script_call.h"
#include "menu/sprites.h"
#include "menu/vram.h"
#include "overworld/frames.h"
#include "overworld/main_loop.h"
#include "overworld/music.h"
#include "overworld/overworld.h"
#include "overworld/places.h"
#include "overworld/sign.h"
#include "overworld/vehicles.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- What the handlers leave --------------------------------------------------------------- */

/* A handler says whether it is done with the carry, which the objects' loop
 * reads; the loop sets the accumulator's width again before it uses it
 * ($C2:0545, $C2:054C), so the width a handler leaves is declared don't-care
 * (DC_WIDTH) and the wrappers leave it as it is. */
static void handler_returns(Snes *s, bool done)
{
    m_set_flag(s, CPU_FLAG_C, done);
    m_rts(s);
}

static void handler_goes_on(Snes *s) { handler_returns(s, false); }

/* ---- A walker's script, on the machine ----------------------------------------------------- */

/* Where each walker calls the interpreter from: as it takes its script up,
 * and as it goes on with it. */
typedef struct {
    Snes *s;
    uint32_t calls[2];
} WalkerOnTheMachine;

static const uint32_t LEADER_CALLS[2] = { [WALKER_STARTS_ITS_SCRIPT] = 0xC2378B,
                                          [WALKER_GOES_ON_WITH_ITS_SCRIPT] = 0xC237A1 };
static const uint32_t FOLLOWER_CALLS[2] = { [WALKER_STARTS_ITS_SCRIPT] = 0xC23DBA,
                                            [WALKER_GOES_ON_WITH_ITS_SCRIPT] = 0xC23DC1 };

/* The walker's step table hands the interpreter an 8-bit accumulator and the
 * overflow clear from working out its row, which the first command's call of
 * a routine a script names would see. */
static WalkerScriptEnd run_on_the_machine(void *ctx, WalkerScriptRun run)
{
    WalkerOnTheMachine *w = ctx;
    m_sep(w->s, CPU_FLAG_M);
    m_set_flag(w->s, CPU_FLAG_V, false);
    switch (menu_run_object_script_from(w->s, w->calls[run])) {
    case SCRIPT_DONE: return WALKER_SCRIPT_DONE;
    case SCRIPT_GAVE_BACK: return WALKER_SCRIPT_HANDED_OVER;
    default: return WALKER_SCRIPT_WAITS;
    }
}

static WalkerScripts on_the_machine(WalkerOnTheMachine *w, Snes *s, const uint32_t calls[2])
{
    *w = (WalkerOnTheMachine){ s, { calls[0], calls[1] } };
    return (WalkerScripts){ run_on_the_machine, w };
}

/* A walker's step after the frame's start over. */
static OverworldStep step_to_run(const GameState *g, MenuObject walker)
{
    return overworld_walkers_start_over(g) ? 0 : overworld_step(g, walker);
}

// $C2:3404
void overworld_lead_entry(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject leader = menu_object_running(g);
    WalkerOnTheMachine w;
    WalkerScripts scripts = on_the_machine(&w, s, LEADER_CALLS);
    if (overworld_lead(g, leader, &scripts))
        handler_goes_on(s);
}

// $C2:3AE2
void overworld_follow_entry(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject self = menu_object_running(g);
    WalkerOnTheMachine w;
    WalkerScripts scripts = on_the_machine(&w, s, FOLLOWER_CALLS);
    if (overworld_follow(g, self, &scripts))
        handler_goes_on(s);
}

// $C2:3264
void overworld_watch_the_ground_entry(Snes *s)
{
    GameState *g = state_of(s);
    overworld_watch_the_ground(g, menu_object_running(g));
    handler_goes_on(s);
}

// $C2:2F94
void overworld_region_song_entry(Snes *s)
{
    GameState *g = state_of(s);
    overworld_play_the_region_song(g, menu_object_running(g));
    handler_goes_on(s);
}

// $C2:5628
void overworld_sign_entry(Snes *s)
{
    GameState *g = state_of(s);
    overworld_show_the_sign(g, menu_object_running(g));
    handler_goes_on(s);
}

/* ---- The vehicles -------------------------------------------------------------------------- */

// $C2:309E
void overworld_vehicles_entry(Snes *s)
{
    overworld_control_the_vehicles(state_of(s));
    handler_goes_on(s);
}

/* The Epoch runs its scenes' scripts through the interpreter's bridge, as
 * its own call at $C2:4823 calls it, with the accumulator its step's
 * dispatch leaves 8 bits wide. */
static ScriptEnd epoch_scene_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    m_sep(s, CPU_FLAG_M);
    return menu_run_object_script_from(s, 0xC24823);
}

// $C2:42DD
void overworld_epoch_entry(Snes *s)
{
    GameState *g = state_of(s);
    EpochScripts scripts = { epoch_scene_on_the_machine, s };
    if (overworld_fly_the_epoch(g, menu_object_running(g), &scripts))
        handler_goes_on(s);
}

// $C2:495E
void overworld_epoch_second_sprite_entry(Snes *s)
{
    GameState *g = state_of(s);
    handler_returns(s, overworld_draw_the_epoch_s_second_sprite(g, menu_object_running(g)));
}

// $C2:49CF
void overworld_black_omen_watch_entry(Snes *s)
{
    overworld_watch_for_the_black_omen(state_of(s));
    handler_goes_on(s);
}

/* The test's operands are in the mode's cells: the two spots, and long
 * pointers to their reaches' records. It answers in the carry, which its
 * callers branch on; and it leaves the accumulator 16 bits wide, which
 * $C2:4917's caller goes on with, reading the pad at $C2:44CE. */
static ReachRecord reach_record(uint32_t pointer) { return (ReachRecord)(uint16_t)pointer; }

// $C2:2345
void overworld_within_reach_entry(Snes *s)
{
    GameState *g = state_of(s);
    WorldSpot first = { overworld_scratch_reach_first_x(g), overworld_scratch_reach_first_y(g) };
    WorldSpot second = { overworld_scratch_reach_second_x(g), overworld_scratch_reach_second_y(g) };
    bool within = overworld_within_reach(g, first, reach_record(overworld_scratch_reach_first(g)), second,
                                         reach_record(overworld_scratch_reach_second(g)));
    m_set_flag(s, CPU_FLAG_C, within);
    m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

/* The routines the Epoch's scenes' scripts call leave the running object's
 * slot in X and a carry: the script's call command goes on to its length,
 * 3, and the interpreter after it reads neither (it loads X again and clears
 * the carry for the next command), but liveness follows the interpreter's
 * branch for a length of 0, which the call commands never return, and keeps
 * them live there: they are left for shadow mode's comparison at the
 * command's return. */
static void leave_a_scene_call(Snes *s, bool carry)
{
    s->cpu.x = menu_object_current(state_of(s));
    m_set_flag(s, CPU_FLAG_C, carry);
}

// $C2:48A9
/* Its carry is its sprite script's place doubled, clear for the game's. */
void overworld_show_the_epoch_flying_entry(Snes *s)
{
    GameState *g = state_of(s);
    overworld_show_the_epoch_flying(g, menu_object_running(g));
    ORIGINAL_EFFECT(leave_a_scene_call(s, false));
    m_rts(s);
}

/* The wrap onto the map leaves the carry of its last sum: carried round
 * forward, set where it went past the map's right edge (so it is now in the
 * map's first third); carried back, set where it went past the left (so it
 * is now in the last third). */
static bool wrapped_round(const GameState *g, MenuObject epoch)
{
    uint16_t across = menu_object_slots_x_pixel(g, epoch);
    if (overworld_pace(g, epoch, MENU_ACROSS) < 0)
        return across >= MENU_MAP_WIDTH - EPOCH_CARRIED_ROUND;
    return across < EPOCH_CARRIED_ROUND;
}

// $C2:48CD
void overworld_carry_the_epoch_round_entry(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject self = menu_object_running(g);
    overworld_carry_the_epoch_round(g, self);
    ORIGINAL_EFFECT(leave_a_scene_call(s, wrapped_round(g, self)));
    m_rts(s);
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
 * animates. On foot on the map while an event runs, it keeps to the event's
 * steps, and a script it runs is one the interpreter runs (in a bank of
 * scripts, the sprite list as the mode keeps it). */
typedef struct {
    OverworldStep on_foot, boarding;
    bool moves_the_map;
} WalkerTables;

static bool event_step_runs(const GameState *g, MenuObject walker, PartyPlace place, OverworldStep step)
{
    enum { EVENT_SCRIPTS = 0x7F0000 };
    switch (step) {
    case WALKER_AWAITS_ITS_SCRIPT:
        return overworld_event_script_for(g, place) == 0 || menu_object_script_can_run(g, EVENT_SCRIPTS);
    case WALKER_RUNS_ITS_SCRIPT:
        return menu_object_script_can_run(g, menu_object_slots_script(g, walker));
    default:
        return step < WALKER_STEPS_IN_AN_EVENT;
    }
}

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
        if (overworld_event_running(g))
            return event_step_runs(g, walker, place, step);
        return step < tables.on_foot && walks;
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

/* The controller keeps to the travels and vehicles the game has. */
bool overworld_vehicles_run(Snes *s)
{
    MenuObject self;
    if (!peek(s, &self))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    enum { BOARDINGS = 4 };
    bool ok = overworld_party_travel(g) < TRAVELS && overworld_boarding(g) < BOARDINGS;
    vars_peek(false);
    return ok;
}

/* The Epoch keeps to its steps on a world map, faces one of the four ways,
 * animates with sprite scripts the language reads (the one it has too,
 * unless it is set down, given one first, or plays a scene, whose script
 * gives it its own), has the map follow it where it flies, and plays its
 * scenes from a bank of scripts. */
static bool epoch_animates(const GameState *g, MenuObject epoch, bool given_one_first)
{
    static const uint8_t ANIMATIONS[] = { EPOCH_STANDS_WITH_ITS_WINGS, EPOCH_STANDS_WITHOUT_WINGS, EPOCH_TAKES_OFF,
                                          EPOCH_LANDS };
    if (!menu_sprite_list_is_sound(g) || !menu_object_queue_is_whole(g)
        || !(given_one_first || menu_sprite_script_is_well_formed(g, epoch)))
        return false;
    for (unsigned i = 0; i < sizeof ANIMATIONS; i++)
        if (!menu_given_sprite_script_is_well_formed(g, ANIMATIONS[i]))
            return false;
    for (uint8_t facing = 0; facing < 4; facing++)
        if (!menu_given_sprite_script_is_well_formed(g, overworld_epoch_flying_animation(g, false, facing))
            || !menu_given_sprite_script_is_well_formed(g, overworld_epoch_flying_animation(g, true, facing)))
            return false;
    return true;
}

static bool on_a_world_map(const GameState *g)
{
    enum { FIRST_WORLD_MAP = 0x01F0, WORLD_MAPS = 8 };
    return (uint16_t)(location_id(g) - FIRST_WORLD_MAP) < WORLD_MAPS;
}

/* In the air the map scrolls with the Epoch at its pace, whole pixels: two a
 * frame at most, across or down. */
static bool the_map_can_follow_the_epoch(const GameState *g, MenuObject epoch, EpochStep step)
{
    enum { FASTEST = 2 };
    unsigned records = 2 * menu_map_scroll_records(MENU_ACROSS, FASTEST) + 2 * menu_map_scroll_records(MENU_DOWN, FASTEST);
    if (!menu_map_can_scroll(g, 1, records) || !menu_map_can_scroll(g, 2, records))
        return false;
    if (step != EPOCH_FLIES_A_TILE)
        return true;
    MenuPlace v = menu_object_velocity(g, epoch);
    int16_t across = (int16_t)menu_pixel(v.x), down = (int16_t)menu_pixel(v.y);
    return (uint16_t)v.x == 0 && (uint16_t)v.y == 0 && across >= -FASTEST && across <= FASTEST && down >= -FASTEST
           && down <= FASTEST;
}

bool overworld_epoch_runs(Snes *s)
{
    MenuObject self;
    if (!peek(s, &self))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    EpochStep step = overworld_epoch_step(g, self);
    bool ok = step < EPOCH_STEPS && on_a_world_map(g) && menu_object_slots_facing(g, self) < 4
              && epoch_animates(g, self, step == EPOCH_SETS_DOWN || step == EPOCH_RUNS_A_SCENE);
    if (ok && (step == EPOCH_FLIES || step == EPOCH_FLIES_A_TILE))
        ok = the_map_can_follow_the_epoch(g, self, step);
    if (ok && step == EPOCH_RUNS_A_SCENE)
        ok = menu_object_script_can_run(g, menu_object_slots_script(g, self));
    vars_peek(false);
    return ok;
}

bool overworld_epoch_second_sprite_runs(Snes *s)
{
    MenuObject self;
    if (!peek(s, &self))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = overworld_step(g, self) < 2 && epoch_animates(g, self, overworld_step(g, self) == 0)
              && menu_given_sprite_script_is_well_formed(g, EPOCH_SECOND_SPRITE_STANDING)
              && menu_given_sprite_script_is_well_formed(g, EPOCH_SECOND_SPRITE_IN_FLIGHT);
    vars_peek(false);
    return ok;
}

bool overworld_black_omen_watch_runs(Snes *s)
{
    MenuObject self;
    return peek(s, &self);
}

/* The test takes the reaches the game's callers name. */
static bool a_reach_record(uint32_t pointer)
{
    static const ReachRecord RECORDS[] = { REACH_OF_THE_PARTY, REACH_OF_THE_EPOCH, REACH_OF_THE_DACTYL,
                                           REACH_OF_THE_EPOCH_IN_FLIGHT, REACH_OF_THE_DACTYL_FROM_THE_AIR,
                                           REACH_OF_THE_BLACK_OMEN };
    for (unsigned i = 0; i < sizeof RECORDS / sizeof RECORDS[0]; i++)
        if (pointer == (OVERWORLD_REACHES_BANK | RECORDS[i]))
            return true;
    return false;
}

bool overworld_reach_test_runs(Snes *s)
{
    if (!menu_objects_on_page(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = a_reach_record(overworld_scratch_reach_first(g)) && a_reach_record(overworld_scratch_reach_second(g));
    vars_peek(false);
    return ok;
}

/* The scenes' calls are made on an object of the mode, on the map, facing
 * one of the four ways. */
bool overworld_epoch_scene_call_runs(Snes *s)
{
    if (!menu_object_can_move(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    MenuObject self = menu_object_running(g);
    bool ok = menu_object_slots_facing(g, self) < 4 && menu_object_slots_x_pixel(g, self) < MENU_MAP_WIDTH;
    vars_peek(false);
    return ok;
}

/* The loop runs on the mode's page with the data bank on low RAM. */
bool overworld_main_loop_runs(Snes *s) { return menu_objects_on_page(s); }
