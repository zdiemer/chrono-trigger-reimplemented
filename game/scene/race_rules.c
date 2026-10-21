/* Scenes: the jet bike race's rules.
 *
 * Crono and Johnny race to a distance of $80000 along the track. Each frame
 * every bike slows down a little and speeds up toward its top speed, and moves
 * on by a 64th of its speed; who leads decides the top speeds, so Johnny,
 * faster behind, keeps the race close. Crono steers across the track with the
 * control pad (which direction steers which way depends on how the camera
 * faces) and has three boosts on B; Johnny steers by a rule of his own,
 * blocking Crono while he leads and swerving away when Crono pulls alongside
 * ahead. Bikes that touch bump: the one ahead is set down just in front of the
 * other and they swap speeds, or they push each other apart across the track
 * and swap drifts. Crono scores two points a frame he leads and loses one a
 * frame he trails, gains 20 when he was ahead as they touch and loses 10 when
 * Johnny was.
 *
 * When Johnny reaches the line first, the race is over: the distance bar's
 * markers go, the camera follows him and Crono's score drains away. When Crono
 * does, his score is kept for the story with what it earns, the victory song
 * starts, and he coasts over to a lane while Johnny drops out of the picture.
 * Then a button ends the scene, and A fades it out first.
 *
 * The rules are plain arithmetic, but for one thing the game does that the
 * gate compares: the original never clears the carry between the sums of a
 * frame's motion, so several of them take a unit from the comparison or sum
 * before -- a drift a unit longer or slower to die, the lead a 32nd shorter,
 * the touching band a unit wider, a bumped bike a unit farther. Each rule has
 * one term for its unit, and "The units the motion hands on" works each one
 * out, from the values the step before it used. */
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

/* The race on to its next stage. The original counts the stage up, and its
 * marks (Johnny gone, the cartridge's column, the program over) the same way,
 * so a mark already set is left set. */
static void next_stage(GameState *g) { set_scene_race_stage(g, (uint16_t)(stage(g) + 1)); }
static uint16_t marked(uint16_t mark) { return (uint16_t)(mark + 1); }

/* Compared as the race compares: by the sign of the difference. */
static bool at_least(uint16_t a, uint16_t b) { return (int16_t)(a - b) >= 0; }

/* What the motion leaves in the arithmetic's working cells. Nothing reads the
 * first before the next routine sets it again. The second is game state: the
 * scene's page is the controller's, and that cell is its held buttons
 * (input_held), which the next frame's controller read takes as the buttons
 * held the frame before. What the race leaves there decides which buttons
 * that read counts as newly pressed and repeating. */
static void leave_working_cells(GameState *g, uint16_t first, uint16_t second)
{
    ORIGINAL_EFFECT(set_scene_call_first(g, scene_program_own(), first));
    set_scene_call_second(g, scene_program_own(), second);
}

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

/* ---- Johnny ----------------------------------------------------------------------------------- */

/* Johnny's controls (scene_race_johnny_controls). */
enum { JOHNNY_THROTTLE = 0x8000, JOHNNY_STEERS_DOWN = 0x0100, JOHNNY_STEERS_UP = 0x0200 };

/* How far apart across the track the bikes are, by their lanes' high bytes,
 * and whether that is alongside. */
enum { ALONGSIDE = 0x20, TRACK_MIDDLE = 0x40 };

static uint16_t lanes_apart(const GameState *g)
{
    uint16_t crono = race_lane_high(g, RACER_CRONO), johnny = race_lane_high(g, RACER_JOHNNY);
    uint16_t apart = (uint16_t)(crono - johnny);
    return (int16_t)apart < 0 ? (uint16_t)(johnny - crono) : apart;
}

static bool alongside(uint16_t apart) { return !at_least(apart, ALONGSIDE); }

/* Johnny opens the throttle while the race runs. Ahead of him, Crono scores;
 * then if Crono comes alongside Johnny swerves away from him, to the lower
 * lanes when Crono is on the higher side of the track. Behind or level, Crono
 * loses a point, and Johnny steers into his lane to block him. */
static void drive_johnny(GameState *g)
{
    uint16_t controls = JOHNNY_THROTTLE;
    if ((int16_t)scene_race_lead(g) > 0) {
        set_scene_race_score(g, (uint16_t)(scene_race_score(g) + 2));
        if (alongside(lanes_apart(g)))
            controls |= at_least(race_lane_high(g, RACER_CRONO), TRACK_MIDDLE) ? JOHNNY_STEERS_DOWN : JOHNNY_STEERS_UP;
    } else {
        set_scene_race_score(g, (uint16_t)(scene_race_score(g) - 1));
        controls |= at_least(lane(g, RACER_CRONO), lane(g, RACER_JOHNNY)) ? JOHNNY_STEERS_UP : JOHNNY_STEERS_DOWN;
    }
    set_scene_race_johnny_controls(g, controls);
}

/* ---- Drifting across the track -------------------------------------------------------------- */

enum {
    MAX_DRIFT = 0x210,
    DRIFT_LOSS = 0x11,             /* every frame */
    CRONO_STEERING = 0x28,         /* how much a frame's steering builds the drift up */
    JOHNNY_STEERING = 0x18,
};

/* A bike's four drifts, in the order the race works them out. */
typedef enum { CRONO_UP, CRONO_DOWN, JOHNNY_UP, JOHNNY_DOWN, DRIFTS } Drift;

static Racer drift_racer(Drift d) { return d < JOHNNY_UP ? RACER_CRONO : RACER_JOHNNY; }
static bool drift_is_up(Drift d) { return d == CRONO_UP || d == JOHNNY_UP; }

static uint16_t drift_of(const GameState *g, Drift d)
{
    Racer r = drift_racer(d);
    return drift_is_up(d) ? scene_race_racers_drift_up(g, r) : scene_race_racers_drift_down(g, r);
}

static void set_drift(GameState *g, Drift d, uint16_t v)
{
    Racer r = drift_racer(d);
    if (drift_is_up(d))
        set_scene_race_racers_drift_up(g, r, v);
    else
        set_scene_race_racers_drift_down(g, r, v);
}

/* Who steers which way this frame, and how hard. */
typedef struct {
    bool steering[DRIFTS];
    uint16_t build_up[DRIFTS];
} Steering;

/* How Crono leans this frame (scene_race_crono_lean): steering both ways at
 * once, he leans down. */
typedef enum { LEAN_UPRIGHT = 0, LEAN_UP = 1, LEAN_DOWN = 2 } CronoLean;

static CronoLean lean_for(bool up, bool down) { return down ? LEAN_DOWN : up ? LEAN_UP : LEAN_UPRIGHT; }

static Steering steering_this_frame(const GameState *g)
{
    PadBits pad = scene_race_buttons(g);
    uint16_t johnny = scene_race_johnny_controls(g);
    Steering s = {
        .steering = { (pad & scene_race_steer_buttons(g, 1)) != 0, (pad & scene_race_steer_buttons(g, 0)) != 0,
                      (johnny & JOHNNY_STEERS_UP) != 0, (johnny & JOHNNY_STEERS_DOWN) != 0 },
        .build_up = { CRONO_STEERING, CRONO_STEERING, JOHNNY_STEERING, JOHNNY_STEERING },
    };
    return s;
}

/* Steering builds a drift up, to at most MAX_DRIFT; a unit more where one is
 * handed on. */
static uint16_t built_up(uint16_t drift, uint16_t build_up, bool unit)
{
    uint16_t v = (uint16_t)(drift + build_up + unit);
    return at_least(v, MAX_DRIFT) ? MAX_DRIFT : v;
}

/* Every frame a drift dies away by DRIFT_LOSS, never below 0; a unit less
 * where one is handed on. */
static uint16_t died_away(uint16_t drift, bool unit)
{
    int16_t v = (int16_t)(drift - DRIFT_LOSS + unit);
    return v < 0 ? 0 : (uint16_t)v;
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

/* How far Crono leads, in 32nds of the distance (a 32nd less without the
 * unit). */
enum { LEAD_SHIFT = 5 };

static uint16_t in_32nds(uint32_t distance) { return (uint16_t)(distance >> LEAD_SHIFT); }

static uint16_t lead(const GameState *g, bool unit)
{
    return (uint16_t)(in_32nds(distance(g, RACER_CRONO)) - in_32nds(distance(g, RACER_JOHNNY)) - 1 + unit);
}

/* ---- Touching ------------------------------------------------------------------------------------ */

/* Where Johnny is from Crono (scene_race_contact). */
enum {
    CRONO_WELL_AHEAD = 0x01, JOHNNY_WELL_AHEAD = 0x02,
    JOHNNY_WELL_ABOVE = 0x10, JOHNNY_WELL_BELOW = 0x20,
    ACROSS_SHIFT = 4,
};

/* How close counts as touching: along the track, Crono's bike from $100 on
 * and Johnny's from $C0 on, $200 and $180 apart; across it, $1000. The
 * original measures along with the distances' low words, so the band repeats
 * every $10000 of the race. */
enum { CRONO_NOSE = 0x100, JOHNNY_NOSE = 0xC0, BEHIND_REACH = 0x200, AHEAD_REACH = 0x180, ACROSS_REACH = 0x1000 };

/* The units the touching takes (contact_units, below). */
typedef struct {
    bool crono_nose, johnny_nose, johnny_back, johnny_ahead, johnny_below;
} ContactUnits;

static uint16_t crono_nose(const GameState *g, const ContactUnits *u)
{
    return (uint16_t)((uint16_t)distance(g, RACER_CRONO) + CRONO_NOSE + u->crono_nose);
}

static uint16_t johnny_nose(const GameState *g, const ContactUnits *u)
{
    return (uint16_t)((uint16_t)distance(g, RACER_JOHNNY) + JOHNNY_NOSE + u->johnny_nose);
}

static uint16_t contact(GameState *g, const ContactUnits *u)
{
    uint16_t c = crono_nose(g, u), j = johnny_nose(g, u);
    uint16_t along = 0;
    if (!at_least((uint16_t)(j + BEHIND_REACH + u->johnny_back), c))
        along = CRONO_WELL_AHEAD;
    else if (at_least((uint16_t)(j - AHEAD_REACH - 1 + u->johnny_ahead), c))
        along = JOHNNY_WELL_AHEAD;

    uint16_t crono_lane = lane(g, RACER_CRONO), johnny_lane = lane(g, RACER_JOHNNY);
    uint16_t across = 0;
    if (at_least((uint16_t)(johnny_lane - ACROSS_REACH), crono_lane))
        across = JOHNNY_WELL_ABOVE >> ACROSS_SHIFT;
    else if (!at_least((uint16_t)(johnny_lane + ACROSS_REACH + u->johnny_below), crono_lane))
        across = JOHNNY_WELL_BELOW >> ACROSS_SHIFT;
    set_scene_race_contact_across(g, across);
    leave_working_cells(g, c, j);
    return (uint16_t)(across << ACROSS_SHIFT | along);
}

/* ---- Bumping ------------------------------------------------------------------------------------ */

/* The units a bump along takes and hands on (bump_along_units, below). */
typedef struct {
    bool overlapping;   /* the bikes overlap: the one ahead goes a unit farther */
    bool drops_back;    /* nothing wrapped: the other drops back a unit */
    bool handed_on;     /* the unit handed to a bump across that follows */
} BumpAlongUnits;

/* A bump along the track: the bike that was ahead, `front`, is set down just
 * in front of the other, `other` (its distance the other's plus both noses),
 * the other drops back a unit, and they swap speeds. The original takes the
 * push's high word -- 0 in any bump the game makes -- off both halves of the
 * other's distance, which the second term keeps. */
static uint32_t push_of(uint32_t front, uint16_t front_nose, uint32_t other, uint16_t other_nose)
{
    return (other + other_nose) - (front - front_nose);
}

static void bump_along(GameState *g, Racer front, uint16_t front_nose, Racer other, uint16_t other_nose,
                       const BumpAlongUnits *u)
{
    uint32_t f = distance(g, front), o = distance(g, other);
    uint32_t push = push_of(f, front_nose, o, other_nose);
    set_scene_race_racers_distance(g, front, f + push + u->overlapping);
    set_scene_race_racers_distance(g, other, o - (push >> 16) * 0x10001u - u->drops_back);
    leave_working_cells(g, (uint16_t)(f - front_nose), (uint16_t)((f - front_nose) >> 16));
    /* The push stays in the arithmetic's cells, where the placing of the bikes'
     * next multiplies take bytes of it in (scene/math.c): it is game state. */
    set_scene_call_bump_push(g, scene_program_own(), push);

    uint16_t s = speed(g, front);
    set_scene_race_racers_speed(g, front, speed(g, other));
    set_scene_race_racers_speed(g, other, s);
}

/* The units a bump across takes (bump_across_units, below). */
typedef struct {
    bool reach;           /* the pushing bike reaches a unit farther */
    bool reach_wrapped;   /* the overlap is a unit less without it */
    bool into_crono;      /* Crono, who moves first, moves a unit farther with it */
    bool into_johnny;     /* and Johnny after him */
} BumpAcrossUnits;

/* How far within ACROSS_REACH of each other the bikes are, `pushing` reaching
 * across toward `pushed`. */
static uint16_t overlap(const GameState *g, Racer pushing, Racer pushed, const BumpAcrossUnits *u)
{
    return (uint16_t)(lane(g, pushing) + ACROSS_REACH + u->reach - lane(g, pushed) - !u->reach_wrapped);
}

/* A bump across the track: the bikes push each other apart by how far they
 * overlap, `pushing` moving away from `pushed`, and swap their drifts. */
static void bump_across(GameState *g, Racer pushing, Racer pushed, const BumpAcrossUnits *u)
{
    uint16_t by = overlap(g, pushing, pushed, u);
    ORIGINAL_EFFECT(set_scene_call_first(g, scene_program_own(), by));
    uint16_t crono = lane(g, RACER_CRONO), johnny = lane(g, RACER_JOHNNY);
    if (pushing == RACER_CRONO) {
        set_scene_race_racers_lane(g, RACER_CRONO, (uint16_t)(crono - by - !u->into_crono));
        set_scene_race_racers_lane(g, RACER_JOHNNY, (uint16_t)(johnny + by + u->into_johnny));
    } else {
        set_scene_race_racers_lane(g, RACER_CRONO, (uint16_t)(crono + by + u->into_crono));
        set_scene_race_racers_lane(g, RACER_JOHNNY, (uint16_t)(johnny - by - !u->into_johnny));
    }
    for (Drift d = CRONO_UP; d < JOHNNY_UP; d++) {
        uint16_t crono_drift = drift_of(g, d);
        set_drift(g, d, drift_of(g, (Drift)(d + JOHNNY_UP)));
        set_drift(g, (Drift)(d + JOHNNY_UP), crono_drift);
    }
}

/* ---- The units the motion hands on ------------------------------------------------------------- */

/* Each function here works out, from the values a step of the motion used,
 * the unit that step leaves for the next sum (see the top of the file). They
 * run in the order the original's sums do, each from the one before. */

/* Out of Johnny's decision, into the drifts: where Crono leads, whether the
 * bikes aren't alongside (alongside, whether Crono is on the track's higher
 * half); where he doesn't, whether Crono's lane is at or above Johnny's. None
 * after the finish, when Johnny decides nothing. */
static bool unit_from_johnny(const GameState *g)
{
    if (stage(g) != RACE_RUNNING)
        return false;
    if ((int16_t)scene_race_lead(g) > 0) {
        uint16_t apart = lanes_apart(g);
        return alongside(apart) ? race_lane_high(g, RACER_CRONO) >= TRACK_MIDDLE : apart >= ALONGSIDE;
    }
    return lane(g, RACER_CRONO) >= lane(g, RACER_JOHNNY);
}

/* A drift's two units: one into its build-up, one into its loss. */
typedef struct {
    bool build_up, loss;
} DriftUnits;

/* The four drifts' units: into a drift's build-up, the unit handed on; into
 * its loss, whether the build-up reached the top (not steering, the unit
 * handed on); and on to the next drift, whether this one was still going. */
static void drift_units(const GameState *g, const Steering *s, DriftUnits out[DRIFTS])
{
    bool unit = unit_from_johnny(g);
    for (Drift d = CRONO_UP; d < DRIFTS; d++) {
        uint16_t v = drift_of(g, d);
        out[d].build_up = unit;
        out[d].loss = s->steering[d] ? (uint16_t)(v + s->build_up[d] + unit) >= MAX_DRIFT : unit;
        uint16_t built = s->steering[d] ? built_up(v, s->build_up[d], unit) : v;
        unit = built >= (uint16_t)(DRIFT_LOSS - out[d].loss);
    }
}

/* Into the lead: whether Crono is in the upper half of his 32nd. */
static bool unit_into_lead(const GameState *g) { return distance(g, RACER_CRONO) & (1u << (LEAD_SHIFT - 1)); }

/* Out of the lead, into the touching: whether Crono's 32nds were at least
 * Johnny's (and one more without the unit in). */
static bool unit_from_lead(const GameState *g, bool unit_in)
{
    return (uint32_t)in_32nds(distance(g, RACER_CRONO)) >= (uint32_t)in_32nds(distance(g, RACER_JOHNNY)) + !unit_in;
}

/* The touching's units: where a bike's nose passes a $10000 boundary of the
 * race, the next sum takes a unit; whether Johnny's reach behind is past
 * Crono's nose; and, across, whether Johnny's lane less the reach is still at
 * or past Crono's as an unsigned number (as it is when Johnny is in the lowest
 * lanes), which widens the band below him by a unit. */
static ContactUnits contact_units(const GameState *g, bool from_lead)
{
    ContactUnits u = { from_lead, false, false, false, false };
    u.johnny_nose = (uint32_t)(uint16_t)distance(g, RACER_CRONO) + CRONO_NOSE + from_lead > 0xFFFF;
    u.johnny_back = (uint32_t)(uint16_t)distance(g, RACER_JOHNNY) + JOHNNY_NOSE + u.johnny_nose > 0xFFFF;
    u.johnny_ahead = (uint16_t)(johnny_nose(g, &u) + BEHIND_REACH + u.johnny_back) >= crono_nose(g, &u);
    u.johnny_below = (uint16_t)(lane(g, RACER_JOHNNY) - ACROSS_REACH) >= lane(g, RACER_CRONO);
    return u;
}

/* A bump along's units: the one into place is whether the other's nose is at
 * or past the front's back (as unsigned distances); the other drops back a unit
 * unless the front's new distance wrapped; and what it hands on is whether the
 * other's distance covered what it lost. */
static BumpAlongUnits bump_along_units(const GameState *g, Racer front, uint16_t front_nose, Racer other,
                                       uint16_t other_nose)
{
    uint32_t f = distance(g, front), o = distance(g, other);
    uint32_t push = push_of(f, front_nose, o, other_nose);
    BumpAlongUnits u;
    u.overlapping = o + other_nose >= f - front_nose;
    uint32_t moved = f + push;
    u.drops_back = !(moved < f || moved + u.overlapping < moved);
    uint32_t taken = (push >> 16) * 0x10001u;
    u.handed_on = o >= taken && o - taken >= (uint32_t)u.drops_back;
    return u;
}

/* A bump across's units: the one handed in, into the reach; whether the reach
 * wrapped past the top of the lanes; into Crono's move, whether the reach got
 * to the bike pushed; and into Johnny's, whether Crono's move covered the
 * overlap (moving down) or ran past the top of the lanes (moving up). */
static BumpAcrossUnits bump_across_units(const GameState *g, Racer pushing, Racer pushed, bool unit_in)
{
    BumpAcrossUnits u = { unit_in, false, false, false };
    uint32_t reach = (uint32_t)lane(g, pushing) + ACROSS_REACH + unit_in;
    u.reach_wrapped = reach > 0xFFFF;
    u.into_crono = (uint16_t)reach >= (uint32_t)lane(g, pushed) + !u.reach_wrapped;
    uint16_t by = overlap(g, pushing, pushed, &u);
    uint16_t crono = lane(g, RACER_CRONO);
    if (pushing == RACER_CRONO)
        u.into_johnny = crono >= (uint32_t)by + !u.into_crono;
    else
        u.into_johnny = (uint32_t)crono + by + u.into_crono > 0xFFFF;
    return u;
}

/* Into the boost: whether Johnny is still speeding up, which the comparison of
 * his speed with his top speed, the last before the boost, says. */
static bool unit_into_boost(const GameState *g) { return scene_race_racers_speeding_up(g, RACER_JOHNNY) != 0; }

/* ---- A bump --------------------------------------------------------------------------------- */

/* Gaining 20 when Crono was the one ahead as they touch, losing 10 when Johnny was. */
enum { SCORE_FOR_BUMPING = 20, SCORE_FOR_BEING_BUMPED = 10 };

static bool bump_crono_forward(GameState *g)
{
    BumpAlongUnits u = bump_along_units(g, RACER_CRONO, CRONO_NOSE, RACER_JOHNNY, JOHNNY_NOSE);
    bump_along(g, RACER_CRONO, CRONO_NOSE, RACER_JOHNNY, JOHNNY_NOSE, &u);
    return u.handed_on;
}

static void bump_johnny_forward(GameState *g)
{
    BumpAlongUnits u = bump_along_units(g, RACER_JOHNNY, JOHNNY_NOSE, RACER_CRONO, CRONO_NOSE);
    bump_along(g, RACER_JOHNNY, JOHNNY_NOSE, RACER_CRONO, CRONO_NOSE, &u);
}

static void push_apart(GameState *g, Racer pushing, Racer pushed, bool unit_in)
{
    BumpAcrossUnits u = bump_across_units(g, pushing, pushed, unit_in);
    bump_across(g, pushing, pushed, &u);
}

/* The bikes have come together: how they bump depends on where they came
 * from, which the last contact apart says. Johnny well above means Crono
 * pushes across toward him, well below Johnny pushes toward Crono; a bump
 * across first hands the bump along that follows nothing (it starts afresh),
 * one along first hands its unit to the bump across. */
static void bump(GameState *g)
{
    bool unit = true;   /* what the choice of bump leaves: it found the contact it bumps by */
    switch (scene_race_last_contact(g)) {
    case CRONO_WELL_AHEAD:
        bump_crono_forward(g);
        set_scene_race_score(g, (uint16_t)(scene_race_score(g) + SCORE_FOR_BUMPING));
        break;
    case JOHNNY_WELL_AHEAD:
        bump_johnny_forward(g);
        set_scene_race_score(g, (uint16_t)(scene_race_score(g) - SCORE_FOR_BEING_BUMPED));
        break;
    case JOHNNY_WELL_ABOVE:
        push_apart(g, RACER_CRONO, RACER_JOHNNY, unit);
        break;
    case JOHNNY_WELL_ABOVE | CRONO_WELL_AHEAD:
        push_apart(g, RACER_CRONO, RACER_JOHNNY, bump_crono_forward(g));
        break;
    case JOHNNY_WELL_ABOVE | JOHNNY_WELL_AHEAD:
        push_apart(g, RACER_CRONO, RACER_JOHNNY, unit);
        bump_johnny_forward(g);
        break;
    case JOHNNY_WELL_BELOW:
        push_apart(g, RACER_JOHNNY, RACER_CRONO, unit);
        break;
    case JOHNNY_WELL_BELOW | CRONO_WELL_AHEAD:
        push_apart(g, RACER_JOHNNY, RACER_CRONO, bump_crono_forward(g));
        break;
    case JOHNNY_WELL_BELOW | JOHNNY_WELL_AHEAD:
        push_apart(g, RACER_JOHNNY, RACER_CRONO, unit);
        bump_johnny_forward(g);
        break;
    default:
        break;
    }
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
 * sends Crono $1700 faster (a unit more where one is handed on), to at most
 * MAX_SPEED. */
static void boost(GameState *g, bool unit)
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
    int16_t faster = (int16_t)(speed(g, RACER_CRONO) + BOOST + unit);
    set_scene_race_racers_speed(g, RACER_CRONO, faster < 0 ? MAX_SPEED : (uint16_t)faster);
}

/* ---- A frame of the bikes ----------------------------------------------------------------------- */

/* What a frame of the bikes asks of the sound driver, which the entry's
 * wrapper sends: the bump's sound, then, while the race runs, the engine's
 * pitch (each unless a song is on its way to the driver at the time). */
typedef struct {
    bool bumped;
    bool running;
} BikeSounds;

/* The drifts for the frame: each built up while steered, then dying away. */
static void drift_all(GameState *g, const Steering *s, const DriftUnits units[DRIFTS])
{
    for (Drift d = CRONO_UP; d < DRIFTS; d++) {
        uint16_t v = drift_of(g, d);
        if (s->steering[d])
            v = built_up(v, s->build_up[d], units[d].build_up);
        set_drift(g, d, died_away(v, units[d].loss));
    }
}

/* The bikes for a frame, before they move: speeds, the boost, the top speeds,
 * the camera's place, Johnny's controls, the drifts, the lead; while the race
 * runs, whether they touch and their bump. */
static BikeSounds ride_bikes(GameState *g)
{
    BikeSounds sounds = { false, stage(g) == RACE_RUNNING };
    change_speed(g, RACER_CRONO, true);
    change_speed(g, RACER_JOHNNY, scene_race_johnny_controls(g) & JOHNNY_THROTTLE);
    keep_speeding_up(g, RACER_CRONO, CRONO_SPEEDING_UP);
    keep_speeding_up(g, RACER_JOHNNY, JOHNNY_SPEEDING_UP);
    if (sounds.running)
        boost(g, unit_into_boost(g));
    set_top_speeds(g);
    place_camera(g);

    set_scene_race_johnny_controls(g, 0);
    if (sounds.running)
        drive_johnny(g);
    Steering s = steering_this_frame(g);
    DriftUnits units[DRIFTS];
    drift_units(g, &s, units);
    set_scene_race_crono_lean(g, lean_for(s.steering[CRONO_UP], s.steering[CRONO_DOWN]));
    drift_all(g, &s, units);

    bool into_lead = unit_into_lead(g);
    bool into_touching = unit_from_lead(g, into_lead);
    set_scene_race_lead(g, lead(g, into_lead));
    leave_working_cells(g, (uint16_t)(distance(g, RACER_CRONO) >> 16) >> LEAD_SHIFT,
                        in_32nds(distance(g, RACER_JOHNNY)));
    if (!sounds.running)
        return sounds;

    ContactUnits cu = contact_units(g, into_touching);
    uint16_t now = contact(g, &cu);
    set_scene_race_contact(g, now);
    if (now != 0) {
        set_scene_race_last_contact(g, now);
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
    set_scene_race_finish_margin(g, (uint8_t)scene_race_lead(g));
    set_scene_race_finish_margin_high(g, 0);
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
    set_scene_race_cartridge_column(g, marked(scene_race_cartridge_column(g)));
    next_stage(g);
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

/* Johnny is out of sight when Crono is ahead of him by $1000 or more, and so,
 * then, is Crono's own sprite. The original reads the low word of the lead by
 * its sign, so in the upper half of each $10000 of lead Johnny shows again. */
enum { OUT_OF_SIGHT = 0x1000 };

static void see_who_is_ahead(GameState *g)
{
    set_scene_race_racers_out_of_sight(g, RACER_JOHNNY, 0);
    set_scene_race_racers_out_of_sight(g, RACER_CRONO, 0);
    uint32_t ahead = distance(g, RACER_CRONO) - distance(g, RACER_JOHNNY);
    leave_working_cells(g, (uint16_t)ahead, (uint16_t)(ahead >> 16));
    if ((int32_t)ahead >= 0 && at_least((uint16_t)ahead, OUT_OF_SIGHT)) {
        set_scene_race_racers_out_of_sight(g, RACER_CRONO, 1);
        set_scene_race_racers_out_of_sight(g, RACER_JOHNNY, 1);
    }
}

/* ---- A frame of the race ----------------------------------------------------------------------- */

typedef enum { RIDE_ON, RIDE_CRONO_WON, RIDE_JOHNNY_WON } RideEnd;

// $7E:485D
/* The race's frame while it runs: the finish, the plane's seam, who is out of
 * sight, and the bikes. Accepted divergence: the boost's unit is whether
 * Johnny is still speeding up, where the original compares his speed with his
 * top speed unsigned; the two differ only where his speed and his top speed
 * are more than $8000 apart, which the game's never are. */
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
    uint16_t moved = at_least(at, target) ? (uint16_t)(at - LANE_STEP) : (uint16_t)(at + LANE_STEP);
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
 * Johnny is gone from the picture. The bikes go on moving all the while.
 * Accepted divergence: a bike pulling over steps $80 either way; the
 * original's step takes a unit from its comparison, which is 0 wherever the
 * lanes are the game's, 0 to $7000. */
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
    uint8_t margin = scene_race_finish_margin(g);
    if (margin != 0)
        set_scene_race_finish_margin(g, (int8_t)margin > 0 ? (uint8_t)(margin - 1) : (uint8_t)(margin + 1));
    if (scene_race_finish_margin(g) != 0 || scene_race_finish_margin_high(g) != 0)
        return;
    if (scene_race_racers_out_of_sight(g, RACER_JOHNNY) != 0 && (lane(g, RACER_CRONO) & 0xFFFE) == CRONO_FINISH_LANE) {
        set_scene_race_racers_drift_up(g, RACER_CRONO, 0);
        set_scene_race_racers_drift_down(g, RACER_CRONO, 0);
        next_stage(g);
        set_scene_race_johnny_gone(g, marked(scene_race_johnny_gone(g)));
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

enum { END_WAIT = 0x20 };

/* After the race (the script's last task), any button begins the scene's end:
 * the curtain starts across the way the scene's setup chose (the program keeps
 * it in its own code, scene_race_ending_sweep: from the right after the race,
 * from the left where the attract sequence set its own curtain up), a sound,
 * and the scene ends 32 frames later. The story byte the menus' scenes clear
 * is cleared too. Returns whether the end has begun. */
static bool begin_ending(GameState *g)
{
    if (scene_joypad(g) == 0 || scene_race_ending(g) != 0)
        return false;
    set_scene_race_ending(g, scene_race_ending_sweep(g));
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
        set_scene_program_over(g, marked(scene_program_over(g)));
}

// $7E:44CE
/* After Johnny's win, every fourth frame Crono's score drains by the largest of
 * 2000, 200, 20 and 2 it still holds; when it holds none the draining stops,
 * and this returns false. */
static const uint16_t drains[] = { 2000, 200, 20, 2 };
enum { LAST_DRAIN = 2 };

static bool drain_score(GameState *g)
{
    uint16_t score = scene_race_score(g);
    for (unsigned i = 0; i < sizeof drains / sizeof drains[0]; i++) {
        if (score >= drains[i]) {
            set_scene_race_score(g, (uint16_t)(score - drains[i]));
            return true;
        }
    }
    return false;
}

/* ---- Entries -------------------------------------------------------------------------------------- */

/* The race's tasks run with 16-bit A, on the scene's page, with a data bank
 * that reaches low RAM; each is guarded by a hash of the code it stands for. */
static bool on_scene(Snes *s) { return scene_on_scene_page_low_bank(s); }

static bool ride_code_present(Snes *s) { return m_code_matches(s, 0x7E4788, 0x585, 0x8A843BADA8B3DE72ull); }

bool scene_race_ride_present(Snes *s) { return on_scene(s) && ride_code_present(s); }

/* The tail sends the pitch straight to the sound driver's ports through the
 * data bank. */
bool scene_race_pitch_and_move_present(Snes *s) { return m_bank_reaches_io(s) && scene_race_ride_present(s); }

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

/* The hash leaves out the curtain's way, which the scenes' setups write. */
bool scene_race_wait_to_leave_present(Snes *s)
{
    return on_scene(s) && m_code_matches(s, 0x7E424D, 0x0C, 0x7A9A012413CE0841ull) &&
           m_code_matches(s, 0x7E425A, 0x29, 0x239A23CA2369EDC7ull);
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

// $7E:4BD2
/* The ride's tail, where the original's own code comes to it in a frame the
 * bikes run: the engine's pitch to the sound driver, then the bikes' motion. */
void scene_race_pitch_and_move(Snes *s)
{
    GameState *g = state_of(s);
    race_send_engine_pitch(g, m_platform(s));
    move_bikes(g);
    ORIGINAL_EFFECT(leave_last_lane(s));
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
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
    uint16_t before = scene_race_score(g);
    if (!drain_score(g)) {
        ORIGINAL_EFFECT(s->cpu.a = (uint16_t)(before - LAST_DRAIN));   /* the last subtraction tried */
        m_stop_at(s, REMOVE_THIS_TASK, DC_AL | DC_N | DC_V | DC_Z | DC_C);
        return;
    }
    ORIGINAL_EFFECT(s->cpu.a = scene_race_score(g));
    m_rts(s);
}

/* ---- The start ------------------------------------------------------------------------------ */

/* The bikes as the race starts. Crono is on the higher lanes and Johnny on
 * the lower; Johnny's top speed is lower than Crono's while he leads and higher
 * while he trails. */
enum {
    CRONO_START_LANE = 0x6000, JOHNNY_START_LANE = 0x2000,
    CRONO_TOP_SPEED = 0x4000, JOHNNY_TOP_SPEED_LEADING = 0x3000, JOHNNY_TOP_SPEED_TRAILING = 0x5000,
    APART = 0x20,   /* the bikes start apart, Crono above */
};

/* The race's own boosts and their timer, which the demo has none of; the
 * setup writes their low bytes. */
enum { STARTING_BOOSTS = 3, BOOST_TIMER_START = 0x80 };

/* The demo's panel is the bar alone, longer: its right end and the markers'
 * at $D8 (the cap after it at $E8), and the score's label and the corners
 * left out of their lists. The setup writes the low bytes of the bar's ends. */
enum { DEMO_BAR_RIGHT = 0xD8, DEMO_BAR_CAP = 0xE8, DEMO_PANEL_SPRITES = 6, DEMO_CORNER_SPRITES = 1 };

static uint16_t with_low_byte(uint16_t word, uint8_t low) { return (uint16_t)((word & 0xFF00) | low); }

static void panel_for_the_demo(GameState *g)
{
    set_scene_race_bar_end_x(g, DEMO_BAR_RIGHT);
    set_scene_race_bar_right(g, with_low_byte(scene_race_bar_right(g), DEMO_BAR_RIGHT));
    set_scene_race_markers_right(g, with_low_byte(scene_race_markers_right(g), DEMO_BAR_RIGHT));
    set_scene_race_bar_cap_x(g, DEMO_BAR_CAP);
    set_scene_race_panel_sprites(g, DEMO_PANEL_SPRITES);
    set_scene_race_corner_sprites(g, DEMO_CORNER_SPRITES);
}

// $7E:45D6
/* The race's start: the panel for the demo or the boosts for the race, the
 * bikes' top speeds and lanes, the plane's column as the cartridge's, the
 * boost button taken as held, the pad's directions for the camera facing down
 * the track, and the distances' divisors forgotten. */
void race_start(GameState *g)
{
    if (scene_race_demo(g) & 0xFF) {
        panel_for_the_demo(g);
    } else {
        set_scene_race_boost_timer(g, with_low_byte(scene_race_boost_timer(g), BOOST_TIMER_START));
        set_scene_race_boosts(g, with_low_byte(scene_race_boosts(g), STARTING_BOOSTS));
    }
    set_scene_race_racers_top_speed_leading(g, RACER_CRONO, CRONO_TOP_SPEED);
    set_scene_race_racers_top_speed_trailing(g, RACER_CRONO, CRONO_TOP_SPEED);
    set_scene_race_racers_top_speed_leading(g, RACER_JOHNNY, JOHNNY_TOP_SPEED_LEADING);
    set_scene_race_racers_top_speed_trailing(g, RACER_JOHNNY, JOHNNY_TOP_SPEED_TRAILING);
    set_scene_race_crono_lean(g, 0);
    set_scene_race_racers_lane(g, RACER_CRONO, CRONO_START_LANE);
    set_scene_race_racers_lane(g, RACER_JOHNNY, JOHNNY_START_LANE);
    set_scene_race_last_contact(g, APART);
    set_scene_race_cartridge_column(g, 1);
    set_scene_race_boost_held(g, 1);
    set_scene_race_johnny_gone(g, 0);
    set_scene_race_steer_buttons(g, 0, scene_race_steering_by_view(g, 0));
    set_scene_race_steer_buttons(g, 1, scene_race_steering_by_view(g, 8));
    race_forget_divisors(g);
}
