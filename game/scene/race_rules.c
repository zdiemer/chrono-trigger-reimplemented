/* Scenes: the jet bike race's rules.
 *
 * Crono and Johnny race to a distance of $80000 along the track. Each frame
 * every bike slows down a little and speeds up toward its top speed, and moves
 * on by a 64th of its speed; who leads decides the top speeds, so Johnny,
 * faster behind, keeps the race close. Crono steers across the track with the
 * control pad (which direction steers which way depends on how the camera
 * faces) and has three boosts on B; Johnny steers by a rule of his own,
 * blocking Crono while he leads and swerving away when Crono pulls alongside
 * ahead. Bikes that touch bump: the one behind takes the one ahead's place
 * and they swap speeds, or they push each other across and swap drifts. Crono
 * scores two points a frame he leads and loses one a frame he trails, gains 20
 * for bumping Johnny from behind and loses 10 for being bumped.
 *
 * When Johnny reaches the line first, the race is over: the distance bar's
 * markers go, the camera follows him and Crono's score drains away. When Crono
 * does, his score is kept for the story with what it earns, the victory song
 * starts, and he coasts over to a lane while Johnny drops out of the picture.
 * Then a button ends the scene, and A fades it out first.
 *
 * Every sum below is the original's, carries included. It never clears the
 * carry between the sums of a frame's motion, so a drift that builds up or dies
 * away, a bump's push, a boost, each takes a one more or less from how the sum
 * or comparison before it came out; those ones are part of how the bikes move,
 * and this file keeps them as named outcomes of the step that made them. */
#include "scene/race.h"

#include "original_effects.h"
#include "platform/audio.h"
#include "platform/input.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The bikes ------------------------------------------------------------------------------ */

enum {
    SPEED_LOSS = 0x80,             /* every frame */
    CRONO_SPEEDING_UP = 0x100,     /* while below his top speed */
    JOHNNY_SPEEDING_UP = 0xF8,
    MAX_SPEED = 0x7FFF,
    BOOST = 0x1700,
    BOOST_FRAMES = 0x80,
    MAX_DRIFT = 0x210,
    DRIFT_LOSS = 0x10,             /* every frame */
    CRONO_STEERING = 0x28,         /* how much a frame's steering builds the drift up */
    JOHNNY_STEERING = 0x18,
    LAST_LANE = 0x7000,
    SPEED_TO_DISTANCE = 6,         /* a bike moves on by its speed over 64 */
    SPEED_TO_DISTANCE_MASK = 0x1FF,
};

/* The finish, as the bikes' distances reach it: the high word at 8. */
enum { FINISHED_HIGH_WORD = RACE_FINISH >> 16 };

static uint32_t distance(const GameState *g, Racer r) { return scene_race_racers_distance(g, r); }
static uint16_t speed(const GameState *g, Racer r) { return scene_race_racers_speed(g, r); }
static uint16_t lane(const GameState *g, Racer r) { return scene_race_racers_lane(g, r); }

static bool finished(const GameState *g, Racer r) { return (distance(g, r) >> 16) == FINISHED_HIGH_WORD; }

static RaceStage stage(const GameState *g) { return (RaceStage)scene_race_stage(g); }

/* Compared as the race compares: by the sign of the difference. */
static bool at_least(uint16_t a, uint16_t b) { return (int16_t)(a - b) >= 0; }

/* A bike's speed for this frame: the frame's loss, then what it gained, which
 * it gains again next frame only while still below its top speed. Johnny
 * gains it only with the throttle open. */
static void change_speed(GameState *g, Racer r, bool throttle)
{
    int16_t slower = (int16_t)(speed(g, r) - SPEED_LOSS);
    uint16_t v = slower < 0 ? 0 : (uint16_t)slower;
    if (throttle)
        v = (uint16_t)(v + scene_race_racers_speeding_up(g, r));
    set_scene_race_racers_speed(g, r, v);
}

static bool below_top_speed(const GameState *g, Racer r) { return at_least(scene_race_racers_top_speed(g, r), speed(g, r)); }

static void keep_speeding_up(GameState *g, Racer r, uint16_t speeding_up)
{
    set_scene_race_racers_speeding_up(g, r, below_top_speed(g, r) ? speeding_up : 0);
}

/* The top speed for the frame: the leading one, or the trailing one for
 * whichever bike is behind (Johnny counts as behind when they're level). */
static void set_top_speeds(GameState *g)
{
    bool crono_behind = (int16_t)scene_race_lead(g) < 0;
    for (Racer r = RACER_CRONO; r <= RACER_JOHNNY; r++) {
        bool behind = (r == RACER_CRONO) == crono_behind;
        set_scene_race_racers_top_speed(
            g, r, behind ? scene_race_racers_top_speed_trailing(g, r) : scene_race_racers_top_speed_leading(g, r));
    }
}

/* Each bike moves on along the track by its speed, and across it by its drifts,
 * kept between the first lane and the last. */
static void move_bike(GameState *g, Racer r)
{
    uint32_t moved = (uint32_t)(speed(g, r) >> SPEED_TO_DISTANCE & SPEED_TO_DISTANCE_MASK);
    set_scene_race_racers_distance(g, r, distance(g, r) + moved);

    uint16_t up = (uint16_t)(lane(g, r) + scene_race_racers_drift_up(g, r));
    if (at_least(up, LAST_LANE))
        up = LAST_LANE;
    int16_t down = (int16_t)(up - scene_race_racers_drift_down(g, r));
    set_scene_race_racers_lane(g, r, down < 0 ? 0 : (uint16_t)down);
}

/* ---- Drifting across the track -------------------------------------------------------------- */

/* A drift for the frame: built up while the bike steers its way, to at most
 * MAX_DRIFT, then dying away by DRIFT_LOSS, never below 0. `carried` is the
 * one the step before left (the drift before this one still going, or this
 * one reaching the top as it builds up): the build-up adds it, and the loss
 * is one more without it. Returns the one this drift leaves for the next:
 * whether it is still going after the loss. */
static bool drift(GameState *g, Racer r, bool up, bool steering, uint16_t build_up, bool carried)
{
    uint16_t v = up ? scene_race_racers_drift_up(g, r) : scene_race_racers_drift_down(g, r);
    if (steering) {
        v = (uint16_t)(v + build_up + carried);
        carried = v >= MAX_DRIFT;
        if (at_least(v, MAX_DRIFT))
            v = MAX_DRIFT;
    }
    int16_t left = (int16_t)(v - DRIFT_LOSS - !carried);
    bool still = v >= (uint16_t)(DRIFT_LOSS + !carried);
    v = left < 0 ? 0 : (uint16_t)left;
    if (up)
        set_scene_race_racers_drift_up(g, r, v);
    else
        set_scene_race_racers_drift_down(g, r, v);
    return still;
}

/* How Crono leans this frame (scene_race_crono_lean): steering both ways at
 * once, he leans down. */
typedef enum { LEAN_UPRIGHT = 0, LEAN_UP = 1, LEAN_DOWN = 2 } CronoLean;

static CronoLean lean_for(bool up, bool down) { return down ? LEAN_DOWN : up ? LEAN_UP : LEAN_UPRIGHT; }

/* Johnny's controls (scene_race_johnny_controls). */
enum { JOHNNY_THROTTLE = 0x8000, JOHNNY_STEERS_DOWN = 0x0100, JOHNNY_STEERS_UP = 0x0200 };

/* Both bikes' drifts for the frame, Crono's by the pad, Johnny's by his
 * controls, each drift's one carried into the next. */
static void steer(GameState *g, bool carried)
{
    PadBits pad = scene_race_buttons(g);
    bool up = pad & scene_race_steer_buttons(g, 1), down = pad & scene_race_steer_buttons(g, 0);
    set_scene_race_crono_lean(g, lean_for(up, down));
    carried = drift(g, RACER_CRONO, true, up, CRONO_STEERING, carried);
    carried = drift(g, RACER_CRONO, false, down, CRONO_STEERING, carried);
    uint16_t johnny = scene_race_johnny_controls(g);
    carried = drift(g, RACER_JOHNNY, true, johnny & JOHNNY_STEERS_UP, JOHNNY_STEERING, carried);
    drift(g, RACER_JOHNNY, false, johnny & JOHNNY_STEERS_DOWN, JOHNNY_STEERING, carried);
}

/* ---- Johnny ----------------------------------------------------------------------------------- */

/* Johnny opens the throttle while the race runs. Ahead of him, Crono scores;
 * then if Crono comes alongside Johnny swerves away from him, to the lower
 * lanes when Crono is on the higher side of the track. Behind or level, Crono
 * loses a point, and Johnny steers into his lane to block him. Returns the one
 * the decision's last comparison leaves, which the drifts carry on with. */
enum { ALONGSIDE = 0x20, TRACK_MIDDLE = 0x40 };

static bool drive_johnny(GameState *g)
{
    uint16_t controls = JOHNNY_THROTTLE;
    int16_t lead = (int16_t)scene_race_lead(g);
    bool carried;
    if (lead > 0) {
        set_scene_race_score(g, (uint16_t)(scene_race_score(g) + 2));
        uint16_t crono = race_lane_high(g, RACER_CRONO), johnny = race_lane_high(g, RACER_JOHNNY);
        uint16_t apart = (uint16_t)(crono - johnny);
        if ((int16_t)apart < 0)
            apart = (uint16_t)(johnny - crono);
        carried = apart >= ALONGSIDE;
        if (!at_least(apart, ALONGSIDE)) {
            carried = crono >= TRACK_MIDDLE;
            controls |= at_least(crono, TRACK_MIDDLE) ? JOHNNY_STEERS_DOWN : JOHNNY_STEERS_UP;
        }
    } else {
        set_scene_race_score(g, (uint16_t)(scene_race_score(g) - 1));
        uint16_t crono = lane(g, RACER_CRONO), johnny = lane(g, RACER_JOHNNY);
        carried = crono >= johnny;
        controls |= at_least(crono, johnny) ? JOHNNY_STEERS_UP : JOHNNY_STEERS_DOWN;
    }
    set_scene_race_johnny_controls(g, controls);
    return carried;
}

/* ---- Boosts ----------------------------------------------------------------------------------- */

/* The boost gauge, in the road strip's pixels: two bytes of each of two rows,
 * which the race shifts while a boost is on, and clears when one starts. */
enum { GAUGE_TOP = 0x11, GAUGE_TOP_COPY = 0x13, GAUGE_BOTTOM = 0x31, GAUGE_BOTTOM_COPY = 0x33 };
enum { GAUGE_FLASH_EVERY = 8 };

static void set_gauge(GameState *g, uint8_t top, uint8_t bottom)
{
    set_scene_race_road_strip_byte(g, GAUGE_TOP, top);
    set_scene_race_road_strip_byte(g, GAUGE_TOP_COPY, top);
    set_scene_race_road_strip_byte(g, GAUGE_BOTTOM, bottom);
    set_scene_race_road_strip_byte(g, GAUGE_BOTTOM_COPY, bottom);
}

/* Every eighth frame of a boost, with boosts left, the gauge's pixels move
 * along a place, lit pixels coming in at the top row's left and the top row's
 * last going into the bottom row. */
static void flash_gauge(GameState *g)
{
    uint8_t top = scene_race_road_strip_byte(g, GAUGE_TOP), bottom = scene_race_road_strip_byte(g, GAUGE_BOTTOM);
    set_gauge(g, (uint8_t)(top >> 1 | 0x80), (uint8_t)(bottom >> 1 | (top & 1) << 7));
}

/* A boost, while the race runs: B pressed (once a press) with boosts left
 * sends Crono $1700 faster, and one more when Johnny is below his top speed
 * (the one his speeding up's comparison leaves). */
static void boost(GameState *g, bool johnny_below_top)
{
    uint16_t timer = scene_race_boost_timer(g);
    if (timer != 0) {
        if (scene_race_boosts(g) != 0 && timer % GAUGE_FLASH_EVERY == 0)
            flash_gauge(g);
        set_scene_race_boost_timer(g, (uint16_t)(timer - 1));
        return;
    }
    if (!(scene_race_buttons(g) & IN_BUTTON_B)) {
        set_scene_race_boost_held(g, 0);
        return;
    }
    if (scene_race_boost_held(g) != 0)
        return;
    set_scene_race_boost_held(g, 1);
    uint16_t boosts = scene_race_boosts(g);
    if (boosts == 0)
        return;
    set_scene_race_boosts(g, (uint16_t)(boosts - 1));
    set_scene_race_boost_timer(g, BOOST_FRAMES);
    set_gauge(g, 0, 0);
    int16_t faster = (int16_t)(speed(g, RACER_CRONO) + BOOST + johnny_below_top);
    set_scene_race_racers_speed(g, RACER_CRONO, faster < 0 ? MAX_SPEED : (uint16_t)faster);
}

/* ---- The lead and the camera's place ---------------------------------------------------------- */

/* Where the camera is along the track, between the two bikes, and its place
 * on the track's loop of $400. */
enum { PLACE_TO_CAMERA = 4, CAMERA_PLACE_START = 0x375, CAMERA_LOOP = 0x3FF };

static void place_camera(GameState *g)
{
    uint32_t place = (distance(g, RACER_CRONO) + distance(g, RACER_JOHNNY)) >> 1;
    set_scene_race_place(g, place);
    set_race_camera(g, CAMERA_PLACE, (uint16_t)(((uint16_t)place >> PLACE_TO_CAMERA) + CAMERA_PLACE_START) & CAMERA_LOOP);
}

/* How far Crono leads, in 32nds of the distance: the two distances' words from
 * bit 5, less one more when Crono's bit 4 is clear (the original subtracts with
 * the bit its shifts carry out last). Returns whether the subtraction didn't
 * borrow, the one the bump's measuring carries on with. */
enum { LEAD_SHIFT = 5 };

static bool measure_lead(GameState *g)
{
    uint32_t crono = distance(g, RACER_CRONO), johnny = distance(g, RACER_JOHNNY);
    uint16_t crono_far = (uint16_t)(crono >> LEAD_SHIFT), johnny_far = (uint16_t)(johnny >> LEAD_SHIFT);
    bool half = crono & (1u << (LEAD_SHIFT - 1));
    set_scene_race_lead(g, (uint16_t)(crono_far - johnny_far - !half));
    /* the shifts' work, in the arithmetic's cells */
    set_scene_call_first(g, scene_program_own(), (uint16_t)(crono >> 16 >> LEAD_SHIFT));
    set_scene_call_second(g, scene_program_own(), johnny_far);
    return (uint32_t)crono_far >= (uint32_t)johnny_far + !half;
}

/* ---- Bumping ------------------------------------------------------------------------------------ */

/* Where Johnny is from Crono (scene_race_contact). */
enum {
    CRONO_WELL_AHEAD = 0x01, JOHNNY_WELL_AHEAD = 0x02,
    JOHNNY_WELL_ABOVE = 0x10, JOHNNY_WELL_BELOW = 0x20,
    ACROSS_SHIFT = 4,
};

/* How close counts as touching: along the track, Crono's bike from $100 on
 * and Johnny's from $C0 on, $200 and $180 apart; across it, $1000. The
 * original measures along with the distances' low words only. */
enum { CRONO_NOSE = 0x100, JOHNNY_NOSE = 0xC0, BEHIND_REACH = 0x200, AHEAD_REACH = 0x180, ACROSS_REACH = 0x1000 };

static uint16_t measure_contact(GameState *g, bool carried)
{
    uint32_t crono_nose = (uint32_t)(uint16_t)distance(g, RACER_CRONO) + CRONO_NOSE + carried;
    uint32_t johnny_nose = (uint32_t)(uint16_t)distance(g, RACER_JOHNNY) + JOHNNY_NOSE + (crono_nose >> 16);
    uint16_t c = (uint16_t)crono_nose, j = (uint16_t)johnny_nose;
    set_scene_call_first(g, scene_program_own(), c);
    set_scene_call_second(g, scene_program_own(), j);

    uint32_t johnny_back = (uint32_t)j + BEHIND_REACH + (johnny_nose >> 16);
    uint16_t along = 0;
    if (!at_least((uint16_t)johnny_back, c))
        along = CRONO_WELL_AHEAD;
    else if (at_least((uint16_t)(j - AHEAD_REACH - !((uint16_t)johnny_back >= c)), c))
        along = JOHNNY_WELL_AHEAD;

    uint16_t crono_lane = lane(g, RACER_CRONO), johnny_lane = lane(g, RACER_JOHNNY);
    uint16_t johnny_below = (uint16_t)(johnny_lane - ACROSS_REACH);
    uint16_t across = 0;
    if (at_least(johnny_below, crono_lane))
        across = JOHNNY_WELL_ABOVE >> ACROSS_SHIFT;
    else {
        uint16_t johnny_above = (uint16_t)(johnny_lane + ACROSS_REACH + (johnny_below >= crono_lane));
        if (!at_least(johnny_above, crono_lane))
            across = JOHNNY_WELL_BELOW >> ACROSS_SHIFT;
    }
    set_scene_race_contact_across(g, across);
    uint16_t contact = (uint16_t)(across << ACROSS_SHIFT | along);
    set_scene_race_contact(g, contact);
    return contact;
}

/* What the bump leaves in the arithmetic's cells: the place the bike behind
 * was put back to, and the push, whose bytes the next multiply carries in from
 * (scene/math.c). */
static void leave_push(GameState *g, uint32_t behind, uint32_t push)
{
    SceneProgramPage own = scene_program_own();
    set_scene_call_first(g, own, (uint16_t)behind);
    set_scene_call_second(g, own, (uint16_t)(behind >> 16));
    set_scene_call_bump_push(g, own, push);
}

/* A bump along the track: the bike behind, `back`, takes the place of the one
 * ahead, `front` (the front of its bike against the back of the other's,
 * `front_nose` and `back_nose` along), and the one ahead is pushed back by
 * the push's high word, taken off both its words; then they swap speeds.
 * Returns the one the last subtraction leaves. */
static bool bump_along(GameState *g, Racer back, uint16_t back_nose, Racer front, uint16_t front_nose)
{
    uint32_t behind = distance(g, back) - back_nose;
    uint32_t ahead = distance(g, front) + front_nose;
    uint32_t push = ahead - behind;
    bool no_borrow = ahead >= behind;
    uint32_t was_back = distance(g, back);
    uint32_t pushed = was_back + push, moved = pushed + no_borrow;
    set_scene_race_racers_distance(g, back, moved);
    bool overflow = pushed < was_back || moved < pushed;
    uint16_t push_high = (uint16_t)(push >> 16);
    uint32_t was = distance(g, front);
    uint16_t low = (uint16_t)was, high = (uint16_t)(was >> 16);
    uint32_t low_after = (uint32_t)low - push_high - !overflow;
    bool low_kept = low >= (uint32_t)push_high + !overflow;
    uint32_t high_after = (uint32_t)high - push_high - !low_kept;
    set_scene_race_racers_distance(g, front, (uint32_t)(uint16_t)high_after << 16 | (uint16_t)low_after);
    leave_push(g, behind, push);

    uint16_t s = speed(g, back);
    set_scene_race_racers_speed(g, back, speed(g, front));
    set_scene_race_racers_speed(g, front, s);
    return high >= (uint32_t)push_high + !low_kept;
}

/* A bump across the track: the bikes push each other apart by how far they
 * overlap across ($1000 apart), `pushed` moving the other way, and swap their
 * drifts. `carried` is the one the step before left. */
static void bump_across(GameState *g, Racer from, Racer pushed, bool carried)
{
    uint16_t f = lane(g, from), p = lane(g, pushed);
    uint32_t reach = (uint32_t)f + ACROSS_REACH + carried;
    bool reach_carry = reach > 0xFFFF;
    uint16_t overlap = (uint16_t)((uint16_t)reach - p - !reach_carry);
    bool overlap_kept = (uint16_t)reach >= (uint32_t)p + !reach_carry;
    set_scene_call_first(g, scene_program_own(), overlap);
    uint16_t crono = lane(g, RACER_CRONO), johnny = lane(g, RACER_JOHNNY);
    if (from == RACER_CRONO) {
        uint16_t moved = (uint16_t)(crono - overlap - !overlap_kept);
        bool kept = crono >= (uint32_t)overlap + !overlap_kept;
        set_scene_race_racers_lane(g, RACER_CRONO, moved);
        set_scene_race_racers_lane(g, RACER_JOHNNY, (uint16_t)(johnny + overlap + kept));
    } else {
        uint32_t moved = (uint32_t)crono + overlap + overlap_kept;
        set_scene_race_racers_lane(g, RACER_CRONO, (uint16_t)moved);
        set_scene_race_racers_lane(g, RACER_JOHNNY, (uint16_t)(johnny - overlap - !(moved > 0xFFFF)));
    }
    uint16_t d = scene_race_racers_drift_down(g, RACER_CRONO);
    set_scene_race_racers_drift_down(g, RACER_CRONO, scene_race_racers_drift_down(g, RACER_JOHNNY));
    set_scene_race_racers_drift_down(g, RACER_JOHNNY, d);
    uint16_t u = scene_race_racers_drift_up(g, RACER_CRONO);
    set_scene_race_racers_drift_up(g, RACER_CRONO, scene_race_racers_drift_up(g, RACER_JOHNNY));
    set_scene_race_racers_drift_up(g, RACER_JOHNNY, u);
}

/* Bumping Johnny from behind scores 20, being bumped from behind costs 10. */
enum { SCORE_FOR_BUMPING = 20, SCORE_FOR_BEING_BUMPED = 10 };

/* The bikes have come together: how they bump depends on where they came
 * from, which the last contact apart says. */
static void bump(GameState *g)
{
    uint16_t from = scene_race_last_contact(g);
    bool carried = true;   /* the bump's own comparison found the contact it bumps by */
    switch (from) {
    case CRONO_WELL_AHEAD:
        (void)bump_along(g, RACER_CRONO, CRONO_NOSE, RACER_JOHNNY, JOHNNY_NOSE);
        set_scene_race_score(g, (uint16_t)(scene_race_score(g) + SCORE_FOR_BUMPING));
        return;
    case JOHNNY_WELL_AHEAD:
        (void)bump_along(g, RACER_JOHNNY, JOHNNY_NOSE, RACER_CRONO, CRONO_NOSE);
        set_scene_race_score(g, (uint16_t)(scene_race_score(g) - SCORE_FOR_BEING_BUMPED));
        return;
    case JOHNNY_WELL_ABOVE:
        bump_across(g, RACER_CRONO, RACER_JOHNNY, carried);
        break;
    case JOHNNY_WELL_ABOVE | CRONO_WELL_AHEAD:
        carried = bump_along(g, RACER_CRONO, CRONO_NOSE, RACER_JOHNNY, JOHNNY_NOSE);
        bump_across(g, RACER_CRONO, RACER_JOHNNY, carried);
        break;
    case JOHNNY_WELL_ABOVE | JOHNNY_WELL_AHEAD:
        bump_across(g, RACER_CRONO, RACER_JOHNNY, carried);
        (void)bump_along(g, RACER_JOHNNY, JOHNNY_NOSE, RACER_CRONO, CRONO_NOSE);
        break;
    case JOHNNY_WELL_BELOW:
        bump_across(g, RACER_JOHNNY, RACER_CRONO, carried);
        break;
    case JOHNNY_WELL_BELOW | CRONO_WELL_AHEAD:
        carried = bump_along(g, RACER_CRONO, CRONO_NOSE, RACER_JOHNNY, JOHNNY_NOSE);
        bump_across(g, RACER_JOHNNY, RACER_CRONO, carried);
        break;
    case JOHNNY_WELL_BELOW | JOHNNY_WELL_AHEAD:
        bump_across(g, RACER_JOHNNY, RACER_CRONO, carried);
        (void)bump_along(g, RACER_JOHNNY, JOHNNY_NOSE, RACER_CRONO, CRONO_NOSE);
        break;
    default:
        break;
    }
}

/* ---- A frame of the bikes ----------------------------------------------------------------------- */

/* What a frame of the bikes asks of the sound driver, which the entry's
 * wrapper sends: the bump's sound, then, while the race runs, the engine's
 * pitch (each unless a song is on its way to the driver at the time). */
typedef struct {
    bool bumped;
    bool running;
} BikeSounds;

/* The bikes for a frame, before they move: speeds, the boost, the top speeds,
 * the camera's place, Johnny's controls, the drifts, the lead; while the race
 * runs, whether they touch and their bump. */
static BikeSounds ride_bikes(GameState *g)
{
    BikeSounds sounds = { false, stage(g) == RACE_RUNNING };
    bool johnny_throttle = scene_race_johnny_controls(g) & JOHNNY_THROTTLE;
    change_speed(g, RACER_CRONO, true);
    change_speed(g, RACER_JOHNNY, johnny_throttle);
    set_scene_race_racers_speeding_up(g, RACER_CRONO, CRONO_SPEEDING_UP);
    set_scene_race_racers_speeding_up(g, RACER_JOHNNY, JOHNNY_SPEEDING_UP);
    keep_speeding_up(g, RACER_CRONO, CRONO_SPEEDING_UP);
    keep_speeding_up(g, RACER_JOHNNY, JOHNNY_SPEEDING_UP);
    bool johnny_below_top = scene_race_racers_top_speed(g, RACER_JOHNNY) >= speed(g, RACER_JOHNNY);
    bool running = sounds.running;
    if (running)
        boost(g, johnny_below_top);

    set_top_speeds(g);
    place_camera(g);

    set_scene_race_johnny_controls(g, 0);
    bool carried = running ? drive_johnny(g) : false;
    steer(g, carried);
    bool lead_kept = measure_lead(g);
    if (!running)
        return sounds;

    uint16_t contact = measure_contact(g, lead_kept);
    if (contact != 0) {
        set_scene_race_last_contact(g, contact);
    } else {
        bump(g);
        sounds.bumped = true;
    }
    return sounds;
}

static void move_bikes(GameState *g)
{
    move_bike(g, RACER_CRONO);
    move_bike(g, RACER_JOHNNY);
}

/* ---- The finish --------------------------------------------------------------------------------- */

/* Who won (scene_race_winner), as the story reads it. */
enum { WINNER_CRONO = 1, WINNER_JOHNNY = 2 };

/* What Crono's winning score earns (scene_race_prize). */
enum {
    PRIZE_REPDIGIT = 1, PRIZE_777 = 2, PRIZE_STRAIGHT = 3, PRIZE_ROUND = 4,
    PRIZE_OVER_1300 = 0x20, PRIZE_OVER_1500 = 0x40, PRIZE_OVER_2000 = 0x80,
};
enum { LUCKY_SCORES = 11 };

static uint8_t lucky_prize(const GameState *g, uint16_t shown)
{
    if (shown == 777)
        return PRIZE_777;
    if (shown == 123 || shown == 1234)
        return PRIZE_STRAIGHT;
    if (shown == 1000 || shown == 2000 || shown == 3000)
        return PRIZE_ROUND;
    for (unsigned i = 0; i < LUCKY_SCORES; i++)
        if (shown == scene_race_lucky_scores(g, i))
            return PRIZE_REPDIGIT;
    return 0;
}

/* The score's own prizes, by how far past 1300, 1500 and 2000 it got. */
static uint8_t score_prize(uint16_t shown)
{
    if (!at_least(shown, 1300))
        return 0;
    if (!at_least(shown, 1500))
        return PRIZE_OVER_1300;
    if (!at_least(shown, 2000))
        return PRIZE_OVER_1300 | PRIZE_OVER_1500;
    return PRIZE_OVER_1300 | PRIZE_OVER_1500 | PRIZE_OVER_2000;
}

/* After the finish Crono coasts at a top speed of $7000; in the demo Johnny's
 * drops to $4000 instead. Johnny's picture goes to its farthest size, and
 * the fades go a step every 5 frames. */
enum { COASTING_SPEED = 0x7000, DEMO_JOHNNY_SPEED = 0x4000, JOHNNY_FARTHEST = 0x0FFF, FINISH_FADE = 5 };
enum { VICTORY_SONG = 0x1B };
enum { LOWER_FINISH_LANE = 0x2000, HIGHER_FINISH_LANE = 0x6000 };

// $7E:4788
/* Crono crosses the line first: the winner and his score, with what it earns,
 * kept for the story; the victory song; his lead kept to die away; the camera's
 * turning and the pause stopped; and in the demo the two bikes are sent to the
 * lanes on their own sides. */
static void crono_finishes(GameState *g)
{
    set_scene_race_winner(g, WINNER_CRONO);
    set_scene_race_prize(g, 0);
    set_scene_race_finish_margin(g, scene_race_lead(g) & 0xFF);
    if (!race_in_attract_mode(g)) {
        set_scene_race_racers_top_speed_leading(g, RACER_CRONO, COASTING_SPEED);
        set_scene_race_racers_top_speed_trailing(g, RACER_CRONO, COASTING_SPEED);
    } else {
        set_scene_race_racers_top_speed_leading(g, RACER_JOHNNY, DEMO_JOHNNY_SPEED);
        set_scene_race_racers_top_speed_trailing(g, RACER_JOHNNY, DEMO_JOHNNY_SPEED);
    }
    set_race_channel_1(g, RACE_JOHNNY_SCALE, JOHNNY_FARTHEST);
    set_scene_fade_period(g, FINISH_FADE);
    set_scene_fade_countdown(g, FINISH_FADE);

    uint16_t shown = scene_race_score(g) >> 1;
    set_scene_race_final_score(g, shown);
    uint8_t lucky = lucky_prize(g, shown);
    if (lucky)
        set_scene_race_prize(g, lucky);
    set_scene_song(g, VICTORY_SONG);
    set_scene_race_prize(g, scene_race_prize(g) | score_prize(shown));

    scene_remove_task(g, SCENE_FRAME_TASKS, TASK_TURN_CAMERA);
    scene_add_task(g, SCENE_FRAME_TASKS, TASK_AWAIT_VICTORY_SONG);
    set_scene_race_cartridge_column(g, (uint16_t)(scene_race_cartridge_column(g) + 1));
    set_scene_race_stage(g, (uint16_t)(scene_race_stage(g) + 1));
    set_scene_pause_enabled(g, 0);
    set_scene_race_racers_drift_up(g, RACER_CRONO, 0);
    set_scene_race_racers_drift_down(g, RACER_CRONO, 0);
    if (at_least(lane(g, RACER_CRONO), lane(g, RACER_JOHNNY))) {
        set_scene_race_finish_lanes(g, RACER_CRONO, HIGHER_FINISH_LANE);
        set_scene_race_finish_lanes(g, RACER_JOHNNY, LOWER_FINISH_LANE);
    }
}

/* Johnny's win: the camera follows him out at the bikes' mean speed, Crono's
 * score drains away, and channel 0 goes on to the script that ends the race. */
enum { JOHNNY_WON_SCRIPT = (uint16_t)0x7E4211, MARKERS_STOPPED = 0x60, CAMERA_SPEED_SHIFT = 11 };

static void johnny_wins(GameState *g)
{
    set_scene_race_winner(g, WINNER_JOHNNY);
    set_scene_race_prize(g, 0);
    scene_remove_task(g, SCENE_FRAME_TASKS, TASK_TURN_CAMERA);
    set_scene_race_markers_shown(g, MARKERS_STOPPED);
    uint16_t both = (uint16_t)(speed(g, RACER_CRONO) + speed(g, RACER_JOHNNY) + 1);
    set_race_camera(g, CAMERA_SPEED, both >> CAMERA_SPEED_SHIFT);
    scene_add_task(g, SCENE_VBLANK_TASKS, TASK_FOLLOW_FINISH);
    scene_add_task(g, SCENE_FRAME_TASKS, TASK_DRAIN_SCORE);
    set_scene_channels_script(g, 0, JOHNNY_WON_SCRIPT);
}

/* The plane's seam column: the race's own copy once the camera is past $1000
 * along, the cartridge's again from $7F200 on. */
enum { COPY_FROM = 0x1000, CARTRIDGE_FROM = 0x7F200, COPY_AGAIN_FROM = 0x80400 };

typedef enum { SEAM_KEPT, SEAM_TO_COPY, SEAM_TO_CARTRIDGE } SeamChange;

static SeamChange seam_change(const GameState *g)
{
    uint32_t place = scene_race_place(g);
    uint16_t high = (uint16_t)(place >> 16), low = (uint16_t)place;
    bool cartridge = scene_race_cartridge_column(g) != 0;
    if (high == 0 && at_least(low, COPY_FROM) && cartridge)
        return SEAM_TO_COPY;
    if (high == CARTRIDGE_FROM >> 16 && low >= (uint16_t)CARTRIDGE_FROM && !cartridge)
        return SEAM_TO_CARTRIDGE;
    return SEAM_KEPT;
}

static void keep_plane_seam(GameState *g)
{
    switch (seam_change(g)) {
    case SEAM_TO_COPY:
        set_scene_race_cartridge_column(g, 0);
        scene_add_task(g, SCENE_VBLANK_TASKS, TASK_COLUMN_FROM_COPY);
        break;
    case SEAM_TO_CARTRIDGE:
        set_scene_race_cartridge_column(g, 1);
        scene_add_task(g, SCENE_VBLANK_TASKS, TASK_COLUMN_FROM_CARTRIDGE);
        break;
    case SEAM_KEPT:
        break;
    }
}

/* Johnny is out of sight when Crono is $1000 ahead (by the distances' high
 * words and then their low words, compared as the race compares), and so,
 * then, is Crono's own sprite. */
enum { OUT_OF_SIGHT = 0x1000 };

static void see_who_is_ahead(GameState *g)
{
    set_scene_race_racers_out_of_sight(g, RACER_JOHNNY, 0);
    set_scene_race_racers_out_of_sight(g, RACER_CRONO, 0);
    uint32_t crono = distance(g, RACER_CRONO), johnny = distance(g, RACER_JOHNNY);
    uint16_t low = (uint16_t)((uint16_t)crono - (uint16_t)johnny);
    uint16_t high = (uint16_t)((crono >> 16) - (johnny >> 16) - ((uint16_t)crono < (uint16_t)johnny));
    set_scene_call_first(g, scene_program_own(), low);
    set_scene_call_second(g, scene_program_own(), high);
    if ((int16_t)high >= 0 && at_least(low, OUT_OF_SIGHT)) {
        set_scene_race_racers_out_of_sight(g, RACER_CRONO, 1);
        set_scene_race_racers_out_of_sight(g, RACER_JOHNNY, 1);
    }
}

/* ---- A frame of the race ----------------------------------------------------------------------- */

typedef enum { RIDE_ON, RIDE_CRONO_WON, RIDE_JOHNNY_WON } RideEnd;

// $7E:485D
/* The race's frame while it runs: the finish, the plane's seam, who is out of
 * sight, and the bikes. */
static RideEnd ride(GameState *g, BikeSounds *sounds)
{
    set_scene_race_frames(g, (uint16_t)(scene_race_frames(g) + 1));
    if (finished(g, RACER_CRONO)) {
        crono_finishes(g);
        return RIDE_CRONO_WON;
    }
    if (finished(g, RACER_JOHNNY)) {
        johnny_wins(g);
        return RIDE_JOHNNY_WON;
    }
    keep_plane_seam(g);
    see_who_is_ahead(g);
    *sounds = ride_bikes(g);
    return RIDE_ON;
}

/* A lane, a step of $80 toward `target`, kept to a multiple of $80. */
enum { LANE_STEP = 0x80, LANE_GRID = 0xFF80 };

static void pull_over(GameState *g, Racer r, uint16_t target)
{
    uint16_t at = lane(g, r);
    if (at == target)
        return;
    bool above = at >= target;
    uint16_t moved = (int16_t)(at - target) > 0 ? (uint16_t)(at - LANE_STEP - !above) : (uint16_t)(at + LANE_STEP + above);
    set_scene_race_racers_lane(g, r, moved & LANE_GRID);
}

/* After the finish, A fades the scene out and ends it; and past the seam
 * again, the plane gets the race's copy of its column back. */
static bool leaving_now(const GameState *g)
{
    return scene_race_leaving(g) == 0 && (scene_race_buttons(g) & IN_BUTTON_A);
}

static bool seam_passed_again(const GameState *g)
{
    uint32_t place = scene_race_place(g);
    return scene_race_cartridge_column(g) != 0 && (place >> 16) == COPY_AGAIN_FROM >> 16 &&
           (uint16_t)place >= (uint16_t)COPY_AGAIN_FROM;
}

static void maybe_leave_early(GameState *g)
{
    if (leaving_now(g)) {
        set_scene_race_leaving(g, 1);
        scene_add_task(g, SCENE_FRAME_TASKS, TASK_FADE_OUT_AND_END);
    }
    if (seam_passed_again(g)) {
        set_scene_race_cartridge_column(g, 0);
        scene_add_task(g, SCENE_VBLANK_TASKS, TASK_COLUMN_FROM_COPY);
    }
    set_scene_race_buttons(g, 0);
}

enum { CRONO_FINISH_LANE = 0x4000 };

// $7E:4677
/* The race's frame after Crono's finish. Coasting, he pulls over to the
 * middle lane (in the demo, both bikes to their lanes), while his lead dies
 * away; once it has and Johnny is out of sight, Crono has pulled away and
 * Johnny is gone from the picture. The bikes go on moving all the while. */
static void coast(GameState *g)
{
    maybe_leave_early(g);
    if (stage(g) != RACE_CRONO_COASTING)
        return;
    set_scene_race_johnny_controls(g, scene_race_demo(g) ? JOHNNY_THROTTLE : 0);
    if (race_in_attract_mode(g)) {
        if (race_song_sending(g))
            return;
        pull_over(g, RACER_CRONO, scene_race_finish_lanes(g, RACER_CRONO));
        pull_over(g, RACER_JOHNNY, scene_race_finish_lanes(g, RACER_JOHNNY));
    } else {
        set_scene_race_racers_out_of_sight(g, RACER_CRONO, 0);
        uint16_t ahead = (uint16_t)((uint16_t)distance(g, RACER_CRONO) - (uint16_t)distance(g, RACER_JOHNNY));
        if (at_least(ahead, OUT_OF_SIGHT))
            set_scene_race_racers_out_of_sight(g, RACER_JOHNNY, 1);
        if (race_song_sending(g))
            return;
        pull_over(g, RACER_CRONO, CRONO_FINISH_LANE);
    }
    uint8_t margin = (uint8_t)scene_race_finish_margin(g);
    if (margin != 0)
        margin = (int8_t)margin > 0 ? (uint8_t)(margin - 1) : (uint8_t)(margin + 1);
    set_scene_race_finish_margin(g, (uint16_t)((scene_race_finish_margin(g) & 0xFF00) | margin));
    if (scene_race_finish_margin(g) != 0)
        return;
    if (scene_race_racers_out_of_sight(g, RACER_JOHNNY) != 0 && (lane(g, RACER_CRONO) & 0xFFFE) == CRONO_FINISH_LANE) {
        set_scene_race_racers_drift_up(g, RACER_CRONO, 0);
        set_scene_race_racers_drift_down(g, RACER_CRONO, 0);
        set_scene_race_stage(g, (uint16_t)(scene_race_stage(g) + 1));
        set_scene_race_johnny_gone(g, (uint16_t)(scene_race_johnny_gone(g) + 1));
    }
}

// $7E:4755
/* After the finish, once the victory song is playing: the camera settles
 * (scene_race_settle_camera takes this task's place) and channel 0 goes on to
 * the script after the race. The frame counter starts its low byte over, which
 * the settling paces itself by. */
enum { AFTER_THE_RACE_SCRIPT = (uint16_t)0x7E41A6 };

static bool await_victory_song(GameState *g, Platform *pf)
{
    if (race_song_sending(g) || au_music_playing(pf, PF_AT(0x7E475C)) == 0)
        return false;
    set_scene_frame_counter(g, scene_frame_counter(g) & 0xFF00);
    set_scene_channels_script(g, 0, AFTER_THE_RACE_SCRIPT);
    return true;
}

/* ---- Leaving -------------------------------------------------------------------------------------- */

enum { ENDING_BEGUN = 2, END_WAIT = 0x20 };

/* After the race (the script's last task), any button begins the scene's end:
 * a sound, and the scene ends 32 frames later. The story byte the menus'
 * scenes clear is cleared too. Returns whether the end has begun. */
static bool begin_ending(GameState *g)
{
    if (scene_joypad(g) == 0 || scene_race_ending(g) != 0)
        return false;
    set_scene_race_ending(g, ENDING_BEGUN);
    return true;
}

static void count_down_to_end(GameState *g)
{
    set_scene_race_end_wait(g, END_WAIT);
    set_menu_scene_flag(g, 0);
}

// $7E:4283
/* The frames before the scene ends, counted down; at 0 the program is over. */
static void leave(GameState *g)
{
    uint16_t left = (uint16_t)(scene_race_end_wait(g) - 1);
    set_scene_race_end_wait(g, left);
    if (left == 0)
        set_scene_program_over(g, (uint16_t)(scene_program_over(g) + 1));
}

// $7E:44CE
/* After Johnny's win, every fourth frame Crono's score drains by the largest of
 * 2000, 200, 20 and 2 it still holds; when it holds none the draining stops.
 * Returns the score after the step, or what is left below 2, less 2. */
static const uint16_t drains[] = { 2000, 200, 20, 2 };

static bool drain_score(GameState *g, uint16_t *left)
{
    uint16_t score = scene_race_score(g);
    for (unsigned i = 0; i < sizeof drains / sizeof drains[0]; i++) {
        if (score >= drains[i]) {
            *left = (uint16_t)(score - drains[i]);
            set_scene_race_score(g, *left);
            return true;
        }
    }
    *left = (uint16_t)(score - 2);
    return false;
}

/* ---- Entries -------------------------------------------------------------------------------------- */

/* The race's tasks run with 16-bit A, on the scene's page, with a data bank
 * that reaches low RAM; each is guarded by a hash of the code it stands for. */
static bool on_scene(Snes *s) { return scene_on_scene_page_low_bank(s); }

static bool ride_code_present(Snes *s) { return m_code_matches(s, 0x7E4788, 0x585, 0x8A843BADA8B3DE72ull); }

bool scene_race_ride_present(Snes *s) { return on_scene(s) && ride_code_present(s); }

/* The coast's compares carry the demo's finish lanes as operands
 * (scene_race_finish_lanes), which the hash leaves out. */
bool scene_race_coast_present(Snes *s)
{
    return on_scene(s) && ride_code_present(s) && m_code_matches(s, 0x7E4677, 0x57, 0x9976F435141BF5AAull) &&
           m_code_matches(s, 0x7E46D0, 0x14, 0x19197C1452B72292ull) &&
           m_code_matches(s, 0x7E46E6, 0x6F, 0x631D422C3D0CD1DCull);
}

bool scene_race_await_victory_song_present(Snes *s)
{
    return on_scene(s) && m_bank_reaches_io(s) && m_code_matches(s, 0x7E4755, 0x1D, 0xCC9805F66890E906ull);
}

bool scene_race_wait_to_leave_present(Snes *s)
{
    return on_scene(s) && m_code_matches(s, 0x7E424D, 0x36, 0xD8C63B5FA73846EBull);
}

bool scene_race_leave_present(Snes *s) { return on_scene(s) && m_code_matches(s, 0x7E4283, 8, 0x773BF38DAF64A867ull); }

bool scene_race_drain_score_present(Snes *s)
{
    return on_scene(s) && m_code_matches(s, 0x7E44CE, 0x30, 0x7C7DBD61E06D9689ull);
}

/* A task that takes itself off the list leaves through the list's own code,
 * which reads the slot it pushed; one that hands its slot to another task, with
 * X that task. Nothing else they leave is read but A's high byte and Y, which
 * they don't touch. */
enum { REMOVE_THIS_TASK = 0x7E39A7, REPLACE_THIS_TASK = 0x7E39B1 };

static void hand_slot_to(Snes *s, uint16_t task)
{
    s->cpu.x = task;
    m_stop_at_own(s, REPLACE_THIS_TASK, DC_A | DC_N | DC_V | DC_Z | DC_C);
}

/* What the frame's motion leaves in A: Johnny's lane, the last it worked out. */
static void leave_last_lane(Snes *s) { s->cpu.a = lane(state_of(s), RACER_JOHNNY); }

/* The bikes' sounds, then their motion. */
static void move_and_sound(Snes *s, BikeSounds sounds)
{
    GameState *g = state_of(s);
    if (sounds.bumped && !race_song_sending(g)) {
        race_sound_bump(s);
        m_set_flag(s, CPU_FLAG_M, false);
    }
    if (sounds.running && !race_song_sending(g))
        race_send_engine_pitch(g, m_platform(s));
    move_bikes(g);
    ORIGINAL_EFFECT(leave_last_lane(s));
    m_rts(s);
}

/* The guard hands the task's list changes to the original where the list has
 * no room or lacks the task taken off, which only states the game never makes
 * reach. */
bool scene_race_ride_lists_fit(Snes *s)
{
    if (!scene_race_ride_present(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool fits;
    if (finished(g, RACER_CRONO))
        fits = scene_task_listed(g, SCENE_FRAME_TASKS, TASK_TURN_CAMERA);
    else if (finished(g, RACER_JOHNNY))
        fits = scene_task_listed(g, SCENE_FRAME_TASKS, TASK_TURN_CAMERA) &&
               scene_task_listed(g, SCENE_VBLANK_TASKS, NO_TASK);
    else
        fits = seam_change(g) == SEAM_KEPT || scene_task_listed(g, SCENE_VBLANK_TASKS, NO_TASK);
    vars_peek(false);
    return fits;
}

/* The race's task while it runs. */
void scene_race_ride(Snes *s)
{
    GameState *g = state_of(s);
    BikeSounds sounds = { false, false };
    switch (ride(g, &sounds)) {
    case RIDE_CRONO_WON:
        hand_slot_to(s, TASK_COAST);
        return;
    case RIDE_JOHNNY_WON:
        s->cpu.a = JOHNNY_WON_SCRIPT;
        m_stop_at(s, REMOVE_THIS_TASK, DC_AL | DC_N | DC_V | DC_Z | DC_C);
        return;
    case RIDE_ON:
        break;
    }
    move_and_sound(s, sounds);
}

bool scene_race_coast_lists_fit(Snes *s)
{
    if (!scene_race_coast_present(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool fits = (!leaving_now(g) || scene_task_listed(g, SCENE_FRAME_TASKS, NO_TASK)) &&
                (!seam_passed_again(g) || scene_task_listed(g, SCENE_VBLANK_TASKS, NO_TASK));
    vars_peek(false);
    return fits;
}

/* The race's task after Crono's finish. */
void scene_race_coast(Snes *s)
{
    GameState *g = state_of(s);
    coast(g);
    move_and_sound(s, ride_bikes(g));
}

/* Waiting for the victory song, in 8-bit A: its low byte is what the wait last
 * read, which nothing reads. */
void scene_race_await_victory_song(Snes *s)
{
    m_set_flag(s, CPU_FLAG_M, true);
    if (await_victory_song(state_of(s), m_platform(s))) {
        hand_slot_to(s, TASK_SETTLE_CAMERA);
        return;
    }
    m_rts(s);
}

/* After the race, in 8-bit A: the sound that begins the end is sent from the
 * program's own call, then the countdown takes the task's place. */
void scene_race_wait_to_leave(Snes *s)
{
    GameState *g = state_of(s);
    m_set_flag(s, CPU_FLAG_M, true);
    if (!begin_ending(g)) {
        m_rts(s);
        return;
    }
    if (!race_song_sending(g))
        race_sound_leaving(s);
    count_down_to_end(g);
    hand_slot_to(s, TASK_LEAVE);
}

void scene_race_leave(Snes *s)
{
    leave(state_of(s));
    m_rts(s);
}

/* Every fourth frame; A is the frame counter's low two bits on the frames
 * between, and the score after the step on the others, whose high byte the
 * list's code reads. */
enum { DRAIN_EVERY = 4 };

void scene_race_drain_score(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t frame = scene_frame_counter(g) & (DRAIN_EVERY - 1);
    if (frame != 0) {
        ORIGINAL_EFFECT(s->cpu.a = frame);
        m_rts(s);
        return;
    }
    uint16_t left;
    bool drained = drain_score(g, &left);
    ORIGINAL_EFFECT(s->cpu.a = left);
    if (!drained) {
        m_stop_at(s, REMOVE_THIS_TASK, DC_AL | DC_N | DC_V | DC_Z | DC_C);
        return;
    }
    m_rts(s);
}
