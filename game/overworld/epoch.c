/* The Epoch on the world map: the time machine the party travels through
 * time in, and, once it has its wings, flies over the map.
 *
 * The Epoch is an object of the mode (overworld_fly_the_epoch), standing on
 * the map where it was left, in the period it was left in. The party walks
 * up and boards it (vehicles.c); once all are aboard:
 *
 * - without its wings, the Epoch can only travel through time: the time
 *   gate's dial opens (the overworld's main loop runs it) and the Epoch waits
 *   on the player's choice;
 * - with them, it rises over the map, and the pad flies it a tile at a time,
 *   the map scrolling under it and the song of flight playing. Its buttons
 *   ask the main loop for the menus, the map view, or the time gate. The
 *   confirm button brings it down where the ground under it is open (no
 *   wall, no place's ground, no dactyl, and not in the part of the map it may
 *   not land in), or, over the Black Omen, takes the party into the Omen.
 *
 * Going through time, the Epoch leaves the map in a scene of its own (a
 * script), and arrives on the next period's map the same way; a few choices
 * have it arrive somewhere else, where a scene of the story's plays. Come
 * down, it stands where it landed and the party gets off.
 *
 * Beside it the Epoch has two more objects: the second sprite it is drawn
 * with (above it while it stands, below it in flight), and the watch for the
 * Black Omen under it in flight, which shows the Omen's name on the sign.
 *
 * Rising, flying and coming down are the same as the dactyl's (vehicles.c,
 * "In the air"); what is the Epoch's own -- the time gate, its scenes, the
 * Black Omen, its first flight as a scene -- is here. */
#include "overworld/vehicles.h"

#include "charge.h"
#include "menu/epoch.h"
#include "menu/sprites.h"
#include "original_effects.h"
#include "overworld/music.h"
#include "vars.h"

/* ---- Its state ----------------------------------------------------------------------------- */

static bool epoch_is(const GameState *g, uint8_t bits) { return overworld_vehicle_is(g, VEHICLE_EPOCH, bits); }
static void mark_epoch(GameState *g, uint8_t bits, bool on) { overworld_mark_vehicle(g, VEHICLE_EPOCH, bits, on); }

static bool has_its_wings(const GameState *g) { return epoch_is(g, EPOCH_HAS_ITS_WINGS); }

EpochStep overworld_epoch_step(const GameState *g, MenuObject epoch) { return (EpochStep)overworld_step(g, epoch); }

static void set_step(GameState *g, MenuObject self, EpochStep step) { overworld_set_step(g, self, step); }

/* Standing, the Epoch is drawn under the party; in the air, over it. */
static void draw(GameState *g, MenuObject self, bool over_the_party)
{
    overworld_draw_vehicle(g, self, EPOCH_PALETTE, over_the_party);
}

static Facing facing(const GameState *g, MenuObject self) { return (Facing)menu_object_slots_facing(g, self); }

static void animate(GameState *g) { overworld_animate(g); }

static void give_animation(GameState *g, MenuObject self, uint8_t animation)
{
    menu_give_sprite_script(g, self, animation);
}

// $C2:48A9
void overworld_show_the_epoch_flying(GameState *g, MenuObject epoch)
{
    overworld_show_vehicle_flying(g, VEHICLE_EPOCH, epoch, facing(g, epoch));
}

// $C2:48CD
/* In a scene of the Epoch's, it is carried a third of the way round the map
 * the way it flies across, and brought back onto the map. */
void overworld_carry_the_epoch_round(GameState *g, MenuObject epoch)
{
    int16_t way = overworld_pace(g, epoch, MENU_ACROSS) < 0 ? -EPOCH_CARRIED_ROUND : EPOCH_CARRIED_ROUND;
    set_menu_object_slots_x_pixel(g, epoch, (uint16_t)(menu_object_slots_x_pixel(g, epoch) + way));
    menu_object_wrap_onto_map(g, epoch);
}

static WorldSpot place(const GameState *g, MenuObject self) { return overworld_walker_spot(g, self); }

/* Where the Epoch is is where it stands for the party, the menus and the map
 * view. */
static void keep_its_spot(GameState *g, MenuObject self) { overworld_keep_vehicle_spot(g, VEHICLE_EPOCH, place(g, self)); }

/* Where the original clears the step's note with a word, the byte after it
 * goes with it; nothing reads it. */
static void clear_the_step_note_as_a_word(GameState *g)
{
    set_overworld_step_begun(g, 0);
    ORIGINAL_EFFECT(set_overworld_after_step_begun(g, 0));
}

static void stop(GameState *g, MenuObject self) { overworld_vehicle_stop(g, self); }

/* The ground under the Epoch: the four tiles round its spot, as a walker's
 * feet stand, and the two below them. It may land only where all six are
 * open ground, no wall and no place's ground. */
static void note_the_ground(GameState *g, MenuObject self, WorldSpot at)
{
    overworld_note_the_ground_under(g, VEHICLE_EPOCH, self, at);
}

/* ---- The time gate ----------------------------------------------------------------------------- */

void overworld_epoch_opens_the_time_gate(GameState *g, MenuObject self)
{
    mark_epoch(g, EPOCH_LEAVING_THROUGH_TIME, true);
    overworld_set_mode(g, OVERWORLD_TRAVELLING_THROUGH_TIME);
    set_step(g, self, EPOCH_AT_THE_TIME_GATE);
    set_menu_travel_location(g, 0);
}

/* Where the Epoch arrives in the location the player chose on the dial, in
 * 8x8 tiles: on the period's map where it stood as it left, facing the way it
 * faced; or, for these choices, somewhere else, facing down, where a scene of
 * the story's is played (the chosen location stands in for another). */
static const struct {
    uint16_t chosen;
    bool with_wings, without_wings;
    uint16_t goes_to; /* 0: the location chosen */
    uint8_t x, y;
} EPOCH_ARRIVALS[] = {
    { 0x01D9, true, true, 0, 0x07, 0x08 },
    { 0x01F7, false, true, 0x01D8, 0x07, 0x08 },
    { 0x01F7, true, false, 0x01B0, 0x07, 0x18 },
    { 0x01F2, false, true, 0x00F3, 0x08, 0x0C },
};
enum { NOTED_ARRIVING_WITHOUT_WINGS = 0x01, NOTED_FOR = 0x01F2 };

static void arrive_at(GameState *g, uint8_t x, uint8_t y, Facing f)
{
    set_location_x(g, x);
    set_location_y(g, y);
    overworld_set_arrival_facing(g, f);
}

/* Setting off for $1F2 before it has its wings, the original keeps the
 * Epoch's spot through the register it has just loaded with the arrival's
 * place rather than the Epoch's slot: what it keeps is the fourth object's
 * goal down and how far down it had to go. The Epoch then leaves the map
 * for location $F3, and its spot is set again before it is seen. */
static void keep_a_stray_spot(GameState *g)
{
    enum { FOURTH_OBJECT = 3 };
    set_overworld_epoch_x(g, menu_object_slots_goal_y(g, FOURTH_OBJECT));
    set_overworld_epoch_y(g, menu_object_slots_to_goal_y(g, FOURTH_OBJECT));
}

// $C2:476B-$C2:480C
static void set_off_through_time(GameState *g, MenuObject self, uint16_t chosen)
{
    set_step(g, self, EPOCH_RUNS_A_SCENE);
    set_menu_object_slots_command_frames(g, self, 0);
    set_menu_object_slots_script(g, self, EPOCH_LEAVES_THROUGH_TIME);
    bool wings = has_its_wings(g);
    for (unsigned i = 0; i < sizeof EPOCH_ARRIVALS / sizeof EPOCH_ARRIVALS[0]; i++) {
        if (EPOCH_ARRIVALS[i].chosen != chosen || !(wings ? EPOCH_ARRIVALS[i].with_wings : EPOCH_ARRIVALS[i].without_wings))
            continue;
        if (EPOCH_ARRIVALS[i].goes_to != 0)
            set_menu_travel_location(g, EPOCH_ARRIVALS[i].goes_to);
        arrive_at(g, EPOCH_ARRIVALS[i].x, EPOCH_ARRIVALS[i].y, FACING_DOWN);
        set_story_epoch_arrivals(g, 0);
        if (chosen == NOTED_FOR) {
            set_story_epoch_notes(g, (uint8_t)(story_epoch_notes(g) | NOTED_ARRIVING_WITHOUT_WINGS));
            keep_a_stray_spot(g);
        } else {
            keep_its_spot(g, self);
        }
        return;
    }
    WorldSpot at = place(g, self);
    arrive_at(g, (uint8_t)(at.x / TILE_PIXELS), (uint8_t)(at.y / TILE_PIXELS), facing(g, self));
    keep_its_spot(g, self);
}

// $C2:4728
/* At the time gate, the Epoch waits for the player's choice on the dial: a
 * location sets it off through time; leaving the dial, it flies on, or
 * without its wings lets the party out. */
static void wait_at_the_time_gate(GameState *g, MenuObject self)
{
    enum { LEFT_THE_DIAL = 0x8000 };
    uint16_t chosen = menu_travel_location(g);
    if (chosen == 0) {
        keep_its_spot(g, self);
        set_overworld_step_begun(g, 0);
    } else if (chosen & LEFT_THE_DIAL) {
        mark_epoch(g, EPOCH_LEAVING_THROUGH_TIME, false);
        keep_its_spot(g, self);
        if (has_its_wings(g)) {
            set_step(g, self, EPOCH_FLIES);
            set_overworld_step_begun(g, 0);
        } else {
            mark_epoch(g, VEHICLE_OUT, false);
            give_animation(g, self, EPOCH_STANDS_WITHOUT_WINGS);
            set_step(g, self, EPOCH_WAITS_FOR_THE_PARTY_TO_GET_OFF);
            stop(g, self);
            clear_the_step_note_as_a_word(g);
        }
    } else {
        set_off_through_time(g, self, chosen);
        set_overworld_step_begun(g, 0);
    }
    animate(g);
}

/* ---- Standing ---------------------------------------------------------------------------------- */


// $C2:42F8
/* As the map starts, and after it has come down, the Epoch is placed where
 * it stands. Where the party has it out it is in the air, or arriving through
 * time in its scene, which first puts the music back to its own pace. */
static void set_down(GameState *g, MenuObject self)
{
    WorldSpot at = overworld_vehicle_spot(g, VEHICLE_EPOCH);
    menu_object_set_place(g, self, (MenuPlace){ (uint32_t)at.x << 16, (uint32_t)at.y << 16 });
    set_menu_object_slots_tiles(g, self, 0);
    if (!epoch_is(g, VEHICLE_OUT)) {
        set_step(g, self, EPOCH_STANDS);
        draw(g, self, false);
        set_menu_object_slots_facing(g, self, FACING_DOWN);
        set_menu_object_slots_facing_before(g, self, FACING_DOWN);
        set_overworld_epoch_flying(g, false);
        give_animation(g, self, has_its_wings(g) ? EPOCH_STANDS_WITH_ITS_WINGS : EPOCH_STANDS_WITHOUT_WINGS);
        animate(g);
        return;
    }
    bool arriving = epoch_is(g, EPOCH_LEAVING_THROUGH_TIME);
    if (arriving)
        (void)overworld_restore_the_tempo(g);
    Facing arrival = overworld_arrival_facing(g);
    set_step(g, self, arriving ? EPOCH_RUNS_A_SCENE : EPOCH_FLIES);
    draw(g, self, true);
    set_menu_object_slots_facing(g, self, arrival);
    set_menu_object_slots_facing_before(g, self, arrival);
    mark_epoch(g, EPOCH_ENTERING_THE_BLACK_OMEN | EPOCH_OVER_THE_BLACK_OMEN, false);
    set_overworld_epoch_flying(g, true);
    if (arriving) {
        set_menu_object_slots_script(g, self, EPOCH_ARRIVES_THROUGH_TIME);
        set_menu_object_slots_command_frames(g, self, 0);
        return;
    }
    overworld_show_the_epoch_flying(g, self);
    note_the_ground(g, self, place(g, self));
    animate(g);
}

/* At one point of the story the Epoch's first flight is a scene: it rises,
 * and a script holds the map. */
enum { STORY_SHOWS_THE_FIRST_FLIGHT = 0xD3 };


// $C2:443C
/* Rising; risen, it flies, unless the story shows its first flight as a
 * scene. */
static void rise(GameState *g, MenuObject self)
{
    if (overworld_vehicle_rises(g, VEHICLE_EPOCH, self))
        return;
    if (menu_story_progress(g) == STORY_SHOWS_THE_FIRST_FLIGHT) {
        if (overworld_mode(g) == OVERWORLD_ON_THE_MAP) {
            overworld_set_mode(g, OVERWORLD_BACK_ON_THE_MAP);
            TIMED_AS(g, 0xC204D9, (void)menu_start_script(g, EPOCH_TAKES_OFF_IN_A_SCENE, MENU_ORDINARY_SLOT));
            animate(g);
        }
        return;
    }
    overworld_vehicle_takes_to_the_air(g, VEHICLE_EPOCH, self);
}

// $C2:43CB
/* Standing, it waits for the party to board; all aboard, it rises, or
 * without its wings opens the time gate. */
static void stand(GameState *g, MenuObject self)
{
    if (overworld_party_travel(g) != TRAVEL_IN_THE_EPOCH) {
        animate(g);
        return;
    }
    mark_epoch(g, VEHICLE_OUT, true);
    if (!has_its_wings(g)) {
        overworld_epoch_opens_the_time_gate(g, self);
        animate(g);
        return;
    }
    set_step(g, self, EPOCH_RISES);
    overworld_play_the_song_of_flight(g);
    note_the_ground(g, self, place(g, self));
    overworld_vehicle_climb(g, VEHICLE_EPOCH, self, -1);
    give_animation(g, self, EPOCH_TAKES_OFF);
    rise(g, self);
}

// $C2:4888
static void wait_for_the_party_to_get_off(GameState *g, MenuObject self)
{
    if (overworld_party_travel(g) == TRAVEL_GETTING_OFF)
        set_step(g, self, EPOCH_SETS_DOWN);
    animate(g);
}

/* ---- In the air -------------------------------------------------------------------------------- */

// $C2:4510
/* Over the Black Omen the confirm button takes the party into it, once. */
bool overworld_epoch_takes_the_party_into_the_omen(GameState *g)
{
    if (!epoch_is(g, EPOCH_OVER_THE_BLACK_OMEN))
        return false;
    if (!epoch_is(g, EPOCH_ENTERING_THE_BLACK_OMEN)) {
        overworld_set_mode(g, OVERWORLD_BACK_ON_THE_MAP);
        mark_epoch(g, EPOCH_ENTERING_THE_BLACK_OMEN, true);
        TIMED_AS(g, 0xC204D9, (void)menu_start_script(g, EPOCH_ENTERS_THE_BLACK_OMEN, MENU_ORDINARY_SLOT));
    }
    return true;
}

/* ---- Its scenes -------------------------------------------------------------------------------- */

// $C2:4823
/* A frame of the scene it leaves or arrives in. Over, it is the party's in
 * the air again, or, without its wings, stands where it arrived with the
 * party getting out. */
static bool play_its_scene(GameState *g, MenuObject self, const EpochScripts *scripts)
{
    ScriptEnd end = scripts->run(scripts->ctx);
    if (end == SCRIPT_GAVE_BACK)
        return false;
    if (end == SCRIPT_DONE) {
        mark_epoch(g, EPOCH_LEAVING_THROUGH_TIME, false);
        if (has_its_wings(g)) {
            set_step(g, self, EPOCH_FLIES);
            note_the_ground(g, self, place(g, self));
        } else {
            mark_epoch(g, VEHICLE_OUT, false);
            give_animation(g, self, EPOCH_STANDS_WITHOUT_WINGS);
            set_step(g, self, EPOCH_SETS_DOWN);
            stop(g, self);
        }
        animate(g);
    }
    keep_its_spot(g, self);
    set_overworld_step_begun(g, 0);
    return true;
}

// $C2:42DD
bool overworld_fly_the_epoch(GameState *g, MenuObject self, const EpochScripts *scripts)
{
    switch (overworld_epoch_step(g, self)) {
    case EPOCH_SETS_DOWN: set_down(g, self); break;
    case EPOCH_STANDS: stand(g, self); break;
    case EPOCH_RISES: rise(g, self); break;
    case EPOCH_FLIES: overworld_vehicle_flies(g, VEHICLE_EPOCH, self); break;
    case EPOCH_FLIES_A_TILE: overworld_vehicle_flies_a_tile(g, VEHICLE_EPOCH, self); break;
    case EPOCH_COMES_DOWN: overworld_vehicle_comes_down(g, VEHICLE_EPOCH, self); break;
    case EPOCH_AT_THE_TIME_GATE: wait_at_the_time_gate(g, self); break;
    case EPOCH_RUNS_A_SCENE: return play_its_scene(g, self, scripts);
    default: wait_for_the_party_to_get_off(g, self); break;
    }
    return true;
}

/* ---- Its second sprite ------------------------------------------------------------------------- */

enum { SECOND_SPRITE_STARTS = 0, SECOND_SPRITE_FOLLOWS = 1 };

// $C2:495E
/* The second sprite the Epoch is drawn with, which follows it: below it in
 * the air (once it has its wings), above it on the ground. Where the Epoch
 * isn't on this map, the object is done at once. */
bool overworld_draw_the_epoch_s_second_sprite(GameState *g, MenuObject self)
{
    enum { LOCATION_NUMBER = 0x01FF };
    if (overworld_step(g, self) == SECOND_SPRITE_STARTS) {
        if ((location_id(g) & LOCATION_NUMBER) != menu_epoch_location(g))
            return true;
        overworld_set_step(g, self, SECOND_SPRITE_FOLLOWS);
        draw(g, self, true);
        set_menu_object_slots_tiles(g, self, 0);
    }
    WorldSpot epoch = overworld_vehicle_spot(g, VEHICLE_EPOCH);
    bool in_the_air = has_its_wings(g) && overworld_epoch_flying(g);
    give_animation(g, self, in_the_air ? VEHICLE_SECOND_SPRITE_IN_FLIGHT : VEHICLE_SECOND_SPRITE_STANDING);
    int16_t below = in_the_air ? VEHICLE_HEIGHT_IN_THE_AIR : -VEHICLE_HEIGHT_IN_THE_AIR;
    overworld_put_walker(g, self, (WorldSpot){ epoch.x, (uint16_t)(epoch.y + below) });
    animate(g);
    return false;
}

/* ---- The Black Omen ---------------------------------------------------------------------------- */

// $C2:49CF
/* While the party flies the Epoch (not away through time), passing over the
 * Black Omen shows its name and lets the confirm button take the party in. */
void overworld_watch_for_the_black_omen(GameState *g)
{
    if (overworld_party_travel(g) != TRAVEL_IN_THE_EPOCH || epoch_is(g, EPOCH_LEAVING_THROUGH_TIME))
        return;
    WorldSpot omen = { overworld_black_omen_x(g), overworld_black_omen_y(g) };
    bool over = overworld_within_reach(g, overworld_vehicle_spot(g, VEHICLE_EPOCH), REACH_OF_THE_EPOCH_IN_FLIGHT, omen,
                                       REACH_OF_THE_BLACK_OMEN);
    mark_epoch(g, EPOCH_OVER_THE_BLACK_OMEN, over);
    set_overworld_sign(g, over ? OVERWORLD_BLACK_OMEN_NAME : 0);
    uint8_t news = overworld_ground_news(g);
    set_overworld_ground_news(g, (uint8_t)(over ? news | GROUND_NEWS_ON_A_PLACE : news & ~GROUND_NEWS_ON_A_PLACE));
}
