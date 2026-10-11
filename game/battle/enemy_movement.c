/* Battle: how the enemies move about the battlefield on their own.
 *
 * Some of an enemy's animations move it while they play: the animation names
 * a movement ($CC:FBAB), which battle/animation.c runs every few frames (the
 * enemy's effect period). A movement takes the enemy a step at a time. It
 * picks a heading, turns the enemy to face it, and looks where a step of
 * eight pixels would take it: if the battlefield's grid stops it there, or
 * another combatant's box is in the way, the step isn't taken; otherwise the
 * enemy drifts there over the next frames (battle/effect_paths.c steps the
 * drift from where it started).
 *
 * The movements:
 *
 *   - approach its target until within reach, turning aside to the next
 *     straight heading round when its way is blocked, and keeping to that
 *     heading until a step is free;
 *   - charge straight at its target until its way is blocked;
 *   - line up with its target, across or down, until in the same 32-pixel
 *     band;
 *   - keep its distance: make for the spot a set distance from its target on
 *     its own side, backing away while nearer, closing in while farther;
 *   - circle its target: make for that spot, then go round the target,
 *     turning back the other way when its way is blocked;
 *   - weave round a loop below where it started, wider than it is high;
 *   - march one way until its way is blocked;
 *   - head for the middle of the battlefield.
 *
 * An enemy that comes where it was going, or whose way is blocked for good,
 * stops to act: it may take its turn whatever animation it shows
 * (battle_enemy_acts_anyway), and keeps the animation it moved with and where
 * its target stood. The movement of the animation it stops with waits for the
 * target to leave that place, and then sets it moving again.
 *
 * The animation a movement is named by picks its variant: how near an
 * approach comes, whether the enemy goes over the grid's marked cells and
 * through the other combatants, the distance it keeps, the size of its
 * circle or its loop, a march's direction. */
#include "battle/animation.h"

#include "battle/animation_data.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/positions.h"
#include "original_effects.h"
#include "vars.h"

/* A step is eight pixels; headings are in 256ths of a turn, 0 to the right
 * and a quarter turn down. */
enum { STEP_LENGTH = 8, QUARTER_TURN = 0x40, HALF_TURN = 0x80 };
enum { HEADING_RIGHT = 0x00, HEADING_DOWN = 0x40, HEADING_LEFT = 0x80, HEADING_UP = 0xC0 };

/* The ways a combatant's frames face (battle_object_facing). */
enum { FACING_UP = 0, FACING_DOWN = 1, FACING_LEFT = 2, FACING_RIGHT = 3 };

static unsigned slot_of(unsigned enemy) { return BATTLE_FIRST_ENEMY + enemy; }

static BattlePoint standing(const GameState *g, unsigned slot) { return (BattlePoint){ combatant_x(g, slot), combatant_y(g, slot) }; }

static uint8_t target_of(const GameState *g, unsigned enemy) { return battle_enemy_target_seen(g, enemy); }

/* ---- Steps ------------------------------------------------------------------------------- */

/* How far a move of `length` along a heading goes down and across. */
typedef struct {
    int8_t down, across;
} Step;

static Step step_along(GameState *g, uint8_t heading, uint8_t length)
{
    int8_t down = battle_along(g, heading, length);
    return (Step){ down, battle_along(g, (uint8_t)(heading + QUARTER_TURN), length) };
}

static BattlePoint moved_by(BattlePoint p, Step s)
{
    return (BattlePoint){ (uint8_t)(p.x + s.across), (uint8_t)(p.y + s.down) };
}

/* The step's drift starts from where the enemy stands now. */
static void start_drift_here(GameState *g, unsigned enemy, BattlePoint at)
{
    set_battle_enemy_drift_from_x(g, enemy, at.x);
    set_battle_enemy_drift_from_y(g, enemy, at.y);
}

static void drift(GameState *g, unsigned enemy, bool on) { set_battle_enemy_drifting(g, enemy, on); }

/* The heading the enemy goes on, which is also the direction last found. */
static void keep_heading(GameState *g, unsigned enemy, uint8_t heading)
{
    set_battle_enemy_heading(g, enemy, heading);
    battle_note_direction(g, heading);
}

static void face(GameState *g, unsigned enemy, uint8_t facing) { set_battle_object_facing(g, slot_of(enemy), facing); }

static void face_heading(GameState *g, unsigned enemy, uint8_t heading)
{
    face(g, enemy, battle_facing_of_direction(g, heading));
}

/* What the original leaves in its work cells (battle_work, from $80) after a
 * step's look: the step's reach down and across and the radius it worked
 * them out with; and once a movement is done, the combatant whose box it
 * looked at, a word. */
enum { LEFT_LOOKED_AT = 0x00, LEFT_DOWN = 0x0C, LEFT_ACROSS = 0x0E };
static void leave_step(GameState *g, Step s)
{
    set_battle_work(g, LEFT_DOWN, (uint8_t)s.down);
    set_battle_work(g, LEFT_ACROSS, (uint8_t)s.across);
    set_battle_multiply_factors(g, 1, STEP_LENGTH);
}

static void leave_looked_at(GameState *g, unsigned slot)
{
    set_battle_work(g, LEFT_LOOKED_AT, (uint8_t)slot);
    set_battle_work(g, LEFT_LOOKED_AT + 1, 0);
}

/* Whether the enemy's way is blocked where the step would take it: by the
 * grid, or by another combatant's box unless it passes through them (and
 * then over the grid's marked cells too). Its box is left there. */
static bool way_blocked(GameState *g, unsigned enemy, Step step, bool passes_through)
{
    unsigned slot = slot_of(enemy);
    BattlePoint there = moved_by(standing(g, slot), step);
    set_battle_bounds_y(g, slot, there.y);
    set_battle_bounds_x(g, slot, there.x);
    battle_update_bounds(g, slot);
    set_battle_search_through_marked(g, passes_through);
    if (battle_grid_stops(g, slot))
        return true;
    return !passes_through && battle_box_overlaps(g, slot) != OVERLAPS_NOBODY;
}

/* ---- Stopping and going on --------------------------------------------------------------- */

/* The enemy stops to act, and keeps the animation it moved with and where its
 * target stands. (The original counts the mark to act up; any count is one.) */
static void mark_to_act(GameState *g, unsigned enemy)
{
    set_battle_enemy_acts_anyway(g, enemy, (uint8_t)(battle_enemy_acts_anyway(g, enemy) + 1));
}

static void stop_to_act(GameState *g, unsigned enemy)
{
    mark_to_act(g, enemy);
    set_battle_enemy_animation_work(g, enemy, battle_enemy_animation(g, enemy));
    BattlePoint target = standing(g, target_of(g, enemy));
    set_battle_enemy_target_seen_x(g, enemy, target.x);
    set_battle_enemy_target_seen_y(g, enemy, target.y);
    drift(g, enemy, false);
}

static bool stopped_to_act(const GameState *g, unsigned enemy) { return battle_enemy_acts_anyway(g, enemy) != 0; }

/* A movement other than the weave ends the weave: the next starts afresh. */
static void stop_weaving(GameState *g, unsigned enemy) { set_battle_enemy_weaving(g, enemy, false); }

static bool turned_aside(const GameState *g, unsigned enemy) { return battle_enemy_turned_aside(g, enemy) != 0; }

/* The next of the four straight headings round from the heading. */
static uint8_t next_straight_heading(uint8_t heading) { return (uint8_t)((heading + QUARTER_TURN) & 0xC0); }

/* Take the step, or, blocked, turn aside to the next straight heading for the
 * next. A walk turned aside keeps where its last drift started (the original
 * doesn't move it while turned aside). */
static void walk_on(GameState *g, unsigned enemy, Step step, bool passes_through)
{
    if (!way_blocked(g, enemy, step, passes_through)) {
        set_battle_enemy_turned_aside(g, enemy, 0);
        drift(g, enemy, true);
        return;
    }
    keep_heading(g, enemy, next_straight_heading(battle_enemy_heading(g, enemy)));
    set_battle_enemy_turned_aside(g, enemy, (uint8_t)(battle_enemy_turned_aside(g, enemy) + 1));
    drift(g, enemy, false);
}

/* The heading and step a walk goes on with, the enemy turned to face it. */
static Step head_for(GameState *g, unsigned enemy, uint8_t heading)
{
    keep_heading(g, enemy, heading);
    Step step = step_along(g, heading, STEP_LENGTH);
    face_heading(g, enemy, heading);
    ORIGINAL_EFFECT(leave_step(g, step));
    return step;
}

/* ---- Waiting for the target to move ($C1:3772) ------------------------------------------- */

// $C1:3772
/* Where an enemy that saw no target ($FF) finds it standing: the original
 * reads the slot's place past the combatants', in the bytes after the
 * battlefield's objects' places, which nothing keeps for it. A target of
 * nobody is a place no one stands on in a build that keeps the game's own
 * storage (docs/TODO.md). */
static BattlePoint where_the_original_finds_nobody(const GameState *g, uint8_t nobody)
{
    unsigned object = battle_object_of(nobody);
    return (BattlePoint){ battle_object_x_unchecked(g, object), battle_object_y_unchecked(g, object) };
}

static BattlePoint target_standing(const GameState *g, unsigned enemy)
{
    uint8_t target = target_of(g, enemy);
    return battle_names_combatant(target) ? standing(g, target) : where_the_original_finds_nobody(g, target);
}

static void wait_for_target_to_move(GameState *g, unsigned enemy)
{
    if (!stopped_to_act(g, enemy))
        return;
    stop_weaving(g, enemy);
    BattlePoint target = target_standing(g, enemy);
    if (target.x == battle_enemy_target_seen_x(g, enemy) && target.y == battle_enemy_target_seen_y(g, enemy))
        return;
    set_battle_enemy_animation(g, enemy, battle_enemy_animation_work(g, enemy));
    set_battle_enemy_acts_anyway(g, enemy, 0);
}

/* ---- Approaching ($C1:379A) and charging ($C1:38BF) -------------------------------------- */

/* How near an approach comes: within the target search's reach, twice as far,
 * or touching (going over the grid's marked cells and through the others);
 * with any other animation the enemy stops at once. */
enum { APPROACH_TO_REACH = 0x01, APPROACH_TO_FAR_REACH = 0x02, APPROACH_TO_TOUCH = 0x03 };

static uint16_t approach_reach(uint8_t animation)
{
    switch (animation) {
    case APPROACH_TO_REACH: return REACH_SEARCH;
    case APPROACH_TO_FAR_REACH: return REACH_FAR;
    case APPROACH_TO_TOUCH: return REACH_TOUCHING;
    default: return 0;
    }
}

/* The boxes the reach is measured between: each where its combatant stands. */
static void box_where_standing(GameState *g, unsigned slot)
{
    BattlePoint at = standing(g, slot);
    set_battle_bounds_x(g, slot, at.x);
    set_battle_bounds_y(g, slot, at.y);
}

/* A walk not turned aside heads afresh from where the enemy stands. */
static uint8_t fresh_heading(GameState *g, unsigned enemy, BattlePoint from, BattlePoint to)
{
    start_drift_here(g, enemy, standing(g, slot_of(enemy)));
    return battle_direction(g, from, to);
}

// $C1:379A
static void approach(GameState *g, unsigned enemy, uint8_t animation)
{
    unsigned slot = slot_of(enemy);
    uint8_t target = target_of(g, enemy);
    stop_weaving(g, enemy);
    uint8_t heading = turned_aside(g, enemy) ? battle_enemy_heading(g, enemy)
                                             : fresh_heading(g, enemy, standing(g, slot), standing(g, target));
    Step step = head_for(g, enemy, heading);
    box_where_standing(g, target);
    box_where_standing(g, slot);
    uint16_t reach = approach_reach(animation);
    if (reach == 0 || battle_within_reach(g, (uint8_t)slot, target, reach)) {
        stop_to_act(g, enemy);
        return;
    }
    walk_on(g, enemy, step, animation == APPROACH_TO_TOUCH);
}

// $C1:38BF
static void charge(GameState *g, unsigned enemy)
{
    BattlePoint at = standing(g, slot_of(enemy));
    stop_weaving(g, enemy);
    start_drift_here(g, enemy, at);
    Step step = head_for(g, enemy, battle_direction(g, at, standing(g, target_of(g, enemy))));
    drift(g, enemy, true);
    bool blocked = way_blocked(g, enemy, step, false);
    if (blocked)
        stop_to_act(g, enemy);
    /* with its way free the original adds the enemies' first slot to the slot
     * the boxes' test handed back, not to the enemy, and leaves that */
    ORIGINAL_EFFECT(leave_looked_at(g, blocked ? slot_of(enemy) : slot_of(enemy) + BATTLE_FIRST_ENEMY));
}

/* ---- Lining up ($C1:3959) ---------------------------------------------------------------- */

/* The animation that lines up down rather than across, and the bands it lines
 * up in. */
enum { LINE_UP_DOWN = 0x08, BAND_SHIFT = 5 };

// $C1:3959
static void line_up(GameState *g, unsigned enemy, uint8_t animation)
{
    BattlePoint at = standing(g, slot_of(enemy)), target = standing(g, target_of(g, enemy));
    stop_weaving(g, enemy);
    start_drift_here(g, enemy, at);
    bool down = animation == LINE_UP_DOWN;
    uint8_t heading = down ? (target.y < at.y ? HEADING_UP : HEADING_DOWN) : (target.x < at.x ? HEADING_LEFT : HEADING_RIGHT);
    Step step = head_for(g, enemy, heading);
    drift(g, enemy, true);
    bool level = down ? (target.y >> BAND_SHIFT) == (at.y >> BAND_SHIFT) : (target.x >> BAND_SHIFT) == (at.x >> BAND_SHIFT);
    if (way_blocked(g, enemy, step, false) || level)
        stop_to_act(g, enemy);
}

/* ---- The spot at a distance from the target ---------------------------------------------- */

/* Which side of the spot the enemy stands on, across and down. The test down
 * counts a pixel more below the spot when the enemy is left of it, as the
 * original carries the borrow of its test across into it. */
typedef struct {
    bool left, above;
} SpotSide;

static SpotSide side_of_spot(BattlePoint at, BattlePoint spot)
{
    bool left = at.x < spot.x;
    return (SpotSide){ left, at.y < spot.y + (left ? 1 : 0) };
}

/* Whether the enemy stands short of the spot, between it and its target, as
 * the heading's quarter has the spot from the target. */
static bool short_of_spot(SpotSide side, uint8_t heading)
{
    switch (heading >> 6) {
    case 0: return side.left && side.above;   /* the spot right of the target and below it */
    case 1: return !side.left && side.above;  /* left and below */
    case 2: return !side.left && !side.above; /* left and above */
    default: return side.left && !side.above; /* right and above */
    }
}

/* What the original leaves of the test: the spot's down, the test down, and
 * the heading's quarter. */
enum { LEFT_SPOT_Y = 0x02, LEFT_ABOVE = 0x03, LEFT_QUARTER = 0x04 };
static void leave_spot_test(GameState *g, BattlePoint spot, bool above, uint8_t heading)
{
    set_battle_work(g, LEFT_SPOT_Y, spot.y);
    set_battle_work(g, LEFT_ABOVE, above ? 0xFF : 0x00);
    set_battle_work(g, LEFT_QUARTER, (uint8_t)(heading >> 6));
}

/* The spot `distance` from the target on the enemy's side, kept; and the
 * heading to it from where the enemy stands: away from the target while the
 * enemy is short of it, back toward it beyond. */
static uint8_t heading_for_spot(GameState *g, unsigned enemy, uint8_t distance)
{
    BattlePoint at = standing(g, slot_of(enemy)), target = standing(g, target_of(g, enemy));
    start_drift_here(g, enemy, at);
    uint8_t away = battle_direction(g, target, at);
    BattlePoint spot = moved_by(target, step_along(g, away, distance));
    set_battle_enemy_spot_x(g, enemy, spot.x);
    set_battle_enemy_spot_y(g, enemy, spot.y);
    SpotSide side = side_of_spot(at, spot);
    ORIGINAL_EFFECT(leave_spot_test(g, spot, side.above, away));
    return short_of_spot(side, away) ? away : (uint8_t)(away + HALF_TURN);
}

static BattlePoint spot_of(const GameState *g, unsigned enemy)
{
    return (BattlePoint){ battle_enemy_spot_x(g, enemy), battle_enemy_spot_y(g, enemy) };
}

/* The original measures to the spot as to a box past the combatants', whose
 * place it writes into the table's next row: past the table, into the cells
 * after it. */
enum { SPOT_BOX_ROW = 11 };
static void leave_spot_box(GameState *g, BattlePoint spot)
{
    set_battle_bounds_y_unchecked(g, SPOT_BOX_ROW, spot.y);
    set_battle_bounds_x_unchecked(g, SPOT_BOX_ROW, spot.x);
}

/* Whether the enemy's box, where its last look left it, is within `reach` of
 * its spot. */
static bool at_spot(GameState *g, unsigned enemy, uint16_t reach)
{
    BattlePoint spot = spot_of(g, enemy);
    ORIGINAL_EFFECT(leave_spot_box(g, spot));
    return battle_box_within_reach_of(g, (uint8_t)slot_of(enemy), spot, reach);
}

/* ---- Keeping a distance ($C1:3A3D) ------------------------------------------------------- */

/* The distance kept, 64 pixels, or 128 for the animation that never stops
 * there. */
enum { KEEP_FAR_OFF = 0x10, KEPT_DISTANCE = 0x40, KEPT_FAR_DISTANCE = 0x80 };

// $C1:3A3D
static void keep_distance(GameState *g, unsigned enemy, uint8_t animation)
{
    bool far_off = animation == KEEP_FAR_OFF;
    stop_weaving(g, enemy);
    uint8_t heading = turned_aside(g, enemy) ? battle_enemy_heading(g, enemy)
                                             : heading_for_spot(g, enemy, far_off ? KEPT_FAR_DISTANCE : KEPT_DISTANCE);
    Step step = head_for(g, enemy, heading);
    if (!far_off && at_spot(g, enemy, REACH_TOUCHING)) {
        stop_to_act(g, enemy);
        return;
    }
    walk_on(g, enemy, step, false);
}

/* ---- Circling ($C1:3BC2) ----------------------------------------------------------------- */

/* The animations: one that circles only once at its spot, standing still
 * until then; one at twice the distance; one that goes over the grid's
 * marked cells and through the others. */
enum { CIRCLE_FROM_THE_SPOT = 0x0D, CIRCLE_FAR_OFF = 0x0E, CIRCLE_THROUGH = 0x0F };
enum { CIRCLE_RADIUS = 0x20, CIRCLE_FAR_RADIUS = 0x40, CIRCLE_STEP = 0x10 };

/* Round the target by a sixteenth of a turn from where the enemy is, at the
 * circle's radius, the way it goes round. A heading straight at the target is
 * nudged round a little, the more for the nearer circle. */
static uint8_t heading_round_target(GameState *g, unsigned enemy)
{
    BattlePoint at = standing(g, slot_of(enemy)), target = standing(g, target_of(g, enemy));
    start_drift_here(g, enemy, at);
    uint8_t from_target = battle_direction(g, target, at);
    uint8_t round = (uint8_t)(battle_enemy_circling_back(g, enemy) ? from_target - CIRCLE_STEP : from_target + CIRCLE_STEP);
    uint8_t radius = battle_circle_radius(g);
    BattlePoint next = moved_by(target, step_along(g, round, radius));
    uint8_t heading = battle_direction(g, at, next);
    if (heading == (uint8_t)(from_target + HALF_TURN))
        heading = (uint8_t)(heading + (radius == CIRCLE_RADIUS ? 2 : 1));
    return heading;
}

/* The enemy goes round its target the other way from now on. (The original
 * flips the cell's low bit; the game only ever keeps 0 or 1 there.) */
static void circle_the_other_way(GameState *g, unsigned enemy)
{
    set_battle_enemy_circling_back(g, enemy, (uint8_t)(battle_enemy_circling_back(g, enemy) ^ 1));
}

/* Go round the target, or, blocked, turn back the other way round. */
static void circle_round(GameState *g, unsigned enemy, bool passes_through)
{
    set_battle_enemy_turned_aside(g, enemy, 0);
    Step step = head_for(g, enemy, heading_round_target(g, enemy));
    if (way_blocked(g, enemy, step, passes_through))
        circle_the_other_way(g, enemy);
    else
        drift(g, enemy, true);
}

// $C1:3BC2
static void circle(GameState *g, unsigned enemy, uint8_t animation)
{
    bool passes_through = animation == CIRCLE_THROUGH;
    stop_weaving(g, enemy);
    uint8_t heading;
    if (turned_aside(g, enemy)) {
        heading = battle_enemy_heading(g, enemy);
    } else {
        set_battle_circle_radius(g, animation == CIRCLE_FAR_OFF ? CIRCLE_FAR_RADIUS : CIRCLE_RADIUS);
        heading = heading_for_spot(g, enemy, battle_circle_radius(g));
    }
    Step step = head_for(g, enemy, heading);
    if (at_spot(g, enemy, REACH_NEAR)) {
        circle_round(g, enemy, passes_through);
        return;
    }
    if (animation == CIRCLE_FROM_THE_SPOT) {
        drift(g, enemy, false);
        return;
    }
    walk_on(g, enemy, step, passes_through);
}

/* ---- Weaving ($C1:3E33) ------------------------------------------------------------------ */

/* The loop's home is 16 pixels below where the weave starts, and the weave
 * starts a quarter turn round it, going a sixteenth of a turn at a time; the
 * loop is 32 pixels high and 64 wide, half that high for the small one; a
 * blocked weave jumps back round the loop and turns the other way. */
enum { WEAVE_SMALL = 0x0A, WEAVE_THROUGH = 0x0C };
enum { WEAVE_HOME_BELOW = 16, WEAVE_START = 0xC0, WEAVE_STEP = 0x10, WEAVE_HEIGHT = 0x20, WEAVE_SMALL_HEIGHT = 0x10,
       WEAVE_WIDER_BY = 0x20, WEAVE_JUMP = 0x58, WEAVE_SMALL_JUMP = 0x30 };

static void start_weave(GameState *g, unsigned enemy, BattlePoint at)
{
    set_battle_enemy_weaving(g, enemy, true);
    set_battle_enemy_weave_home_y(g, enemy, (uint8_t)(at.y + WEAVE_HOME_BELOW));
    set_battle_enemy_weave_home_x(g, enemy, at.x);
    set_battle_enemy_weave_angle(g, enemy, WEAVE_START);
    set_battle_enemy_weave_back(g, enemy, false);
}

/* The next point round the loop. */
static BattlePoint next_on_loop(GameState *g, unsigned enemy, bool small)
{
    bool back = battle_enemy_weave_back(g, enemy);
    uint8_t angle = (uint8_t)(battle_enemy_weave_angle(g, enemy) + (back ? -WEAVE_STEP : WEAVE_STEP));
    set_battle_enemy_weave_angle(g, enemy, angle);
    uint8_t height = small ? WEAVE_SMALL_HEIGHT : WEAVE_HEIGHT;
    ORIGINAL_EFFECT(set_battle_weave_height(g, height));
    int8_t down = battle_along(g, angle, height);
    int8_t across = battle_along(g, (uint8_t)(angle + QUARTER_TURN), (uint8_t)(height + WEAVE_WIDER_BY));
    BattlePoint home = { battle_enemy_weave_home_x(g, enemy), battle_enemy_weave_home_y(g, enemy) };
    return moved_by(home, (Step){ down, across });
}

/* The enemy weaves round its loop the other way from now on. (The original
 * flips the cell's low bit; the game only ever keeps 0 or 1 there.) */
static void weave_the_other_way(GameState *g, unsigned enemy)
{
    set_battle_enemy_weave_back(g, enemy, (uint8_t)(battle_enemy_weave_back(g, enemy) ^ 1));
}

/* What the original leaves of a blocked weave: how far it jumped back. */
enum { LEFT_JUMP = 0x02 };

// $C1:3E33
static void weave(GameState *g, unsigned enemy, uint8_t animation)
{
    bool small = animation == WEAVE_SMALL;
    BattlePoint at = standing(g, slot_of(enemy));
    start_drift_here(g, enemy, at);
    if (!battle_enemy_weaving(g, enemy))
        start_weave(g, enemy, at);
    BattlePoint next = next_on_loop(g, enemy, small);
    Step step = head_for(g, enemy, battle_direction(g, at, next));
    if (!way_blocked(g, enemy, step, animation == WEAVE_THROUGH)) {
        drift(g, enemy, true);
        return;
    }
    uint8_t jump = small ? WEAVE_SMALL_JUMP : WEAVE_JUMP;
    ORIGINAL_EFFECT(set_battle_work(g, LEFT_JUMP, jump));
    bool back = battle_enemy_weave_back(g, enemy);
    uint8_t angle = battle_enemy_weave_angle(g, enemy);
    set_battle_enemy_weave_angle(g, enemy, (uint8_t)(back ? angle + jump : angle - jump));
    weave_the_other_way(g, enemy);
}

/* ---- Marching ($C1:3F5A) and making for the middle ($C1:4021) ---------------------------- */

/* A march's animation names its direction: the four straight ones, then the
 * diagonals, which face a way of their own; every other animation of the
 * march goes up and to the right. */
enum { MARCH_UP = 0x11, MARCH_DOWN, MARCH_RIGHT, MARCH_LEFT, MARCH_DOWN_RIGHT, MARCH_DOWN_LEFT, MARCH_UP_LEFT };
enum { HEADING_DOWN_RIGHT = 0x20, HEADING_DOWN_LEFT = 0x60, HEADING_UP_LEFT = 0xA0, HEADING_UP_RIGHT = 0xE0 };
enum { FACING_OF_THE_HEADING = 0xFF };

typedef struct {
    uint8_t heading, facing;
} March;

static March march_of(uint8_t animation)
{
    switch (animation) {
    case MARCH_UP: return (March){ HEADING_UP, FACING_OF_THE_HEADING };
    case MARCH_DOWN: return (March){ HEADING_DOWN, FACING_OF_THE_HEADING };
    case MARCH_RIGHT: return (March){ HEADING_RIGHT, FACING_OF_THE_HEADING };
    case MARCH_LEFT: return (March){ HEADING_LEFT, FACING_OF_THE_HEADING };
    case MARCH_DOWN_RIGHT: return (March){ HEADING_DOWN_RIGHT, FACING_RIGHT };
    case MARCH_DOWN_LEFT: return (March){ HEADING_DOWN_LEFT, FACING_DOWN };
    case MARCH_UP_LEFT: return (March){ HEADING_UP_LEFT, FACING_LEFT };
    default: return (March){ HEADING_UP_RIGHT, FACING_UP };
    }
}

// $C1:3F5A
static void march(GameState *g, unsigned enemy, uint8_t animation)
{
    March m = march_of(animation);
    start_drift_here(g, enemy, standing(g, slot_of(enemy)));
    Step step = head_for(g, enemy, m.heading);
    if (m.facing != FACING_OF_THE_HEADING)
        face(g, enemy, m.facing);
    drift(g, enemy, true);
    if (way_blocked(g, enemy, step, false)) {
        drift(g, enemy, false);
        mark_to_act(g, enemy);
    }
}

/* The middle of the battlefield. */
static const BattlePoint MIDDLE = { 0x80, 0x70 };

// $C1:4021
static void make_for_the_middle(GameState *g, unsigned enemy)
{
    BattlePoint at = standing(g, slot_of(enemy));
    start_drift_here(g, enemy, at);
    uint8_t heading = battle_direction(g, at, MIDDLE);
    keep_heading(g, enemy, heading);
    face_heading(g, enemy, heading);
    drift(g, enemy, true);
}

/* ---- The movements ----------------------------------------------------------------------- */

bool battle_enemy_movement_needs_target(EnemyMovement movement)
{
    return movement <= MOVEMENT_CIRCLE;
}

/* Waiting for the target to move goes on when the enemy saw no target: it
 * waits on where nobody stands (where_the_original_finds_nobody). */
static bool takes_no_target(EnemyMovement movement) { return movement == MOVEMENT_WAIT_FOR_TARGET; }

void battle_move_enemy(GameState *g, unsigned enemy, EnemyMovement movement, uint8_t animation)
{
    switch (movement) {
    case MOVEMENT_WAIT_FOR_TARGET: wait_for_target_to_move(g, enemy); break;
    case MOVEMENT_APPROACH: approach(g, enemy, animation); break;
    case MOVEMENT_CHARGE: charge(g, enemy); return;
    case MOVEMENT_LINE_UP: line_up(g, enemy, animation); break;
    case MOVEMENT_KEEP_DISTANCE: keep_distance(g, enemy, animation); break;
    case MOVEMENT_CIRCLE: circle(g, enemy, animation); break;
    case MOVEMENT_WEAVE: weave(g, enemy, animation); break;
    case MOVEMENT_MARCH: march(g, enemy, animation); break;
    case MOVEMENT_TO_THE_MIDDLE: make_for_the_middle(g, enemy); break;
    default: return;
    }
    if (movement != MOVEMENT_WAIT_FOR_TARGET)
        ORIGINAL_EFFECT(leave_looked_at(g, slot_of(enemy)));
}

/* ---- The entries ------------------------------------------------------------------------- */

/* Each movement's routine entered by itself, as the original's table calls it:
 * the enemy in battle_effect_enemy, the animation in battle_effect_kind, and A's
 * high byte clear, as the movements' loop leaves it (the original finds the
 * target's place by the whole of A). It takes only the animations that name
 * its movement, as the table does, and an enemy with a target to go by where
 * the movement needs one. */
static bool entry_ready(Snes *s, EnemyMovement movement)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    unsigned enemy = battle_effect_enemy(g);
    bool fits = enemy < BATTLE_ENEMIES && battle_enemy_animation_effect(g, battle_effect_kind(g)) == movement
                && (!battle_enemy_movement_needs_target(movement) || takes_no_target(movement)
                    || battle_names_combatant(target_of(g, enemy)));
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && fits;
}

static void entry(Snes *s, EnemyMovement movement)
{
    GameState *g = state_of(s);
    battle_move_enemy(g, battle_effect_enemy(g), movement, battle_effect_kind(g));
    m_rts(s);
}

#define MOVEMENT_ENTRY(name, movement)                                 \
    bool name##_ready(Snes *s) { return entry_ready(s, movement); } \
    void name(Snes *s) { entry(s, movement); }

MOVEMENT_ENTRY(battle_enemy_waits_for_target, MOVEMENT_WAIT_FOR_TARGET)
MOVEMENT_ENTRY(battle_enemy_approaches, MOVEMENT_APPROACH)
MOVEMENT_ENTRY(battle_enemy_charges, MOVEMENT_CHARGE)
MOVEMENT_ENTRY(battle_enemy_lines_up, MOVEMENT_LINE_UP)
MOVEMENT_ENTRY(battle_enemy_keeps_distance, MOVEMENT_KEEP_DISTANCE)
MOVEMENT_ENTRY(battle_enemy_circles, MOVEMENT_CIRCLE)
MOVEMENT_ENTRY(battle_enemy_weaves, MOVEMENT_WEAVE)
MOVEMENT_ENTRY(battle_enemy_marches, MOVEMENT_MARCH)
MOVEMENT_ENTRY(battle_enemy_makes_for_the_middle, MOVEMENT_TO_THE_MIDDLE)
