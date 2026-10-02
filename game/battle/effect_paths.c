/* Battle: the paths that move the combatants and the anchors while effects
 * play (battle/effects.h).
 *
 * Nineteen movers run a path each: the eleven combatants and the eight
 * anchors. A mover's path runs on a timer, a step every so many frames, and
 * the kind of path decides what a step does:
 *
 *   a line          an 8.8 position stepping by a velocity, from a base
 *                   point, for a number of steps;
 *   a jump          the line, with a height that rises and falls under
 *                   gravity lifting the mover (a combatant's sprite, or the
 *                   anchor itself), bouncing back up when its fall is fast;
 *   a spiral out    a growing distance from the base point along an angle,
 *                   until the point comes to the edge of the screen;
 *   an orbit        round a center at a radius, the angle stepping, for a
 *                   number of steps: a combatant's sprite round where it
 *                   stands, an anchor round a center of its own;
 *   a jump onto a target
 *                   the jump, four substeps a step, until it lands near its
 *                   target;
 *   a flat orbit    the orbit at half the height, and an anchor's also turns
 *                   the effect it carries in front of the combatant it circles
 *                   or behind it, by which half of the circle it is in;
 *   a flicker       a combatant's sprite jumping between where it stands and
 *                   a point off it, every few frames;
 *   a chase         toward a target, turning a few steps of angle a step and
 *                   moving at a speed along the angle, until it is near.
 *
 * A path that ends marks its mover done, which the animation scripts wait
 * on. The frame's update steps the movers (battle_step_effect_slots) while
 * effects play; otherwise it steps the enemies' drifts from a center
 * (battle_step_orbits) and the formation, which keeps the enemies at their
 * places round a leader.
 *
 * The movers' tables hold the combatants' eleven entries and then the
 * anchors' eight. Angles are 256 steps a turn, and a point's distance along
 * an angle is its radius times the sine, rounded down to a whole pixel. */
#include "battle/effects.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/sprite_data.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "vars.h"

/* ---- Movers --------------------------------------------------------------------------- */

enum { COMBATANT_MOVERS = BATTLE_COMBATANTS, ANCHOR_MOVERS = BATTLE_ANCHORS };

static void end_path(GameState *g, Mover m) { set_mover_done_count(g, m, 1); }
static void count_done(GameState *g, Mover m) { set_mover_done_count(g, m, (uint8_t)(mover_done_count(g, m) + 1)); }

/* The homing jump counts the top of its jump with a word increment of the
 * flag, as the original does: a flag at $FF carries into the next byte, the
 * next mover's, or for the last past the table (re/ram.toml). No battle makes
 * $FF; the fuzzer's random states do. */
static void count_turn(GameState *g, Mover m)
{
    count_done(g, m);
    if (mover_done_count(g, m) != 0)
        return;
    unsigned next = m.index + 1u;
    if (mover_is_anchor(m))
        set_battle_anchor_done_unchecked(g, next, (uint8_t)(battle_anchor_done_unchecked(g, next) + 1));
    else
        set_battle_effect_done_unchecked(g, next, (uint8_t)(battle_effect_done_unchecked(g, next) + 1));
}

/* ---- Where a mover is ------------------------------------------------------------------- */

typedef struct {
    int8_t dx, dy;
} Offset;

static BattlePoint moved(BattlePoint p, Offset o) { return (BattlePoint){ (uint8_t)(p.x + o.dx), (uint8_t)(p.y + o.dy) }; }

/* ---- What the original leaves in its cells ------------------------------------------ */

/* The effect loops' cells ($C6-$D1) and the multiply's ($00-$06): the
 * original works in them, and the ports leave their last values there for
 * shadow mode. `cell` is the original's direct-page address. */
static void leave_cell(GameState *g, uint8_t cell, uint8_t v) { set_battle_effect_cells(g, (unsigned)(cell - 0xC6), v); }
static void leave_cell16(GameState *g, uint8_t cell, uint16_t v)
{
    leave_cell(g, cell, (uint8_t)v);
    leave_cell(g, (uint8_t)(cell + 1), (uint8_t)(v >> 8));
}
static void leave_offset_cells(GameState *g, Offset o)
{
    leave_cell(g, 0xCC, (uint8_t)o.dy);
    leave_cell(g, 0xCD, (uint8_t)o.dx);
}

/* The loop's cells while a mover's step runs: the mover, and twice and four
 * times it (its word and long entries' offsets); and what a count down past
 * the first mover leaves. */
static void leave_loop_cells(GameState *g, unsigned index)
{
    leave_cell16(g, 0xC6, (uint16_t)index);
    leave_cell16(g, 0xC8, (uint16_t)(index * 2));
    leave_cell16(g, 0xCA, (uint16_t)(index * 4));
}
static void leave_loop_ended(GameState *g) { leave_cell16(g, 0xC6, 0x00FF); }

/* The multiply: the factor at $00, the word at $02 and the product (24-bit)
 * at $04, and the CPU's multiplier holding the factor times the word's high
 * byte. */
static void leave_product(GameState *g, uint8_t factor, uint16_t word, uint32_t product)
{
    set_battle_low_scratch(g, 0, factor);
    set_battle_low_scratch(g, 2, (uint8_t)word);
    set_battle_low_scratch(g, 3, (uint8_t)(word >> 8));
    set_battle_low_scratch(g, 4, (uint8_t)product);
    set_battle_low_scratch(g, 5, (uint8_t)(product >> 8));
    set_battle_low_scratch(g, 6, (uint8_t)(product >> 16));
    hw_set_multiplicand(g, factor);
    hw_multiply_by(g, (uint8_t)(word >> 8));
}

/* ---- Shared arithmetic --------------------------------------------------------------- */

// $CF:FA17
/* A word scaled by a factor in 256ths: the middle byte of their product. */
static uint8_t scaled(GameState *g, uint8_t factor, uint16_t word)
{
    uint32_t product = (uint32_t)factor * word;
    ORIGINAL_EFFECT(leave_product(g, factor, word, product));
    return (uint8_t)(product >> 8);
}

enum { QUARTER_TURN = 0x40, HALF_TURN = 0x80 };

// $CF:F9FB
/* How far along an axis a point `radius` pixels away at `angle` is: the
 * radius times the sine, rounded down to a whole pixel (so a point a little
 * above the axis is a pixel above). */
static int8_t scaled_sine(GameState *g, uint8_t angle, uint8_t radius)
{
    int16_t sine = battle_sine_magnitude(g, angle);
    if (angle >= HALF_TURN)
        sine = (int16_t)-sine;
    return (int8_t)scaled(g, radius, (uint16_t)sine);
}

/* The offset of a point at an angle, `across` pixels away horizontally and
 * `down` vertically (an ellipse when they differ). */
static Offset polar(GameState *g, uint8_t angle, uint8_t across, uint8_t down)
{
    int8_t dy = scaled_sine(g, angle, down);
    int8_t dx = scaled_sine(g, (uint8_t)(angle + QUARTER_TURN), across);
    return (Offset){ dx, dy };
}

/* How far apart two coordinates are, as the original measures it: their
 * difference as a signed byte, made positive (a difference of 128 stays
 * 128). */
static uint8_t distance(uint8_t a, uint8_t b)
{
    int8_t d = (int8_t)(a - b);
    return (uint8_t)(d < 0 ? -d : d);
}

// $CF:F957
/* Whether two points are less than `limit` apart on both axes. */
static bool points_close(BattlePoint a, BattlePoint b, uint8_t limit)
{
    return distance(a.x, b.x) < limit && distance(a.y, b.y) < limit;
}

static void leave_proximity_test(GameState *g, BattlePoint a, BattlePoint b, uint8_t limit, bool close)
{
    leave_cell(g, 0xCC, a.x);
    leave_cell(g, 0xCD, a.y);
    leave_cell(g, 0xCE, b.x);
    leave_cell(g, 0xCF, b.y);
    leave_cell(g, 0xD1, limit);
    leave_cell(g, 0xD0, close);
}

static bool close_to(GameState *g, BattlePoint a, BattlePoint b, uint8_t limit)
{
    bool close = points_close(a, b, limit);
    ORIGINAL_EFFECT(leave_proximity_test(g, a, b, limit, close));
    return close;
}

/* The chase gives the angle's computation its two points in its cells. */
static void leave_chase_points(GameState *g, BattlePoint from, BattlePoint to)
{
    set_battle_chase_cells(g, 0, from.x);
    set_battle_chase_cells(g, 1, from.y);
    set_battle_chase_cells(g, 2, to.x);
    set_battle_chase_cells(g, 3, to.y);
}

/* What the angle's computation leaves in its cells: the two points, their
 * differences as bytes with the signs, the angle, the distances and their
 * steps in the table. */
static void leave_angle_cells(GameState *g, BattlePoint from, BattlePoint to, uint8_t across, uint8_t down, uint8_t angle)
{
    uint8_t cells[BATTLE_CHASE_CELLS_COUNT] = {
        from.x, from.y, to.x, to.y,
        (uint8_t)(from.x - to.x), from.x < to.x ? 0xFF : 0x00,
        (uint8_t)(from.y - to.y), from.y < to.y ? 0xFF : 0x00,
        angle, (uint8_t)(across / BATTLE_ARCTANGENT_STEP), 0, across, 0, down, 0, (uint8_t)battle_arctangent_index(across, down),
    };
    for (unsigned i = 0; i < BATTLE_CHASE_CELLS_COUNT; i++)
        set_battle_chase_cells(g, i, cells[i]);
}

// $CF:FA43
/* The angle from one point toward another. The table gives the angle within a
 * quarter turn; which quarter the target is in turns it round.
 *
 * The original keeps the table's index as a word in its cells, the last of
 * which is followed by the damage numbers' rising flag: the index's high
 * byte lands on it, so a target 64 pixels or more below or above the point
 * sets the numbers rising, and a nearer one stops them (docs/TODO.md). The
 * game sees that, so it is kept. */
static uint8_t angle_toward(GameState *g, BattlePoint from, BattlePoint to)
{
    bool right = from.x < to.x, below = from.y < to.y;
    uint8_t across = right ? (uint8_t)(to.x - from.x) : (uint8_t)(from.x - to.x);
    uint8_t down = below ? (uint8_t)(to.y - from.y) : (uint8_t)(from.y - to.y);
    uint8_t angle = battle_arctangent(g, across, down);
    if (!right)
        angle = below ? (uint8_t)(HALF_TURN - angle) : (uint8_t)(HALF_TURN + angle);
    else if (!below)
        angle = (uint8_t)-angle;
    ORIGINAL_EFFECT(leave_angle_cells(g, from, to, across, down, angle));
    set_battle_numbers_rising(g, (uint8_t)(battle_arctangent_index(across, down) >> 8));
    return angle;
}

/* ---- The line and the jump ----------------------------------------------------------- */

/* Move the mover's position along its line by its velocity, and return where
 * that puts it: the base point plus the position's whole part. */
static BattlePoint advance_along_line(GameState *g, Mover m)
{
    unsigned e = mover_entry(m);
    uint16_t x = (uint16_t)(battle_effect_position_x(g, e) + battle_effect_velocity_x(g, e));
    uint16_t y = (uint16_t)(battle_effect_position_y(g, e) + battle_effect_velocity_y(g, e));
    set_battle_effect_position_x(g, e, x);
    set_battle_effect_position_y(g, e, y);
    return (BattlePoint){ (uint8_t)((x >> 8) + battle_effect_base_x(g, e)), (uint8_t)((y >> 8) + battle_effect_base_y(g, e)) };
}

/* A jump's speed and height are 16.16 pixels; gravity takes from the speed's
 * fraction while it rises and adds to it while it falls. At the top (the
 * speed gone) the jump turns and falls; a fall whose speed runs past a whole
 * pixel a substep bounces back up at the bounce speed. */
typedef struct {
    uint32_t speed, height;
    bool falling, turned;
} Jump;

enum { WHOLE_PIXEL = 0x10000 };

/* Run the jump `substeps` substeps (a count of 0 runs 65536), counting its
 * turns at the top on the mover's done flag where `count_turns`. The falling
 * flag is written where the jump turns. Returns the height's whole part. */
static uint16_t run_jump(GameState *g, Mover m, uint16_t substeps, bool count_turns)
{
    unsigned e = mover_entry(m);
    uint16_t gravity = battle_effect_jump_gravity(g, e);
    Jump j = { battle_effect_jump_speed(g, e), battle_effect_jump_height(g, e), battle_effect_jump_falling(g, e) != 0, false };
    do {
        uint16_t fraction = (uint16_t)j.speed;
        if (!j.falling) {
            if (fraction < gravity) {
                j.speed = 0;
                j.falling = j.turned = true;
                if (count_turns)
                    count_turn(g, m);
            } else {
                j.speed = (j.speed & ~0xFFFFu) | (uint16_t)(fraction - gravity);
            }
        } else if (fraction + gravity >= WHOLE_PIXEL) {
            j.speed = battle_effect_jump_bounce(g, e);
            j.falling = false;
            j.turned = true;
        } else {
            j.speed = (j.speed & ~0xFFFFu) | (uint16_t)(fraction + gravity);
        }
        j.height = j.falling ? j.height - j.speed : j.height + j.speed;
    } while (--substeps != 0);
    set_battle_effect_jump_speed(g, e, j.speed);
    set_battle_effect_jump_height(g, e, j.height);
    if (j.turned)
        set_battle_effect_jump_falling(g, e, j.falling);
    uint16_t whole = (uint16_t)(j.height >> 16);
    ORIGINAL_EFFECT(leave_cell16(g, 0xCC, 0));
    ORIGINAL_EFFECT(set_battle_low_scratch(g, 4, (uint8_t)whole));
    ORIGINAL_EFFECT(set_battle_low_scratch(g, 5, (uint8_t)(whole >> 8)));
    return whole;
}

/* A coordinate moved by the jump's height: up the screen, or down it for a
 * jump that goes downward; by the height, or by twice it. */
static uint8_t lifted(uint8_t y, uint8_t height, bool downward, bool single)
{
    uint8_t by = single ? height : (uint8_t)(2 * height);
    return downward ? (uint8_t)(y + by) : (uint8_t)(y - by);
}

/* Whether the jump lifts by its height once rather than twice: the flag's
 * low byte, which is all the original looks at. */
static bool lifts_once(const GameState *g, Mover m) { return (uint8_t)battle_effect_jump_single(g, mover_entry(m)) != 0; }

// $CF:F03E, $CF:F4B2
/* A straight line for its number of steps. */
static void line(GameState *g, Mover m)
{
    if (mover_path_done(g, m))
        return;
    unsigned e = mover_entry(m);
    uint8_t left = battle_effect_line_frames(g, e);
    if (left != 0) {
        place_mover(g, m, advance_along_line(g, m));
        set_battle_effect_line_frames(g, e, --left);
    }
    if (left == 0)
        end_path(g, m);
}

// $CF:F087, $CF:F503
/* A jump along the line for its number of steps, its height lifting a
 * combatant's sprite or the anchor itself. A combatant's sprite comes back
 * down to it when the jump ends. */
static void jump(GameState *g, Mover m)
{
    if (mover_path_done(g, m))
        return;
    unsigned e = mover_entry(m);
    uint8_t left = battle_effect_jump_frames(g, e);
    if (left != 0) {
        BattlePoint p = advance_along_line(g, m);
        uint8_t height = (uint8_t)run_jump(g, m, battle_effect_steps(g, e), false);
        bool downward = battle_effect_jump_downward(g, e) != 0, once = lifts_once(g, m);
        if (mover_is_anchor(m)) {
            place_mover(g, m, (BattlePoint){ p.x, lifted(p.y, height, downward, once) });
        } else {
            place_mover(g, m, p);
            set_combatant_sprite_offset_y(g, m.index, lifted(0, height, downward, once));
        }
        set_battle_effect_jump_frames(g, e, --left);
    }
    if (left != 0)
        return;
    if (!mover_is_anchor(m))
        set_combatant_sprite_offset_y(g, m.index, 0);
    end_path(g, m);
}

/* ---- Jumping onto a target ------------------------------------------------------------- */

/* How near the homing jump must fall to its target to land: at first within
 * 16 pixels (24 for an anchor), with a combatant's sprite at most 24 pixels
 * off the ground; after that, on it. */
enum { LANDING_REACH = 16, ANCHOR_LANDING_REACH = 24, LANDING_HEIGHT = 24, ON_TARGET = 1 };
enum { HOMING_SUBSTEPS = 4 };

static BattlePoint target_of(const GameState *g, Mover m)
{
    return (BattlePoint){ battle_effect_target_x(g, mover_entry(m)), battle_effect_target_y(g, mover_entry(m)) };
}

static bool about_to_land(const GameState *g, Mover m)
{
    int8_t lift = combatant_sprite_offset_y(g, m.index);
    return mover_is_anchor(m) || (lift < 0 && lift >= -LANDING_HEIGHT);
}

/* Whether the jump has landed: near the target while falling. The first time
 * it comes near counts as landing and starts its second stage, and a landing
 * counts on the done flag. The original looks only at the falling flag's low
 * byte here. */
static bool landed(GameState *g, Mover m)
{
    unsigned e = mover_entry(m);
    if ((uint8_t)battle_effect_jump_falling(g, e) == 0)
        return false;
    BattlePoint p = mover_position(g, m);
    bool second_stage = battle_effect_home_stage(g, e) != 0;
    if (second_stage) {
        if (!close_to(g, p, target_of(g, m), ON_TARGET))
            return false;
    } else {
        if (!close_to(g, p, target_of(g, m), mover_is_anchor(m) ? ANCHOR_LANDING_REACH : LANDING_REACH) || !about_to_land(g, m))
            return false;
        set_battle_effect_home_stage(g, e, (uint8_t)(battle_effect_home_stage(g, e) + 1));
    }
    count_done(g, m);
    return true;
}

// $CF:F23D, $CF:F6E2
/* A jump onto a target: along the line, four substeps a step, lifted by the
 * height (twice it for a combatant's sprite: the original writes the single
 * lift and then the double over it), until it lands. */
static void jump_to_target(GameState *g, Mover m)
{
    if (landed(g, m))
        return;
    BattlePoint p = advance_along_line(g, m);
    uint8_t height = (uint8_t)run_jump(g, m, HOMING_SUBSTEPS, true);
    if (mover_is_anchor(m)) {
        place_mover(g, m, (BattlePoint){ p.x, lifted(p.y, height, false, lifts_once(g, m)) });
    } else {
        place_mover(g, m, p);
        set_combatant_sprite_offset_y(g, m.index, lifted(0, height, false, false));
    }
}

/* ---- The spiral --------------------------------------------------------------------- */

/* The screen's edge, as the spiral finds it: the outermost 16-pixel columns
 * and the top row of the original's 256 by 224 screen, and the row just
 * below it. */
enum { EDGE = 16, SCREEN_WIDTH = 256, SCREEN_HEIGHT = 224 };

static bool at_screen_edge(BattlePoint p)
{
    return p.y / EDGE == 0 || p.y / EDGE == SCREEN_HEIGHT / EDGE || p.x / EDGE == 0 || p.x / EDGE == SCREEN_WIDTH / EDGE - 1;
}

// $CF:F194, $CF:F61F
/* A spiral out from the base point: each step the distance grows by the
 * mover's step, at its angle, until the mover comes to the edge of the
 * screen or its steps run out. */
static void spiral(GameState *g, Mover m)
{
    if (mover_path_done(g, m))
        return;
    unsigned e = mover_entry(m);
    if (at_screen_edge(mover_position(g, m))) {
        end_path(g, m);
        return;
    }
    uint8_t left = (uint8_t)(battle_effect_spiral_frames(g, e) - 1);
    set_battle_effect_spiral_frames(g, e, left);
    if (left == 0) {
        end_path(g, m);
        return;
    }
    uint8_t radius = (uint8_t)(battle_effect_radius(g, e) + battle_effect_steps(g, e));
    set_battle_effect_radius(g, e, radius);
    uint8_t angle = battle_effect_angle(g, e);
    set_battle_effect_angle_used(g, e, angle);
    Offset o = polar(g, angle, radius, radius);
    ORIGINAL_EFFECT(leave_offset_cells(g, o));
    place_mover(g, m, moved((BattlePoint){ battle_effect_base_x(g, e), battle_effect_base_y(g, e) }, o));
}

/* ---- Orbits ------------------------------------------------------------------------- */

/* The flat orbit's effect goes in front of the combatant it circles on the
 * near half of the circle and behind it on the far half: the side its record
 * names (battle/effects.h). The record keeps the rest of its bits. */
enum { RECORD_KEEPS = 0x3F };

/* The record of the anchor's effect whose side the flat orbit turns: its row
 * is the anchor's, and the column the anchor's record names (the combatant),
 * as the original indexes it, a byte. */
static unsigned facing_record(const GameState *g, unsigned anchor)
{
    return (uint8_t)battle_record_of(battle_anchor_facing_record(g, anchor), anchor);
}

static void turn_effect_by_side(GameState *g, Mover m, uint8_t angle)
{
    unsigned record = facing_record(g, m.index);
    uint8_t side = angle < HALF_TURN ? BATTLE_RECORD_IN_FRONT : BATTLE_RECORD_BEHIND;
    set_battle_object_effect_slots(g, record, (uint8_t)((battle_object_effect_slots(g, record) & RECORD_KEEPS) | side));
}

/* An anchor's orbit moves it within the 256 pixels its coordinates' high
 * bytes put it in. */
static void orbit_anchor_to(GameState *g, unsigned anchor, BattlePoint p)
{
    place_anchor(g, anchor, (int16_t)(((uint16_t)anchor_x(g, anchor) & 0xFF00) | p.x),
                 (int16_t)(((uint16_t)anchor_y(g, anchor) & 0xFF00) | p.y));
}

/* Take the orbit's next step: the angle steps on, and the mover goes the
 * radius from its center (half the radius vertically for a flat orbit). The
 * orbit ends when its steps (a word) run out. */
static void orbit_step(GameState *g, Mover m, bool flat)
{
    unsigned e = mover_entry(m);
    uint8_t radius = battle_effect_orbit_radius(g, e);
    uint8_t angle = (uint8_t)(battle_effect_orbit_angle(g, e) + battle_effect_orbit_step(g, e));
    set_battle_effect_orbit_angle(g, e, angle);
    Offset o = polar(g, angle, radius, flat ? (uint8_t)(radius / 2) : radius);
    if (mover_is_anchor(m)) {
        ORIGINAL_EFFECT(leave_offset_cells(g, o));
        BattlePoint center = { battle_anchor_orbit_center_x(g, m.index), battle_anchor_orbit_center_y(g, m.index) };
        orbit_anchor_to(g, m.index, moved(center, o));
    } else {
        set_combatant_sprite_offset(g, m.index, o.dx, o.dy);
    }
    uint16_t left = (uint16_t)(battle_effect_orbit_frames(g, e) - 1);
    set_battle_effect_orbit_frames(g, e, left);
    if (left == 0)
        count_done(g, m);
}

// $CF:F1F8, $CF:F68D
/* An orbit: a combatant's sprite round where it stands, or an anchor round
 * its center. */
static void orbit(GameState *g, Mover m)
{
    if (!mover_path_done(g, m))
        orbit_step(g, m, false);
}

// $CF:F354, $CF:F7FC
/* A flat orbit; an anchor's turns its effect in front of the combatant it
 * circles or behind it, by where it was on the circle. */
static void orbit_flat(GameState *g, Mover m)
{
    if (mover_is_anchor(m))
        ORIGINAL_EFFECT(set_battle_low_scratch(g, 0, m.index));
    if (mover_path_done(g, m))
        return;
    if (mover_is_anchor(m))
        turn_effect_by_side(g, m, battle_effect_orbit_angle(g, mover_entry(m)));
    orbit_step(g, m, true);
}

/* ---- The flicker -------------------------------------------------------------------- */

/* The flicker moves a combatant: the one whose battle slot is the mover's
 * index, whichever of the movers runs it (an anchor's flicker moves the
 * combatant with the anchor's number). */
enum { MIRRORED_SLOT = 3, MIRRORING_SLOT = 9 };

// $CF:F3BA
/* For its number of steps, every few steps, the combatant's sprite jumps
 * between where the combatant stands and the point the flicker's radius
 * away at the angle the spiral last used. When the steps run out the sprite
 * goes back and the combatant's path stops. */
static void flicker_step(GameState *g, unsigned slot)
{
    uint8_t left = (uint8_t)(battle_effect_flicker_frames(g, slot) - 1);
    set_battle_effect_flicker_frames(g, slot, left);
    if (left == 0) {
        set_combatant_sprite_offset(g, slot, 0, 0);
        set_battle_effect_active(g, slot, false);
        return;
    }
    uint8_t wait = (uint8_t)(battle_effect_flicker_count(g, slot) - 1);
    set_battle_effect_flicker_count(g, slot, wait ? wait : battle_effect_flicker_period(g, slot));
    if (wait != 0)
        return;
    uint8_t radius = battle_effect_flicker_radius(g, slot);
    ORIGINAL_EFFECT(set_battle_low_scratch(g, 0, radius));
    uint8_t hidden = battle_effect_flicker_hidden(g, slot) ^ 1;
    set_battle_effect_flicker_hidden(g, slot, hidden);
    Offset o = hidden ? (Offset){ 0, 0 } : polar(g, battle_effect_angle_used(g, slot), radius, radius);
    set_combatant_sprite_offset(g, slot, o.dx, o.dy);
}

// $CF:F39F
/* The flicker; and a combatant in slot 9 marked to mirror slot 3 shows slot
 * 3's flicker too. */
static void flicker(GameState *g, Mover m)
{
    flicker_step(g, m.index);
    if (m.index == MIRRORED_SLOT && battle_effect_mirror_slot3(g))
        set_combatant_sprite_offset(g, MIRRORING_SLOT, combatant_sprite_offset_x(g, MIRRORED_SLOT),
                                    combatant_sprite_offset_y(g, MIRRORED_SLOT));
}

/* ---- The chase ---------------------------------------------------------------------- */

enum { CHASE_REACH = 16 };

/* Where a chasing anchor is: off the left or top edge (a negative
 * coordinate) counts as 0. */
static BattlePoint chaser_position(const GameState *g, Mover m)
{
    if (!mover_is_anchor(m))
        return mover_position(g, m);
    int16_t x = anchor_x(g, m.index), y = anchor_y(g, m.index);
    return (BattlePoint){ x < 0 ? 0 : (uint8_t)x, y < 0 ? 0 : (uint8_t)y };
}

/* Turn the chase's heading toward the target's angle, in its turning
 * direction, by up to `speed` steps of angle (0: all the way). Reaching it
 * marks the chase lined up, and from then on it heads straight at the
 * target. */
static uint8_t turn_toward(GameState *g, Mover m, uint8_t target, uint8_t speed)
{
    unsigned e = mover_entry(m);
    if (battle_effect_chase_aligned(g, e))
        return target;
    uint8_t heading = battle_effect_chase_angle(g, e);
    if (heading == target)
        return heading;
    bool down = battle_effect_chase_turn(g, e) != 0;
    uint8_t apart = down ? (uint8_t)(heading - target) : (uint8_t)(target - heading);
    if (speed != 0 && apart > speed)
        return down ? (uint8_t)(heading - speed) : (uint8_t)(heading + speed);
    set_battle_effect_chase_aligned(g, e, (uint8_t)(battle_effect_chase_aligned(g, e) + 1));
    return target;
}

/* A chasing anchor moves by the step as words: it can leave the
 * battlefield's 256 pixels. */
static void leave_anchor_step_cells(GameState *g, Offset o)
{
    leave_cell16(g, 0xCA, (uint16_t)(int16_t)o.dy);
    leave_cell16(g, 0xCC, (uint16_t)(int16_t)o.dx);
}

// $CF:F405, $CF:F884
/* A chase: until the mover is within 16 pixels of its target, it turns
 * toward the target, faces its heading, and moves its speed along it. */
static void chase(GameState *g, Mover m)
{
    unsigned e = mover_entry(m);
    BattlePoint from = chaser_position(g, m), to = target_of(g, m);
    ORIGINAL_EFFECT(leave_chase_points(g, from, to));
    if (close_to(g, from, to, CHASE_REACH)) {
        count_done(g, m);
        return;
    }
    uint8_t speed = battle_effect_steps(g, e);
    uint8_t heading = turn_toward(g, m, angle_toward(g, from, to), speed);
    set_battle_effect_chase_angle(g, e, heading);
    uint8_t facing = battle_facing_for_angle(g, heading);
    Offset o = polar(g, heading, speed, speed);
    if (mover_is_anchor(m)) {
        set_battle_anchor_facing(g, m.index, facing);
        ORIGINAL_EFFECT(leave_anchor_step_cells(g, o));
        place_anchor(g, m.index, (int16_t)(anchor_x(g, m.index) + o.dx), (int16_t)(anchor_y(g, m.index) + o.dy));
    } else {
        set_battle_object_facing(g, m.index, facing);
        ORIGINAL_EFFECT(leave_offset_cells(g, o));
        place_mover(g, m, moved(mover_position(g, m), o));
    }
}

/* ---- The movers' timers and their paths ------------------------------------------------ */

/* Each kind of path, and the original's routines for it, a combatant's and
 * an anchor's, whose time its step is. An anchor's flicker is the
 * combatants' routine (it moves a combatant). */
typedef void PathStep(GameState *g, Mover m);

static const struct {
    PathStep *step;
    uint32_t combatant_routine, anchor_routine;
} paths[PATH_KINDS] = {
    [PATH_LINE] = { line, 0xCFF03E, 0xCFF4B2 },
    [PATH_JUMP] = { jump, 0xCFF087, 0xCFF503 },
    [PATH_SPIRAL] = { spiral, 0xCFF194, 0xCFF61F },
    [PATH_ORBIT] = { orbit, 0xCFF1F8, 0xCFF68D },
    [PATH_JUMP_TO_TARGET] = { jump_to_target, 0xCFF23D, 0xCFF6E2 },
    [PATH_ORBIT_FLAT] = { orbit_flat, 0xCFF354, 0xCFF7FC },
    [PATH_FLICKER] = { flicker, 0xCFF39F, 0xCFF39F },
    [PATH_CHASE] = { chase, 0xCFF405, 0xCFF884 },
};

static bool mover_running(const GameState *g, Mover m) { return battle_effect_active(g, mover_entry(m)) != 0; }
static PathKind path_kind(const GameState *g, Mover m) { return (PathKind)battle_effect_type(g, mover_entry(m)); }

/* A running mover's timer counts down a frame; when it runs out it starts
 * again and the path takes a step. */
static bool path_due(GameState *g, Mover m)
{
    if (!mover_running(g, m))
        return false;
    unsigned e = mover_entry(m);
    uint8_t left = (uint8_t)(battle_effect_delay(g, e) - 1);
    set_battle_effect_delay(g, e, left ? left : battle_effect_period(g, e));
    return left == 0;
}

/* ---- The enemies' drifts and the formation ---------------------------------------------- */

/* An enemy's drift: every few frames its distance from a center changes by a
 * step, along a fixed angle, until the distance comes to 8, which ends it. */
enum { DRIFT_ENDS_AT = 8 };

static unsigned enemy_slot(unsigned enemy) { return BATTLE_FIRST_ENEMY + enemy; }

static bool drift_due(GameState *g, unsigned enemy)
{
    if (!battle_orbit_active(g, enemy))
        return false;
    uint8_t left = (uint8_t)(battle_orbit_countdown(g, enemy) - 1);
    set_battle_orbit_countdown(g, enemy, left ? left : battle_orbit_period(g, enemy));
    return left == 0;
}

// $CF:F978
/* Step the enemies' drifts, the last enemy first. */
static void step_drifts(GameState *g)
{
    for (int enemy = BATTLE_ENEMIES - 1; enemy >= 0; enemy--) {
        if (!drift_due(g, (unsigned)enemy))
            continue;
        unsigned slot = enemy_slot((unsigned)enemy);
        uint8_t radius = (uint8_t)(battle_orbit_radius(g, slot) + battle_orbit_radius_step(g, (unsigned)enemy));
        set_battle_orbit_radius(g, slot, radius);
        if (radius == DRIFT_ENDS_AT) {
            set_battle_orbit_active(g, (unsigned)enemy, false);
            set_battle_orbit_radius(g, slot, 0);
        }
        Offset o = polar(g, battle_orbit_angle(g, (unsigned)enemy), radius, radius);
        ORIGINAL_EFFECT(leave_cell(g, 0xC8, (uint8_t)o.dy));
        ORIGINAL_EFFECT(leave_cell(g, 0xC9, (uint8_t)o.dx));
        BattlePoint center = { battle_orbit_center_x(g, (unsigned)enemy), battle_orbit_center_y(g, (unsigned)enemy) };
        BattlePoint p = moved(center, o);
        place_combatant_y(g, enemy_slot((unsigned)enemy), p.y);
        place_combatant_x(g, enemy_slot((unsigned)enemy), p.x);
    }
    ORIGINAL_EFFECT(leave_loop_ended(g));
}

// $CF:F9D3
/* Keep the enemies of the formation at their places round its leader: each
 * enemy on the battlefield, the last first, at its place's offset from where
 * the leader is then (the leader moves to its own place too, and the enemies
 * before it follow it there). */
static void place_formation(GameState *g)
{
    unsigned leader = enemy_slot(battle_formation_leader(g));
    for (int enemy = BATTLE_ENEMIES - 1; enemy >= 0; enemy--) {
        unsigned slot = enemy_slot((unsigned)enemy);
        if (!battle_slot_present(g, slot))
            continue;
        place_combatant_x(g, slot, (uint8_t)(combatant_x(g, leader) + battle_formation_offset_x(g, (unsigned)enemy)));
        place_combatant_y(g, slot, (uint8_t)(combatant_y(g, leader) + battle_formation_offset_y(g, (unsigned)enemy)));
    }
}

/* ---- The entries ------------------------------------------------------------------------ */

bool battle_effect_page(Snes *s) { return battle_on_page(s); }
bool battle_effect_handler_ready(Snes *s) { return battle_on_page_a_high_clear(s); }

/* The movers' loops: each running mover's timer counts down, the combatants
 * from the last to the first and then the anchors, and a mover whose timer
 * ran out takes its path's step, in the time of the original's routine for
 * it. */
static void step_movers(Snes *s, MoverKind kind, unsigned movers)
{
    GameState *g = state_of(s);
    for (int index = (int)movers - 1; index >= 0; index--) {
        Mover m = { kind, (uint8_t)index };
        if (!path_due(g, m))
            continue;
        ORIGINAL_EFFECT(leave_loop_cells(g, (unsigned)index));
        PathKind k = path_kind(g, m);
        NATIVE_TIME_AS(s, mover_is_anchor(m) ? paths[k].anchor_routine : paths[k].combatant_routine, paths[k].step(g, m));
    }
    ORIGINAL_EFFECT(leave_loop_ended(g));
}

// $CF:EFAF
/* The frame's paths: while effects play, every mover's; otherwise, unless
 * they are held, the enemies' drifts ($CF:F978, which the original jumps
 * to). */
void battle_step_effect_slots(Snes *s)
{
    GameState *g = state_of(s);
    if (battle_effects_running(g)) {
        step_movers(s, MOVES_COMBATANT, COMBATANT_MOVERS);
        step_movers(s, MOVES_ANCHOR, ANCHOR_MOVERS);
    } else if (!battle_orbits_held(g)) {
        NATIVE_TIME_AS(s, 0xCFF978, step_drifts(g));
    }
    m_rts(s);
}

/* The game gives a running mover one of the eight kinds of path. */
static bool paths_known(Snes *s)
{
    GameState *g = state_of(s);
    vars_peek(true);
    bool known = true;
    for (unsigned e = 0; e < COMBATANT_MOVERS + ANCHOR_MOVERS; e++)
        if (battle_effect_active(g, e) && battle_effect_type(g, e) >= PATH_KINDS)
            known = false;
    vars_peek(false);
    return known;
}

bool battle_effect_paths_ready(Snes *s) { return battle_effect_handler_ready(s) && paths_known(s); }

void battle_step_orbits(Snes *s)
{
    step_drifts(state_of(s));
    m_rts(s);
}

void battle_place_formation(Snes *s)
{
    place_formation(state_of(s));
    m_rts(s);
}

/* A path's routine on its own takes its mover from the loop's cells: the
 * mover, and twice and four times it, which its word and long tables are
 * indexed by. The guards take the cells as the loop sets them. */
static uint16_t cell_word(Snes *s, uint8_t cell)
{
    GameState *g = state_of(s);
    return (uint16_t)(battle_effect_cells(g, (unsigned)(cell - 0xC6))
                      | battle_effect_cells(g, (unsigned)(cell - 0xC6 + 1)) << 8);
}

static bool loop_cells_name(Snes *s, unsigned movers)
{
    vars_peek(true);
    uint16_t index = cell_word(s, 0xC6);
    bool named = index < movers && cell_word(s, 0xC8) == index * 2 && cell_word(s, 0xCA) == index * 4;
    vars_peek(false);
    return named;
}

bool battle_path_cells_name_combatant(Snes *s) { return battle_effect_handler_ready(s) && loop_cells_name(s, COMBATANT_MOVERS); }
bool battle_path_cells_name_anchor(Snes *s) { return battle_effect_handler_ready(s) && loop_cells_name(s, ANCHOR_MOVERS); }

static Mover mover_in_cells(Snes *s, MoverKind kind) { return (Mover){ kind, (uint8_t)cell_word(s, 0xC6) }; }

static void run_path(Snes *s, MoverKind kind, PathStep *step)
{
    step(state_of(s), mover_in_cells(s, kind));
    m_rts(s);
}

void battle_effect_combatant_line(Snes *s) { run_path(s, MOVES_COMBATANT, line); }
void battle_effect_combatant_jump(Snes *s) { run_path(s, MOVES_COMBATANT, jump); }
void battle_effect_combatant_spiral(Snes *s) { run_path(s, MOVES_COMBATANT, spiral); }
void battle_effect_combatant_orbit(Snes *s) { run_path(s, MOVES_COMBATANT, orbit); }
void battle_effect_combatant_jump_to_target(Snes *s) { run_path(s, MOVES_COMBATANT, jump_to_target); }
void battle_effect_combatant_orbit_flat(Snes *s) { run_path(s, MOVES_COMBATANT, orbit_flat); }
void battle_effect_flicker(Snes *s) { run_path(s, MOVES_COMBATANT, flicker); }
void battle_effect_combatant_chase(Snes *s) { run_path(s, MOVES_COMBATANT, chase); }
void battle_effect_anchor_line(Snes *s) { run_path(s, MOVES_ANCHOR, line); }
void battle_effect_anchor_jump(Snes *s) { run_path(s, MOVES_ANCHOR, jump); }
void battle_effect_anchor_spiral(Snes *s) { run_path(s, MOVES_ANCHOR, spiral); }
void battle_effect_anchor_orbit(Snes *s) { run_path(s, MOVES_ANCHOR, orbit); }
void battle_effect_anchor_jump_to_target(Snes *s) { run_path(s, MOVES_ANCHOR, jump_to_target); }
void battle_effect_anchor_orbit_facing(Snes *s) { run_path(s, MOVES_ANCHOR, orbit_flat); }
void battle_effect_anchor_chase(Snes *s) { run_path(s, MOVES_ANCHOR, chase); }

void battle_effect_flicker_step(Snes *s)
{
    flicker_step(state_of(s), cell_word(s, 0xC6));
    m_rts(s);
}

/* The arithmetic on its own: its operands are in the cells the handlers
 * leave them in, and the callers read the result in A (its high byte 0, as
 * the original's clear leaves it) and its sign, or in the proximity test's
 * cell. */
static void return_byte(Snes *s, uint8_t v)
{
    s->cpu.a = v;
    m_set_nz8(s, v);
    m_rts(s);
}

static uint8_t cell(Snes *s, uint8_t cell) { return battle_effect_cells(state_of(s), (unsigned)(cell - 0xC6)); }

/* The two points at $CC and $CE and the limit at $D1; the answer at $D0. */
void battle_points_are_close(Snes *s)
{
    BattlePoint a = { cell(s, 0xCC), cell(s, 0xCD) }, b = { cell(s, 0xCE), cell(s, 0xCF) };
    set_battle_effect_cells(state_of(s), 0xD0 - 0xC6, points_close(a, b, cell(s, 0xD1)));
    m_rts(s);
}

void battle_sine_scale(Snes *s)
{
    GameState *g = state_of(s);
    return_byte(s, (uint8_t)scaled_sine(g, (uint8_t)s->cpu.a, battle_low_scratch(g, 0)));
}

void battle_multiply_scale(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t word = (uint16_t)(battle_low_scratch(g, 2) | battle_low_scratch(g, 3) << 8);
    return_byte(s, scaled(g, battle_low_scratch(g, 0), word));
}

void battle_chase_angle(Snes *s)
{
    GameState *g = state_of(s);
    BattlePoint from = { battle_chase_cells(g, 0), battle_chase_cells(g, 1) };
    BattlePoint to = { battle_chase_cells(g, 2), battle_chase_cells(g, 3) };
    angle_toward(g, from, to);
    s->cpu.a &= 0x00FF;
    m_rts(s);
}
