/* The overworld's objects as the game runs them: the walkers, the vehicles,
 * the places' watcher, the music and the sign -- what each takes of the
 * game's state, its turn as the program runs it with no machine, and the
 * routines the world maps' scenes call of the overworld's.
 *
 * Each turn is the same function of the game's state the machine's entry
 * runs (entries.c); the walkers and the Epoch run a script of their own
 * through the command scripts' interpreter, which here is the interpreter
 * as C (menu/handlers.h). What each takes is the state half of the entry's
 * guard, which the program checks first (menu/handlers.c) and the entries'
 * guards check after the machine's half. */
#include "overworld/handlers.h"

#include "charge.h"
#include "menu/handlers.h"
#include "menu/map.h"
#include "menu/script_call.h"
#include "menu/sprites.h"
#include "menu/vram.h"
#include "overworld/frames.h"
#include "overworld/music.h"
#include "overworld/overworld.h"
#include "overworld/places.h"
#include "overworld/sign.h"
#include "overworld/terrain.h"
#include "overworld/vehicles.h"
#include "platform/audio.h"
#include "port/native.h"
#include "vars.h"

/* ---- What each takes ------------------------------------------------------------------------ */

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

bool overworld_leader_takes(const GameState *g, MenuObject self)
{
    return walker_runs(g, self, PARTY_LEADER, (WalkerTables){ LEADER_STEPS_ON_FOOT, LEADER_STEPS_BOARDING, true });
}

bool overworld_follower_takes(const GameState *g, MenuObject self)
{
    PartyPlace place = (PartyPlace)menu_object_slots_party_place(g, self);
    return (place == PARTY_FIRST_MEMBER || place == PARTY_SECOND_MEMBER)
           && walker_runs(g, self, place, (WalkerTables){ FOLLOWER_STEPS_ON_FOOT, FOLLOWER_STEPS_BOARDING, false });
}

/* The watcher looks the leader's tile up in lists as a map's setup loads
 * them, and keeps to the original's table of what an event can be. */
bool overworld_watcher_takes(const GameState *g, MenuObject self)
{
    (void)self;
    return overworld_event(g) <= 1 && overworld_lists_are_loaded(g);
}

bool overworld_region_song_takes(const GameState *g, MenuObject self) { return overworld_region_song_can_play(g, self); }

bool overworld_sign_takes(const GameState *g, MenuObject self) { return overworld_sign_can_show(g, self); }

/* The controller keeps to the travels and vehicles the game has. */
bool overworld_vehicles_take(const GameState *g, MenuObject self)
{
    enum { BOARDINGS = 4 };
    (void)self;
    return overworld_party_travel(g) < TRAVELS && overworld_boarding(g) < BOARDINGS;
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

/* In the air the map scrolls with a vehicle at its pace, whole pixels: two a
 * frame at most, across or down. */
static bool the_map_can_follow_the_vehicle(const GameState *g, MenuObject vehicle, bool flies_a_tile)
{
    enum { FASTEST = 2 };
    unsigned records = 2 * menu_map_scroll_records(MENU_ACROSS, FASTEST) + 2 * menu_map_scroll_records(MENU_DOWN, FASTEST);
    if (!menu_map_can_scroll(g, 1, records) || !menu_map_can_scroll(g, 2, records))
        return false;
    if (!flies_a_tile)
        return true;
    MenuPlace v = menu_object_velocity(g, vehicle);
    int16_t across = (int16_t)menu_pixel(v.x), down = (int16_t)menu_pixel(v.y);
    return (uint16_t)v.x == 0 && (uint16_t)v.y == 0 && across >= -FASTEST && across <= FASTEST && down >= -FASTEST
           && down <= FASTEST;
}

bool overworld_epoch_takes(const GameState *g, MenuObject self)
{
    EpochStep step = overworld_epoch_step(g, self);
    bool ok = step < EPOCH_STEPS && on_a_world_map(g) && menu_object_slots_facing(g, self) < 4
              && epoch_animates(g, self, step == EPOCH_SETS_DOWN || step == EPOCH_RUNS_A_SCENE);
    if (ok && (step == EPOCH_FLIES || step == EPOCH_FLIES_A_TILE))
        ok = the_map_can_follow_the_vehicle(g, self, step == EPOCH_FLIES_A_TILE);
    if (ok && step == EPOCH_RUNS_A_SCENE)
        ok = menu_object_script_can_run(g, menu_object_slots_script(g, self));
    return ok;
}

bool overworld_epoch_second_sprite_takes(const GameState *g, MenuObject self)
{
    return overworld_step(g, self) < 2 && epoch_animates(g, self, overworld_step(g, self) == 0)
           && menu_given_sprite_script_is_well_formed(g, VEHICLE_SECOND_SPRITE_STANDING)
           && menu_given_sprite_script_is_well_formed(g, VEHICLE_SECOND_SPRITE_IN_FLIGHT);
}

/* The dactyls keep to their steps, face one of the four ways, and animate
 * with sprite scripts the language reads (the one each has too, unless it is
 * set down and given one first); the leader's has the map follow it where it
 * flies, and the others are the second's and the third's, kept near their
 * places in the V (vehicles.h). */
static bool animations_are_sound(const GameState *g, const uint8_t *animations, unsigned count)
{
    for (unsigned i = 0; i < count; i++)
        if (!menu_given_sprite_script_is_well_formed(g, animations[i]))
            return false;
    return true;
}

static bool dactyl_animates(const GameState *g, MenuObject self, bool given_one_first)
{
    static const uint8_t ANIMATIONS[] = { DACTYL_STANDS, DACTYL_TAKES_OFF, SECOND_S_DACTYL_TAKES_OFF,
                                          THIRD_S_DACTYL_TAKES_OFF, VEHICLE_SECOND_SPRITE_STANDING,
                                          VEHICLE_SECOND_SPRITE_IN_FLIGHT };
    if (!menu_sprite_list_is_sound(g) || !menu_object_queue_is_whole(g)
        || !(given_one_first || menu_sprite_script_is_well_formed(g, self))
        || !animations_are_sound(g, ANIMATIONS, sizeof ANIMATIONS))
        return false;
    for (uint8_t facing = 0; facing < 4; facing++)
        if (!menu_given_sprite_script_is_well_formed(g, overworld_dactyl_flying_animation(g, facing))
            || !menu_given_sprite_script_is_well_formed(g, overworld_following_dactyl_flying_animation(g, 0, facing))
            || !menu_given_sprite_script_is_well_formed(g, overworld_following_dactyl_flying_animation(g, 1, facing)))
            return false;
    return true;
}

static bool a_follower(const GameState *g, MenuObject self)
{
    PartyPlace place = (PartyPlace)menu_object_slots_party_place(g, self);
    return place == PARTY_FIRST_MEMBER || place == PARTY_SECOND_MEMBER;
}

bool overworld_dactyl_takes(const GameState *g, MenuObject self)
{
    DactylStep step = (DactylStep)overworld_step(g, self);
    bool ok = step < DACTYL_STEPS && menu_object_slots_facing(g, self) < 4
              && dactyl_animates(g, self, step == DACTYL_SETS_DOWN);
    if (ok && (step == DACTYL_FLIES || step == DACTYL_FLIES_A_TILE))
        ok = the_map_can_follow_the_vehicle(g, self, step == DACTYL_FLIES_A_TILE);
    return ok;
}

bool overworld_dactyl_second_sprite_takes(const GameState *g, MenuObject self)
{
    return overworld_step(g, self) < DACTYL_SECOND_SPRITE_STEPS && overworld_dactyl_flying(g) <= 1
           && dactyl_animates(g, self, overworld_step(g, self) == 0);
}

bool overworld_following_dactyl_takes(const GameState *g, MenuObject self)
{
    FollowingDactylStep step = (FollowingDactylStep)overworld_step(g, self);
    return step < FOLLOWER_STEPS && a_follower(g, self) && menu_object_slots_facing(g, self) < 4
           && dactyl_animates(g, self, step == FOLLOWER_SETS_DOWN) && overworld_following_dactyl_keeps_near(g, self);
}

bool overworld_following_dactyl_second_sprite_takes(const GameState *g, MenuObject self)
{
    FollowingSecondSpriteStep step = (FollowingSecondSpriteStep)overworld_step(g, self);
    return step < FOLLOWERS_SPRITE_STEPS && a_follower(g, self)
           && dactyl_animates(g, self, step == FOLLOWERS_SPRITE_SETS_DOWN)
           && overworld_following_second_sprite_keeps_near(g, self);
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

bool overworld_reach_test_takes(const GameState *g)
{
    return a_reach_record(overworld_scratch_reach_first(g)) && a_reach_record(overworld_scratch_reach_second(g));
}

/* The scenes' calls are made on an object on the map, facing one of the four
 * ways. */
bool overworld_epoch_scene_call_takes(const GameState *g, MenuObject self)
{
    return menu_object_slots_facing(g, self) < 4 && menu_object_slots_x_pixel(g, self) < MENU_MAP_WIDTH;
}

/* Its place is on the region map where it looks a region up. */
bool overworld_location_song_takes(const GameState *g, MenuObject self)
{
    (void)self;
    return menu_song_kept(g) || overworld_location_has_a_region(g);
}

/* ---- The location's song --------------------------------------------------------------------- */

/* The driver ($C7:0004) reads nothing of the registers, and hands back X,
 * the carry and A's high byte as it found them. */
enum { SOUND_DRIVER = 0xC70004, LOCATION_SONG_SENT = 0xC22F49 };
#define DRIVER_IGNORES (DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C)

// $C2:2F0F
void overworld_start_the_location_song(GameState *g, Platform *pf, void (*looked_up)(void *ctx, LocationSong song),
                                       void *ctx)
{
    LocationSong song = overworld_location_song(g);
    looked_up(ctx, song);
    au_command(pf, PF_CALL_DC(LOCATION_SONG_SENT, SOUND_DRIVER, DRIVER_IGNORES), overworld_song_command(song.song));
}

/* ---- As C ------------------------------------------------------------------------------------- */

/* A walker's or the Epoch's script, run by the interpreter as C. */
typedef struct {
    GameState *game;
    Platform *pf;
    MenuObject self;
} ScriptOf;

static WalkerScriptEnd walker_script_as_c(void *ctx, WalkerScriptRun run)
{
    (void)run;
    ScriptOf *o = ctx;
    switch (menu_run_object_script_as_c(o->game, o->pf, o->self)) {
    case MENU_TURN_DONE: return WALKER_SCRIPT_DONE;
    case MENU_TURN_GOES_ON: return WALKER_SCRIPT_WAITS;
    default: return WALKER_SCRIPT_HANDED_OVER;
    }
}

static ScriptEnd epoch_scene_as_c(void *ctx)
{
    ScriptOf *o = ctx;
    switch (menu_run_object_script_as_c(o->game, o->pf, o->self)) {
    case MENU_TURN_DONE: return SCRIPT_DONE;
    case MENU_TURN_GOES_ON: return SCRIPT_WAITING;
    default: return SCRIPT_GAVE_BACK;
    }
}

static MenuTurn goes_on_unless(bool went_no_further) { return went_no_further ? MENU_TURN_WENT_NO_FURTHER : MENU_TURN_GOES_ON; }

static MenuTurn lead(GameState *g, Platform *pf, MenuObject self)
{
    ScriptOf o = { g, pf, self };
    WalkerScripts scripts = { walker_script_as_c, &o };
    return goes_on_unless(!overworld_lead(g, self, &scripts));
}

static MenuTurn follow(GameState *g, Platform *pf, MenuObject self)
{
    ScriptOf o = { g, pf, self };
    WalkerScripts scripts = { walker_script_as_c, &o };
    return goes_on_unless(!overworld_follow(g, self, &scripts));
}

static MenuTurn fly_the_epoch(GameState *g, Platform *pf, MenuObject self)
{
    ScriptOf o = { g, pf, self };
    EpochScripts scripts = { epoch_scene_as_c, &o };
    return goes_on_unless(!overworld_fly_the_epoch(g, self, &scripts));
}

/* The handlers that never finish of themselves. */
#define GOES_ON(name, work)                                                    \
    static MenuTurn name(GameState *g, Platform *pf, MenuObject self)        \
    {                                                                          \
        (void)pf, (void)self;                                                  \
        work;                                                                  \
        return MENU_TURN_GOES_ON;                                              \
    }
GOES_ON(watch_the_ground, overworld_watch_the_ground(g, self))
GOES_ON(play_the_region_song, overworld_play_the_region_song(g, self))
GOES_ON(show_the_sign, overworld_show_the_sign(g, self))
GOES_ON(control_the_vehicles, overworld_control_the_vehicles(g))
GOES_ON(watch_for_the_black_omen, overworld_watch_for_the_black_omen(g))
GOES_ON(fly_the_dactyl, overworld_fly_the_dactyl(g, self))
GOES_ON(draw_the_dactyl_s_second_sprite, overworld_draw_the_dactyl_s_second_sprite(g, self))
GOES_ON(fly_a_following_dactyl, overworld_fly_a_following_dactyl(g, self))
GOES_ON(draw_a_following_dactyl_s_second_sprite, overworld_draw_a_following_dactyl_s_second_sprite(g, self))

static MenuTurn draw_the_epoch_s_second_sprite(GameState *g, Platform *pf, MenuObject self)
{
    (void)pf;
    return menu_turn_done_if(overworld_draw_the_epoch_s_second_sprite(g, self));
}

MENU_HANDLERS(overworld_handlers,
    { 0x3404, "the party's leader walking the map", overworld_leader_takes, lead },
    { 0x3AE2, "a member following the leader", overworld_follower_takes, follow },
    { 0x3264, "the places' watcher", overworld_watcher_takes, watch_the_ground },
    { 0x2F94, "the regions' music", overworld_region_song_takes, play_the_region_song },
    { 0x5628, "the place's sign", overworld_sign_takes, show_the_sign },
    { 0x309E, "the vehicles' controller", overworld_vehicles_take, control_the_vehicles },
    { 0x42DD, "the Epoch", overworld_epoch_takes, fly_the_epoch },
    { 0x495E, "the Epoch's second sprite", overworld_epoch_second_sprite_takes, draw_the_epoch_s_second_sprite },
    { 0x49CF, "the watch for the Black Omen", NULL, watch_for_the_black_omen },
    { 0x4CC5, "the leader's dactyl", overworld_dactyl_takes, fly_the_dactyl },
    { 0x504B, "the leader's dactyl's second sprite", overworld_dactyl_second_sprite_takes,
      draw_the_dactyl_s_second_sprite },
    { 0x5097, "a member's dactyl", overworld_following_dactyl_takes, fly_a_following_dactyl },
    { 0x5494, "a member's dactyl's second sprite", overworld_following_dactyl_second_sprite_takes,
      draw_a_following_dactyl_s_second_sprite },
);

/* The routines the scenes call of the overworld's. */
static void nothing_to_leave(void *ctx, LocationSong song) { (void)ctx, (void)song; }

static bool start_the_location_song(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)self, (void)far;
    overworld_start_the_location_song(g, pf, nothing_to_leave, NULL);
    return true;
}

static bool clear_the_sign_label(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)pf, (void)self, (void)far;
    overworld_clear_the_sign_label(g);
    return true;
}

static bool show_the_epoch_flying(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)pf, (void)far;
    overworld_show_the_epoch_flying(g, self);
    return true;
}

static bool carry_the_epoch_round(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)pf, (void)far;
    overworld_carry_the_epoch_round(g, self);
    return true;
}

MENU_SCRIPT_ROUTINES(overworld_routines,
    { 0xC22F0F, "the song of where the party is", overworld_location_song_takes, start_the_location_song },
    { 0xC25798, "the sign's label cleared", NULL, clear_the_sign_label },
    { 0xC248A9, "the Epoch shown flying", overworld_epoch_scene_call_takes, show_the_epoch_flying },
    { 0xC248CD, "the Epoch carried round the map", overworld_epoch_scene_call_takes, carry_the_epoch_round },
);
