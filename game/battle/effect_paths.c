/* Battle: timed effect slots -- the paths combatants and anchor points move on.
 *
 * Nineteen slots run on a timer each (battle_effect_active, _delay, _period,
 * _type): slots 0-10 move the combatants of battle slots 0-10 (object $0C +
 * slot), slots 11-18 the eight anchor points effects are drawn at
 * (battle_anchor_x/_y). When a slot's delay runs out its handler runs, by its
 * type, from one of two tables of eight ($CF:F01E, $CF:F02E):
 *
 *   0 a straight line: an 8.8 position stepping by a velocity for a count
 *   1 a jump: the line, plus a height that rises and falls under gravity
 *   2 a spiral out: a growing radius at an angle, until the point leaves the
 *     middle of the screen
 *   3 an orbit: the combatant's sprite offset, or the anchor, circling
 *   4 a jump onto a target, until it is near it
 *   5 an orbit with half the height (combatants), or with the object's facing
 *     following the angle (anchors)
 *   6 a flicker of the sprite offset, on and off
 *   7 a chase: toward a target, turning a step at a time
 *
 * The per-slot variables are arrays of 19 (the anchors' are elements 11-18),
 * so a handler takes the slot's index in them. The handlers run with the
 * battle page and data bank $7E (the entries' guard) and A's high byte 0, as
 * the loop leaves it; they return what they leave in A, which the loop hands
 * on (battle_step_effect_slots still reproduces it). Called by themselves they
 * leave A's low byte as it is: their callers don't read it (DC_AL). */
#include "battle/battle.h"
#include "battle/effects.h"

#include "port/machine.h"
#include "ram.h"

enum {
    COMBATANT_SLOTS = 11,
    ANCHOR_SLOTS = 8,
    FIRST_ANCHOR = COMBATANT_SLOTS,
    COMBATANT_OBJECT = 0x0C,   /* battle object of battle slot 0 */
    QUARTER_TURN = 0x40,
};

/* ---- The loop's cells (battle_effect_cells, $C6 up) ----------------------------------- */

enum { CELL_SLOT = 0x00, CELL_SLOT_X2 = 0x02, CELL_SLOT_X4 = 0x04, CELL_A = 0x06, CELL_B = 0x07, CELL_C = 0x08,
       CELL_D = 0x09, CELL_CLOSE = 0x0A, CELL_LIMIT = 0x0B };

static uint8_t cell(Snes *s, uint8_t c) { return m_read8(s, RAM_BATTLE_EFFECT_CELLS + c); }
static void set_cell(Snes *s, uint8_t c, uint8_t v) { m_write8(s, RAM_BATTLE_EFFECT_CELLS + c, v); }
static uint16_t cell16(Snes *s, uint8_t c) { return m_read16(s, RAM_BATTLE_EFFECT_CELLS + c); }
static void set_cell16(Snes *s, uint8_t c, uint16_t v) { m_write16(s, RAM_BATTLE_EFFECT_CELLS + c, v); }

/* The low scratch the multiply uses: the radius at $00, the product at $04. */
static void set_radius(Snes *s, uint8_t r) { m_write8(s, RAM_BATTLE_LOW_SCRATCH, r); }
static void set_product(Snes *s, uint16_t v) { m_write16(s, RAM_BATTLE_LOW_SCRATCH + 4, v); }

/* An element of one of the 19-slot arrays, at the index the original uses
 * (the slot, twice it for words, four times it for the jump's 32-bit values,
 * each read from the loop's cells). */
static uint8_t at8(Snes *s, uint32_t array, uint32_t i) { return m_read8(s, array + i); }
static void set_at8(Snes *s, uint32_t array, uint32_t i, uint8_t v) { m_write8(s, array + i, v); }
static uint16_t at16(Snes *s, uint32_t array, uint32_t i) { return m_read16(s, array + i); }
static void set_at16(Snes *s, uint32_t array, uint32_t i, uint16_t v) { m_write16(s, array + i, v); }
static uint8_t inc8(Snes *s, uint32_t addr)
{
    uint8_t v = (uint8_t)(m_read8(s, addr) + 1);
    m_write8(s, addr, v);
    return v;
}
static uint8_t dec8(Snes *s, uint32_t addr)
{
    uint8_t v = (uint8_t)(m_read8(s, addr) - 1);
    m_write8(s, addr, v);
    return v;
}

/* A combatant's object position, by effect slot. */
static uint32_t object_x(uint16_t slot) { return RAM_BATTLE_OBJECT_X + COMBATANT_OBJECT + slot; }
static uint32_t object_y(uint16_t slot) { return RAM_BATTLE_OBJECT_Y + COMBATANT_OBJECT + slot; }

/* Count a slot's 16-bit frame count down (the original's two 8-bit SBCs):
 * returns the OR of its two bytes, zero when it ran out. */
static uint8_t count_down16(Snes *s, uint32_t array, uint32_t i)
{
    uint16_t v = (uint16_t)(at16(s, array, i) - 1);
    set_at16(s, array, i, v);
    return (uint8_t)((v >> 8) | (v & 0xFF));
}

/* ---- Shared arithmetic ($CF:F957, $CF:F9FB, $CF:FA17, $CF:FA43) --------------------- */

// $CF:F957
/* Are the points in cells A, B and C, D less than the limit in cell F apart
 * on both axes? The answer also goes to cell E. `distance` gets the last
 * distance measured, which the original leaves in A. */
static bool points_close(Snes *s, uint8_t *distance)
{
    set_cell(s, CELL_CLOSE, 0);
    uint8_t limit = cell(s, CELL_LIMIT);
    for (int axis = 0; axis < 2; axis++) {
        uint8_t d = (uint8_t)(cell(s, (uint8_t)(CELL_A + axis)) - cell(s, (uint8_t)(CELL_C + axis)));
        if (d & 0x80)
            d = (uint8_t)-d;
        *distance = d;
        if (d >= limit)
            return false;
    }
    set_cell(s, CELL_CLOSE, 1);
    return true;
}

bool battle_points_close(Snes *s)
{
    uint8_t distance;
    return points_close(s, &distance);
}

void battle_points_are_close(Snes *s)
{
    uint8_t distance = 0;
    points_close(s, &distance);
    m_set_a8(s, distance);
    m_rts(s);
}

// $CF:FA17
uint8_t battle_multiply_scaled(Snes *s, uint8_t factor, uint16_t v)
{
    m_write8(s, 0x004202, factor);
    m_write8(s, 0x004203, (uint8_t)v);
    uint16_t low = m_read16(s, 0x004216);
    m_write8(s, RAM_BATTLE_LOW_SCRATCH + 6, 0);
    m_write8(s, 0x004203, (uint8_t)(v >> 8));
    set_product(s, low);
    uint16_t high = (uint16_t)(m_read16(s, RAM_BATTLE_LOW_SCRATCH + 5) + m_read16(s, 0x004216));
    m_write16(s, RAM_BATTLE_LOW_SCRATCH + 5, high);
    return (uint8_t)high;
}

// $CF:F9FB
uint8_t battle_sine_scaled(Snes *s, uint16_t angle)
{
    uint16_t at = (uint16_t)(angle << 2) & 0x03FF;
    uint16_t v = battle_sine_entry(s, at);
    if (at >= 0x0200)
        v = (uint16_t)-v;
    m_write16(s, RAM_BATTLE_LOW_SCRATCH + 2, v);
    return battle_multiply_scaled(s, m_read8(s, RAM_BATTLE_LOW_SCRATCH), v);
}

/* The sine and cosine of `angle` scaled by `radius`, into cells A and B. */
static void polar(Snes *s, uint8_t radius_y, uint8_t radius_x, uint8_t angle, uint8_t *dy, uint8_t *dx)
{
    set_radius(s, radius_y);
    *dy = battle_sine_scaled(s, angle);
    set_radius(s, radius_x);
    *dx = battle_sine_scaled(s, (uint8_t)(angle + QUARTER_TURN));
}

// $CF:FA43
/* The angle (256 steps a turn) from the chase's point to its target
 * (battle_chase_cells: from X, Y, to X, Y), through the arctangent table at
 * $C0:F300 by the distances in eighths, into the chase's target angle. The
 * original keeps its working values in the cells after them. Returns the
 * target angle, or what the original leaves in A. */
static uint8_t angle_to_target(Snes *s)
{
    uint32_t c = RAM_BATTLE_CHASE_CELLS;
    uint8_t dx = (uint8_t)(m_read8(s, c + 0) - m_read8(s, c + 2));
    uint8_t dx_sign = m_read8(s, c + 0) < m_read8(s, c + 2) ? 0xFF : 0x00;
    m_write8(s, c + 4, dx);
    m_write8(s, c + 5, dx_sign);
    uint8_t dy = (uint8_t)(m_read8(s, c + 1) - m_read8(s, c + 3));
    uint8_t dy_sign = m_read8(s, c + 1) < m_read8(s, c + 3) ? 0xFF : 0x00;
    m_write8(s, c + 6, dy);
    m_write8(s, c + 7, dy_sign);
    /* magnitudes: EOR with the sign, less the sign */
    m_write8(s, c + 0x0B, (uint8_t)((dx ^ dx_sign) - dx_sign));
    m_write8(s, c + 0x0C, 0);
    m_write8(s, c + 0x0D, (uint8_t)((dy ^ dy_sign) - dy_sign));
    m_write8(s, c + 0x0E, 0);
    uint16_t column = (uint16_t)(m_read16(s, c + 0x0B) >> 3);
    m_write16(s, c + 0x09, column);
    uint16_t index = (uint16_t)(((m_read16(s, c + 0x0D) & 0xFFF8) << 2) + column);
    m_write16(s, c + 0x0F, index);
    uint8_t angle = battle_arctangent(s, index);
    m_write8(s, c + 0x08, angle);
    if (!(dx_sign & 0x80)) {
        angle = (dy_sign & 0x80) ? (uint8_t)(0x80 - angle) : (uint8_t)(0x80 + angle);
        m_write8(s, c + 0x08, angle);
        return angle;
    }
    if (!(dy_sign & 0x80)) {
        angle = (uint8_t)(0 - angle);
        m_write8(s, c + 0x08, angle);
    }
    return m_read8(s, c + 0x08);
}


/* ---- The handlers --------------------------------------------------------------------
 *
 * A handler takes its slot from the loop's cells as the original does: the
 * slot at $C6 and twice and four times it at $C8 and $CA. For the anchors'
 * table the same offsets reach elements 11-18 of the arrays, as the
 * original's bases are 11 elements further on. */

typedef struct {
    bool anchor;
    uint16_t slot, w, l;   /* $C6, $C8, $CA */
    uint32_t i, wi, li;    /* the offsets into the 19-slot arrays, which run on into bank $7F */
    uint32_t done;         /* battle_effect_done or battle_anchor_done */
} Slot;

static Slot slot_of(Snes *s, bool anchor)
{
    Slot k = { anchor, cell16(s, CELL_SLOT), cell16(s, CELL_SLOT_X2), cell16(s, CELL_SLOT_X4), 0, 0, 0,
               anchor ? RAM_BATTLE_ANCHOR_DONE : RAM_BATTLE_EFFECT_DONE };
    k.i = (uint32_t)k.slot + (anchor ? FIRST_ANCHOR : 0);
    k.wi = (uint32_t)k.w + (anchor ? 2 * FIRST_ANCHOR : 0);
    k.li = (uint32_t)k.l + (anchor ? 4 * FIRST_ANCHOR : 0);
    return k;
}

static uint8_t done_of(Snes *s, const Slot *k) { return at8(s, k->done, k->slot); }
static uint8_t finish(Snes *s, const Slot *k)
{
    set_at8(s, k->done, k->slot, 1);
    return 1;
}

/* Step the slot's 8.8 position by its velocity. */
static void step_position(Snes *s, const Slot *k)
{
    uint32_t w = k->wi;
    set_at16(s, RAM_BATTLE_EFFECT_POSITION_X, w,
             (uint16_t)(at16(s, RAM_BATTLE_EFFECT_POSITION_X, w) + at16(s, RAM_BATTLE_EFFECT_VELOCITY_X, w)));
    set_at16(s, RAM_BATTLE_EFFECT_POSITION_Y, w,
             (uint16_t)(at16(s, RAM_BATTLE_EFFECT_POSITION_Y, w) + at16(s, RAM_BATTLE_EFFECT_VELOCITY_Y, w)));
}

/* The position's whole part plus the slot's base. */
static uint8_t placed_x(Snes *s, const Slot *k)
{
    return (uint8_t)(at8(s, RAM_BATTLE_EFFECT_POSITION_X + 1, k->wi) + at8(s, RAM_BATTLE_EFFECT_BASE_X, k->i));
}
static uint8_t placed_y(Snes *s, const Slot *k)
{
    return (uint8_t)(at8(s, RAM_BATTLE_EFFECT_POSITION_Y + 1, k->wi) + at8(s, RAM_BATTLE_EFFECT_BASE_Y, k->i));
}

/* An anchor's coordinate: the byte, and 0 above it. */
static void set_anchor(Snes *s, uint32_t anchor, uint16_t w, uint8_t v)
{
    set_at8(s, anchor, w, v);
    set_at8(s, anchor + 1, w, 0);
}

/* The jump's height: `steps` substeps (a 16-bit count kept in cell A, which
 * runs 65536 times for 0) of a speed falling under gravity while the height
 * rises and growing while it falls. When the speed runs out the jump turns
 * (and the homing jump counts that as arriving: `turn_done`), and when it
 * passes the bounce speed it turns back. Speed and height are 32-bit, their
 * whole parts in the high words. Leaves the height's whole part in $04 and
 * returns it. */
static uint16_t step_jump(Snes *s, const Slot *k, uint16_t steps, bool turn_done)
{
    uint32_t w = k->wi, l = k->li;
    uint32_t falling = RAM_BATTLE_EFFECT_JUMP_FALLING, speed = RAM_BATTLE_EFFECT_JUMP_SPEED;
    uint32_t height = RAM_BATTLE_EFFECT_JUMP_HEIGHT;
    set_cell16(s, CELL_A, steps);
    do {
        if (at16(s, falling, w) == 0) {
            uint16_t v = at16(s, speed, l), g = at16(s, RAM_BATTLE_EFFECT_JUMP_GRAVITY, w);
            set_at16(s, speed, l, (uint16_t)(v - g));
            if (v < g) {
                set_at16(s, speed, l, 0);
                set_at16(s, speed + 2, l, 0);
                set_at16(s, falling, w, 1);
                if (turn_done)   /* a 16-bit INC: it carries into the next slot's byte */
                    m_write16(s, k->done + k->slot, (uint16_t)(m_read16(s, k->done + k->slot) + 1));
            }
        } else {
            uint32_t v = (uint32_t)at16(s, speed, l) + at16(s, RAM_BATTLE_EFFECT_JUMP_GRAVITY, w);
            set_at16(s, speed, l, (uint16_t)v);
            if (v > 0xFFFF) {
                set_at16(s, speed, l, at16(s, RAM_BATTLE_EFFECT_JUMP_BOUNCE, w));
                set_at16(s, speed + 2, l, 0);
                set_at16(s, falling, w, 0);
            }
        }
        uint32_t sp = (uint32_t)at16(s, speed, l) | (uint32_t)at16(s, speed + 2, l) << 16;
        uint32_t h = (uint32_t)at16(s, height, l) | (uint32_t)at16(s, height + 2, l) << 16;
        h = at16(s, falling, w) == 0 ? h + sp : h - sp;
        set_at16(s, height, l, (uint16_t)h);
        set_at16(s, height + 2, l, (uint16_t)(h >> 16));
        set_cell16(s, CELL_A, (uint16_t)(cell16(s, CELL_A) - 1));
    } while (cell16(s, CELL_A) != 0);
    uint16_t whole = at16(s, height + 2, l);
    set_product(s, whole);
    return whole;
}

/* A coordinate moved up or down by the height, once or twice (the original's
 * chains of 8-bit ADC and SBC). */
static uint8_t lifted(uint8_t base, uint8_t h, bool up, bool single)
{
    if (up)
        return single ? (uint8_t)(base + h) : (uint8_t)(base + h + h);
    return single ? (uint8_t)(base - h) : (uint8_t)(base - h - h);
}

/* Whether a coordinate has left the middle of the screen: its 16-pixel column
 * or row is the first, or `last`. */
static bool off_middle(uint8_t v, uint8_t last) { return (v >> 4) == 0 || (v >> 4) == last; }

// $CF:F03E, $CF:F4B2
/* A straight line, for a count of frames. */
static uint8_t move_line(Snes *s, bool anchor)
{
    Slot k = slot_of(s, anchor);
    uint8_t d = done_of(s, &k);
    if (d)
        return d;
    uint32_t frames = RAM_BATTLE_EFFECT_LINE_FRAMES + k.i;
    if (m_read8(s, frames) == 0)
        return finish(s, &k);
    step_position(s, &k);
    uint8_t a;
    if (anchor) {
        set_anchor(s, RAM_BATTLE_ANCHOR_X, k.w, placed_x(s, &k));
        set_anchor(s, RAM_BATTLE_ANCHOR_Y, k.w, placed_y(s, &k));
        a = 0;
    } else {
        m_write8(s, object_x(k.slot), placed_x(s, &k));
        a = placed_y(s, &k);
        m_write8(s, object_y(k.slot), a);
    }
    return dec8(s, frames) == 0 ? finish(s, &k) : a;
}

// $CF:F087, $CF:F503
/* A jump: the line, with the height (the slot's substeps a frame) lifting
 * the combatant's sprite or the anchor, for a count of frames. A combatant's
 * sprite offset goes back to 0 when it ends. */
static uint8_t move_jump(Snes *s, bool anchor)
{
    Slot k = slot_of(s, anchor);
    uint8_t d = done_of(s, &k);
    if (d)
        return d;
    uint32_t frames = RAM_BATTLE_EFFECT_JUMP_FRAMES + k.i;
    if (m_read8(s, frames) != 0) {
        step_position(s, &k);
        uint8_t h = (uint8_t)step_jump(s, &k, at8(s, RAM_BATTLE_EFFECT_STEPS, k.i), false);
        bool up = at8(s, RAM_BATTLE_EFFECT_JUMP_UP, k.i);
        bool single = at8(s, RAM_BATTLE_EFFECT_JUMP_SINGLE, k.wi);
        uint8_t a;
        if (anchor) {
            set_anchor(s, RAM_BATTLE_ANCHOR_X, k.w, placed_x(s, &k));
            set_anchor(s, RAM_BATTLE_ANCHOR_Y, k.w, lifted(placed_y(s, &k), h, up, single));
            a = 0;
        } else {
            m_write8(s, object_x(k.slot), placed_x(s, &k));
            m_write8(s, object_y(k.slot), placed_y(s, &k));
            a = lifted(0, h, up, single);
            m_write8(s, RAM_BATTLE_SPRITE_OFFSET_Y + k.slot, a);
        }
        if (dec8(s, frames) != 0)
            return a;
    }
    if (!anchor)
        m_write8(s, RAM_BATTLE_SPRITE_OFFSET_Y + k.slot, 0);
    return finish(s, &k);
}

// $CF:F194, $CF:F61F
/* A spiral out from the slot's base: the radius grows by the slot's step
 * each frame at the slot's angle, until the point leaves the middle of the
 * screen or a count of frames runs out. */
static uint8_t move_spiral(Snes *s, bool anchor)
{
    Slot k = slot_of(s, anchor);
    uint8_t d = done_of(s, &k);
    if (d)
        return d;
    uint8_t y = anchor ? m_read8(s, RAM_BATTLE_ANCHOR_Y + k.w) : m_read8(s, object_y(k.slot));
    uint8_t x = anchor ? m_read8(s, RAM_BATTLE_ANCHOR_X + k.w) : m_read8(s, object_x(k.slot));
    if (off_middle(y, 0x0E) || off_middle(x, 0x0F) || dec8(s, RAM_BATTLE_EFFECT_SPIRAL_FRAMES + k.i) == 0)
        return finish(s, &k);
    uint8_t radius = (uint8_t)(at8(s, RAM_BATTLE_EFFECT_RADIUS, k.i) + at8(s, RAM_BATTLE_EFFECT_STEPS, k.i));
    set_at8(s, RAM_BATTLE_EFFECT_RADIUS, k.i, radius);
    uint8_t angle = at8(s, RAM_BATTLE_EFFECT_ANGLE, k.i);
    set_at8(s, RAM_BATTLE_EFFECT_ANGLE_USED, k.i, angle);
    uint8_t dy, dx;
    polar(s, radius, radius, angle, &dy, &dx);
    set_cell(s, CELL_A, dy);
    set_cell(s, CELL_B, dx);
    y = (uint8_t)(at8(s, RAM_BATTLE_EFFECT_BASE_Y, k.i) + dy);
    x = (uint8_t)(at8(s, RAM_BATTLE_EFFECT_BASE_X, k.i) + dx);
    if (anchor) {
        set_anchor(s, RAM_BATTLE_ANCHOR_Y, k.w, y);
        set_anchor(s, RAM_BATTLE_ANCHOR_X, k.w, x);
    } else {
        m_write8(s, object_y(k.slot), y);
        m_write8(s, object_x(k.slot), x);
    }
    return x;
}

// $CF:F1F8, $CF:F354, $CF:F68D
/* An orbit for a 16-bit count of frames: the angle steps each frame and the
 * point is the radius away (the vertical radius halved for the flat one): a
 * combatant's sprite offset, or an anchor around its center. */
static uint8_t move_orbit(Snes *s, bool anchor, bool flat)
{
    Slot k = slot_of(s, anchor);
    uint8_t d = done_of(s, &k);
    if (d)
        return d;
    uint8_t radius = at8(s, RAM_BATTLE_EFFECT_ORBIT_RADIUS, k.i);
    uint8_t angle = (uint8_t)(at8(s, RAM_BATTLE_EFFECT_ORBIT_ANGLE, k.i) + at8(s, RAM_BATTLE_EFFECT_ORBIT_STEP, k.i));
    set_at8(s, RAM_BATTLE_EFFECT_ORBIT_ANGLE, k.i, angle);
    uint8_t dy, dx;
    polar(s, flat ? (uint8_t)(radius >> 1) : radius, radius, angle, &dy, &dx);
    if (anchor) {
        set_cell(s, CELL_A, dy);
        set_cell(s, CELL_B, dx);
        m_write8(s, RAM_BATTLE_ANCHOR_X + k.w, (uint8_t)(at8(s, RAM_BATTLE_ANCHOR_ORBIT_CENTER_X, k.slot) + dx));
        m_write8(s, RAM_BATTLE_ANCHOR_Y + k.w, (uint8_t)(at8(s, RAM_BATTLE_ANCHOR_ORBIT_CENTER_Y, k.slot) + dy));
    } else {
        m_write8(s, RAM_BATTLE_SPRITE_OFFSET_Y + k.slot, dy);
        m_write8(s, RAM_BATTLE_SPRITE_OFFSET_X + k.slot, dx);
    }
    uint8_t left = count_down16(s, RAM_BATTLE_EFFECT_ORBIT_FRAMES, k.wi);
    if (left == 0)
        inc8(s, k.done + k.slot);
    return left;
}

// $CF:F7FC
/* The anchors' flat orbit with a facing: the facing bits of the anchor's
 * object record (the slot's 11 bytes, plus the anchor's offset) follow which
 * half of the turn the angle is in. */
static uint8_t move_orbit_facing(Snes *s)
{
    Slot k = slot_of(s, true);
    uint8_t slot = (uint8_t)k.slot;
    m_write8(s, RAM_BATTLE_LOW_SCRATCH, slot);
    uint8_t d = at8(s, k.done, slot);
    if (d)
        return d;
    m_write8(s, RAM_BATTLE_LOW_SCRATCH + 2, 0x0B);
    m_write8(s, RAM_BATTLE_LOW_SCRATCH + 3, 0);
    battle_multiply_scaled(s, slot, 0x000B);
    uint8_t record = (uint8_t)(m_read8(s, RAM_BATTLE_LOW_SCRATCH + 4) + at8(s, RAM_BATTLE_ANCHOR_FACING_RECORD, slot));
    uint8_t bits = at8(s, RAM_BATTLE_OBJECT_EFFECT_SLOTS, record) & 0x3F;
    bits |= at8(s, RAM_BATTLE_EFFECT_ORBIT_ANGLE, k.i) < 0x80 ? 0x80 : 0x40;
    set_at8(s, RAM_BATTLE_OBJECT_EFFECT_SLOTS, record, bits);
    uint8_t radius = at8(s, RAM_BATTLE_EFFECT_ORBIT_RADIUS, k.i);
    uint8_t angle = (uint8_t)(at8(s, RAM_BATTLE_EFFECT_ORBIT_ANGLE, k.i) + at8(s, RAM_BATTLE_EFFECT_ORBIT_STEP, k.i));
    set_at8(s, RAM_BATTLE_EFFECT_ORBIT_ANGLE, k.i, angle);
    uint8_t dy, dx;
    polar(s, (uint8_t)(radius >> 1), radius, angle, &dy, &dx);
    set_cell(s, CELL_A, dy);
    set_cell(s, CELL_B, dx);
    m_write8(s, RAM_BATTLE_ANCHOR_X + k.w, (uint8_t)(at8(s, RAM_BATTLE_ANCHOR_ORBIT_CENTER_X, slot) + dx));
    m_write8(s, RAM_BATTLE_ANCHOR_Y + k.w, (uint8_t)(at8(s, RAM_BATTLE_ANCHOR_ORBIT_CENTER_Y, slot) + dy));
    uint8_t left = count_down16(s, RAM_BATTLE_EFFECT_ORBIT_FRAMES, k.wi);
    if (left == 0)
        inc8(s, k.done + slot);
    return left;
}

/* Is the point near the slot's target, within `limit` on both axes? The
 * point and the target go to cells A-D for the test. */
static bool near_target(Snes *s, const Slot *k, uint8_t x, uint8_t y, uint8_t limit)
{
    set_cell(s, CELL_A, x);
    set_cell(s, CELL_B, y);
    set_cell(s, CELL_C, at8(s, RAM_BATTLE_EFFECT_TARGET_X, k->i));
    set_cell(s, CELL_D, at8(s, RAM_BATTLE_EFFECT_TARGET_Y, k->i));
    set_cell(s, CELL_LIMIT, limit);
    return battle_points_close(s);
}

// $CF:F23D, $CF:F6E2
/* A jump onto the target, four substeps a frame. While falling it ends when
 * the point is near the target: the first time within 16 pixels with the
 * combatant's sprite lifted at least 24 (for an anchor within 24), after
 * that within 1. The fall's start also counts as arriving. */
static uint8_t move_jump_to_target(Snes *s, bool anchor)
{
    Slot k = slot_of(s, anchor);
    if (at8(s, RAM_BATTLE_EFFECT_JUMP_FALLING, k.wi) != 0) {
        uint8_t x = anchor ? m_read8(s, RAM_BATTLE_ANCHOR_X + k.w) : m_read8(s, object_x(k.slot));
        uint8_t y = anchor ? m_read8(s, RAM_BATTLE_ANCHOR_Y + k.w) : m_read8(s, object_y(k.slot));
        uint32_t stage = RAM_BATTLE_EFFECT_HOME_STAGE + k.i;
        if (m_read8(s, stage) != 0) {
            if (near_target(s, &k, x, y, 1)) {
                inc8(s, k.done + k.slot);
                return 1;
            }
        } else if (near_target(s, &k, x, y, anchor ? 0x18 : 0x10)) {
            uint8_t lift = anchor ? 1 : m_read8(s, RAM_BATTLE_SPRITE_OFFSET_Y + k.slot);
            if (anchor || lift >= 0xE8) {
                inc8(s, k.done + k.slot);
                inc8(s, stage);
                return lift;
            }
        }
    }
    step_position(s, &k);
    uint8_t h = (uint8_t)step_jump(s, &k, 4, true);
    bool single = at8(s, RAM_BATTLE_EFFECT_JUMP_SINGLE, k.wi);
    if (anchor) {
        set_anchor(s, RAM_BATTLE_ANCHOR_X, k.w, placed_x(s, &k));
        set_anchor(s, RAM_BATTLE_ANCHOR_Y, k.w, lifted(placed_y(s, &k), h, false, single));
        return 0;
    }
    m_write8(s, object_x(k.slot), placed_x(s, &k));
    m_write8(s, object_y(k.slot), placed_y(s, &k));
    /* a single height is written, and then the double one over it */
    if (single)
        m_write8(s, RAM_BATTLE_SPRITE_OFFSET_Y + k.slot, lifted(0, h, false, true));
    uint8_t a = lifted(0, h, false, false);
    m_write8(s, RAM_BATTLE_SPRITE_OFFSET_Y + k.slot, a);
    return a;
}

// $CF:F3BA
/* The flicker: for a count of frames, every period frames the combatant's
 * sprite offset turns off, or on at the radius and the angle the spiral last
 * used. When the count runs out the offset clears and the slot stops (the
 * combatants' slot of the same number, whichever table it runs from).
 * Returns what the original leaves in A, from `a` on the paths that don't
 * load it. */
static uint16_t flicker(Snes *s, uint16_t slot, uint16_t a)
{
    if (dec8(s, RAM_BATTLE_EFFECT_FLICKER_FRAMES + slot) == 0) {
        m_write8(s, RAM_BATTLE_SPRITE_OFFSET_X + slot, 0);
        m_write8(s, RAM_BATTLE_SPRITE_OFFSET_Y + slot, 0);
        m_write8(s, RAM_BATTLE_EFFECT_ACTIVE + slot, 0);
        return a;
    }
    if (dec8(s, RAM_BATTLE_EFFECT_FLICKER_COUNT + slot) != 0)
        return a;
    m_write8(s, RAM_BATTLE_EFFECT_FLICKER_COUNT + slot, m_read8(s, RAM_BATTLE_EFFECT_FLICKER_PERIOD + slot));
    uint8_t radius = m_read8(s, RAM_BATTLE_EFFECT_FLICKER_RADIUS + slot);
    set_radius(s, radius);
    uint8_t hidden = (uint8_t)(m_read8(s, RAM_BATTLE_EFFECT_FLICKER_HIDDEN + slot) ^ 1);
    m_write8(s, RAM_BATTLE_EFFECT_FLICKER_HIDDEN + slot, hidden);
    if (hidden) {
        m_write8(s, RAM_BATTLE_SPRITE_OFFSET_X + slot, 0);
        m_write8(s, RAM_BATTLE_SPRITE_OFFSET_Y + slot, 0);
        return hidden;
    }
    uint8_t dy, dx;
    polar(s, radius, radius, m_read8(s, RAM_BATTLE_EFFECT_ANGLE_USED + slot), &dy, &dx);
    m_write8(s, RAM_BATTLE_SPRITE_OFFSET_Y + slot, dy);
    m_write8(s, RAM_BATTLE_SPRITE_OFFSET_X + slot, dx);
    return dx;
}

// $CF:F39F
/* The flicker, and while slot 3 flickers with the mirror flag set, slot 9's
 * offset copies slot 3's. */
static uint8_t move_flicker(Snes *s)
{
    uint16_t slot = cell16(s, CELL_SLOT);
    flicker(s, slot, 0);
    uint8_t low = cell(s, CELL_SLOT);
    if (low != 3)
        return low;
    if (!m_read8(s, RAM_BATTLE_EFFECT_MIRROR_SLOT3))
        return 0;
    m_write8(s, RAM_BATTLE_SPRITE_OFFSET_X + 9, m_read8(s, RAM_BATTLE_SPRITE_OFFSET_X + 3));
    uint8_t y = m_read8(s, RAM_BATTLE_SPRITE_OFFSET_Y + 3);
    m_write8(s, RAM_BATTLE_SPRITE_OFFSET_Y + 9, y);
    return y;
}

/* Turn the chase's angle toward the target angle by up to `steps` steps
 * (a 16-bit count: 0 runs 65536), in the slot's direction; reaching it marks
 * the chase aligned, after which it follows the target angle at once. */
static uint8_t turn(Snes *s, const Slot *k, uint8_t target, uint16_t steps)
{
    uint32_t aligned = RAM_BATTLE_EFFECT_CHASE_ALIGNED + k->i;
    if (m_read8(s, aligned))
        return target;
    uint8_t angle = at8(s, RAM_BATTLE_EFFECT_CHASE_ANGLE, k->i);
    if (angle == target)
        return angle;
    int8_t dir = at8(s, RAM_BATTLE_EFFECT_CHASE_TURN, k->i) ? -1 : 1;
    do {
        angle = (uint8_t)(angle + dir);
        if (angle == target) {
            inc8(s, aligned);
            break;
        }
        steps--;
    } while (steps != 0);
    return angle;
}

// $CF:F405, $CF:F884
/* A chase: until the point is within 16 pixels of the target, turn toward it
 * (the angle from $CF:FA43), face the angle, and move the speed along it. A
 * combatant's facing is its object's; an anchor's goes to its own facing
 * table, and its move is added to its 16-bit position, sign-extended (the
 * position counts as 0 where it is negative). */
static uint8_t move_chase(Snes *s, bool anchor)
{
    Slot k = slot_of(s, anchor);
    uint32_t chase = RAM_BATTLE_CHASE_CELLS;
    /* each coordinate goes to its cell and the chase's as it is read, which
     * matters where a stray slot offset makes them overlap */
    uint8_t x = anchor ? ((m_read8(s, RAM_BATTLE_ANCHOR_X + 1 + k.w) & 0x80) ? 0 : m_read8(s, RAM_BATTLE_ANCHOR_X + k.w))
                       : m_read8(s, object_x(k.slot));
    set_cell(s, CELL_A, x);
    m_write8(s, chase + 0, x);
    uint8_t y = anchor ? ((m_read8(s, RAM_BATTLE_ANCHOR_Y + 1 + k.w) & 0x80) ? 0 : m_read8(s, RAM_BATTLE_ANCHOR_Y + k.w))
                       : m_read8(s, object_y(k.slot));
    set_cell(s, CELL_B, y);
    m_write8(s, chase + 1, y);
    uint8_t tx = at8(s, RAM_BATTLE_EFFECT_TARGET_X, k.i);
    set_cell(s, CELL_C, tx);
    m_write8(s, chase + 2, tx);
    uint8_t ty = at8(s, RAM_BATTLE_EFFECT_TARGET_Y, k.i);
    set_cell(s, CELL_D, ty);
    m_write8(s, chase + 3, ty);
    set_cell(s, CELL_LIMIT, 0x10);
    if (battle_points_close(s)) {
        inc8(s, k.done + k.slot);
        return 1;
    }
    uint8_t target = angle_to_target(s);
    uint8_t speed = at8(s, RAM_BATTLE_EFFECT_STEPS, k.i);
    set_radius(s, speed);
    uint8_t angle = turn(s, &k, target, speed);
    set_at8(s, RAM_BATTLE_EFFECT_CHASE_ANGLE, k.i, angle);
    uint8_t facing = battle_facing_for_angle(s, angle);
    if (anchor)
        m_write8(s, RAM_BATTLE_ANCHOR_FACING + k.slot, facing);
    else
        m_write8(s, RAM_BATTLE_OBJECT_FACING + k.slot, facing);
    if (!anchor) {
        uint8_t dy = battle_sine_scaled(s, angle);
        set_cell(s, CELL_A, dy);
        uint8_t dx = battle_sine_scaled(s, (uint8_t)(angle + QUARTER_TURN));
        set_cell(s, CELL_B, dx);
        m_write8(s, object_y(k.slot), (uint8_t)(m_read8(s, object_y(k.slot)) + dy));
        uint8_t nx = (uint8_t)(m_read8(s, object_x(k.slot)) + dx);
        m_write8(s, object_x(k.slot), nx);
        return nx;
    }
    /* the move goes to cells $CA-$CD, sign-extended, over the slot offset */
    set_cell16(s, CELL_SLOT_X4, 0);
    set_cell(s, CELL_B, 0);
    uint8_t dy = battle_sine_scaled(s, angle);
    set_cell(s, CELL_SLOT_X4, dy);
    if (dy & 0x80)
        set_cell(s, CELL_SLOT_X4 + 1, 0xFF);
    uint8_t dx = battle_sine_scaled(s, (uint8_t)(angle + QUARTER_TURN));
    set_cell(s, CELL_A, dx);
    if (dx & 0x80)
        set_cell(s, CELL_B, 0xFF);
    m_write16(s, RAM_BATTLE_ANCHOR_Y + k.w, (uint16_t)(m_read16(s, RAM_BATTLE_ANCHOR_Y + k.w) + cell16(s, CELL_SLOT_X4)));
    m_write16(s, RAM_BATTLE_ANCHOR_X + k.w, (uint16_t)(m_read16(s, RAM_BATTLE_ANCHOR_X + k.w) + cell16(s, CELL_A)));
    return 0;
}

/* ---- The loop ------------------------------------------------------------------------- */

typedef uint8_t (*Handler)(Snes *s);

static uint8_t combatant_line(Snes *s) { return move_line(s, false); }
static uint8_t combatant_jump(Snes *s) { return move_jump(s, false); }
static uint8_t combatant_spiral(Snes *s) { return move_spiral(s, false); }
static uint8_t combatant_orbit(Snes *s) { return move_orbit(s, false, false); }
static uint8_t combatant_jump_to_target(Snes *s) { return move_jump_to_target(s, false); }
static uint8_t combatant_orbit_flat(Snes *s) { return move_orbit(s, false, true); }
static uint8_t combatant_chase(Snes *s) { return move_chase(s, false); }
static uint8_t anchor_line(Snes *s) { return move_line(s, true); }
static uint8_t anchor_jump(Snes *s) { return move_jump(s, true); }
static uint8_t anchor_spiral(Snes *s) { return move_spiral(s, true); }
static uint8_t anchor_orbit(Snes *s) { return move_orbit(s, true, false); }
static uint8_t anchor_jump_to_target(Snes *s) { return move_jump_to_target(s, true); }
static uint8_t anchor_chase(Snes *s) { return move_chase(s, true); }

/* The handlers by their address in the two tables. */
static const struct {
    uint16_t addr;
    Handler fn;
} handlers[] = {
    { 0xF03E, combatant_line },   { 0xF087, combatant_jump },      { 0xF194, combatant_spiral },
    { 0xF1F8, combatant_orbit },  { 0xF23D, combatant_jump_to_target }, { 0xF354, combatant_orbit_flat },
    { 0xF39F, move_flicker },     { 0xF405, combatant_chase },     { 0xF4B2, anchor_line },
    { 0xF503, anchor_jump },      { 0xF61F, anchor_spiral },       { 0xF68D, anchor_orbit },
    { 0xF6E2, anchor_jump_to_target }, { 0xF7FC, move_orbit_facing }, { 0xF884, anchor_chase },
};

static Handler handler_at(uint16_t addr)
{
    for (size_t i = 0; i < sizeof handlers / sizeof handlers[0]; i++)
        if (handlers[i].addr == addr)
            return handlers[i].fn;
    return NULL;
}

/* One of the two loops: from `last` down to 0, count an active slot's delay
 * down and, when it runs out, reload it and run the handler for its type.
 * `first` is the table's first slot in the 19-slot arrays, `table` the
 * handler table, `site` its JSR. Returns false when a handler was called out
 * to and didn't come back as the loop needs; `a` tracks what the original
 * leaves in A. */
static bool run_slots(Snes *s, uint16_t last, uint16_t first, uint16_t table, uint32_t site, uint16_t *a)
{
    set_cell16(s, CELL_SLOT, last);
    do {
        uint16_t slot = cell16(s, CELL_SLOT);
        uint32_t i = first + (uint32_t)slot;
        uint8_t active = m_read8(s, RAM_BATTLE_EFFECT_ACTIVE + i);
        *a = (uint16_t)((*a & 0xFF00) | active);
        if (active && dec8(s, RAM_BATTLE_EFFECT_DELAY + i) == 0) {
            m_write8(s, RAM_BATTLE_EFFECT_DELAY + i, m_read8(s, RAM_BATTLE_EFFECT_PERIOD + i));
            set_cell16(s, CELL_SLOT_X2, (uint16_t)(slot << 1));
            set_cell16(s, CELL_SLOT_X4, (uint16_t)(slot << 2));
            uint8_t type_x2 = (uint8_t)(m_read8(s, RAM_BATTLE_EFFECT_TYPE + i) << 1);
            uint16_t target = battle_effect_handler(s, table, type_x2);
            Handler fn = handler_at(target);
            if (fn) {
                *a = fn(s);
            } else {
                /* the registers and flags as ASL : TAX leave them */
                m_set_flag(s, CPU_FLAG_C, (m_read8(s, RAM_BATTLE_EFFECT_TYPE + i) & 0x80) != 0);
                m_set_nz8(s, type_x2);
                s->cpu.a = type_x2;
                s->cpu.x = type_x2;
                s->cpu.p |= CPU_FLAG_M;
                s->cpu.p &= (uint8_t)~CPU_FLAG_X;
                m_jsr(s, site, 0xCF0000 | target);
                if (s->cpu.e || (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
                    return false;
                *a = s->cpu.a;
            }
        }
        set_cell(s, CELL_SLOT, (uint8_t)(cell(s, CELL_SLOT) - 1));
    } while (!(cell(s, CELL_SLOT) & 0x80));
    return true;
}

// $CF:F978
/* Up to eight enemies moving in circles (slots 0-7, the enemies' battle
 * slots 3-10): every period frames the radius grows by the step (the orbit
 * ends when it reaches 8), and the object sits at the center plus the radius
 * along its angle. Returns what the original leaves in A. */
static uint16_t step_orbits(Snes *s, uint16_t a)
{
    set_cell16(s, CELL_SLOT, 7);
    do {
        uint16_t i = cell16(s, CELL_SLOT);
        uint8_t on = m_read8(s, RAM_BATTLE_ORBIT_ACTIVE + i);
        a = (uint16_t)((a & 0xFF00) | on);
        if (on && dec8(s, RAM_BATTLE_ORBIT_COUNTDOWN + i) == 0) {
            m_write8(s, RAM_BATTLE_ORBIT_COUNTDOWN + i, m_read8(s, RAM_BATTLE_ORBIT_PERIOD + i));
            uint8_t radius = (uint8_t)(m_read8(s, RAM_BATTLE_ORBIT_RADIUS + i) + m_read8(s, RAM_BATTLE_ORBIT_RADIUS_STEP + i));
            m_write8(s, RAM_BATTLE_ORBIT_RADIUS + i, radius);
            set_radius(s, radius);
            if (radius == 8) {
                m_write8(s, RAM_BATTLE_ORBIT_ACTIVE + i, 0);
                m_write8(s, RAM_BATTLE_ORBIT_RADIUS + i, 0);
            }
            uint8_t angle = m_read8(s, RAM_BATTLE_ORBIT_ANGLE + i);
            uint8_t dy = battle_sine_scaled(s, angle);
            set_cell(s, CELL_SLOT_X2, dy);
            uint8_t dx = battle_sine_scaled(s, (uint8_t)(angle + QUARTER_TURN));
            set_cell(s, CELL_SLOT_X2 + 1, dx);
            m_write8(s, object_y(3 + i), (uint8_t)(m_read8(s, RAM_BATTLE_ORBIT_CENTER_Y + i) + dy));
            uint8_t x = (uint8_t)(m_read8(s, RAM_BATTLE_ORBIT_CENTER_X + i) + dx);
            m_write8(s, object_x(3 + i), x);
            a = x;
        }
        set_cell(s, CELL_SLOT, (uint8_t)(cell(s, CELL_SLOT) - 1));
    } while (!(cell(s, CELL_SLOT) & 0x80));
    return a;
}

// $CF:EFAF
/* Step the timed effect slots: while effects run (battle_effects_running),
 * the combatants' eleven and the anchors' eight; otherwise, unless orbits are
 * held off ($A0FD), the enemies' orbits. */
void battle_step_effect_slots(Snes *s)
{
    uint16_t a = s->cpu.a;
    if (!m_read8(s, RAM_BATTLE_EFFECTS_RUNNING)) {
        uint8_t hold = m_read8(s, RAM_BATTLE_ORBITS_HELD);
        a = hold ? (uint16_t)((a & 0xFF00) | hold) : step_orbits(s, a & 0xFF00);
    } else {
        a = (uint16_t)((a & 0xFF00) | m_read8(s, RAM_BATTLE_EFFECTS_RUNNING));
        if (!run_slots(s, COMBATANT_SLOTS - 1, 0, 0xF01E, 0xCFEFE6, &a) ||
            !run_slots(s, ANCHOR_SLOTS - 1, FIRST_ANCHOR, 0xF02E, 0xCFF016, &a))
            return;
    }
    s->cpu.a = a;
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

void battle_step_orbits(Snes *s)
{
    s->cpu.a = step_orbits(s, s->cpu.a);
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

// $CF:F9D3
/* Place the enemies of the formation around the leader enemy's object
 * ($A002, a battle slot less 3), each at its own offset. */
void battle_place_formation(Snes *s)
{
    uint8_t leader = m_read8(s, RAM_BATTLE_FORMATION_LEADER);
    uint16_t lead = (uint16_t)((s->cpu.a & 0xFF00) | leader);
    for (int i = 7; i >= 0; i--) {
        if (!m_read8(s, RAM_BATTLE_SLOT_PRESENT + 3 + (unsigned)i))
            continue;
        m_write8(s, object_x(3 + (unsigned)i), (uint8_t)(m_read8(s, object_x(3) + lead) + m_read8(s, RAM_BATTLE_FORMATION_OFFSET_X + (unsigned)i)));
        uint8_t y = (uint8_t)(m_read8(s, object_y(3) + lead) + m_read8(s, RAM_BATTLE_FORMATION_OFFSET_Y + (unsigned)i));
        m_write8(s, object_y(3 + (unsigned)i), y);
    }
    s->cpu.x = 0xFFFF;
    s->cpu.y = lead;
    m_rts(s);
}

bool battle_effect_page(Snes *s) { return s->cpu.d == 0x0000 && s->cpu.db == 0x7E; }

/* ---- The handlers as entries of their own ------------------------------------------- *
 *
 * Guarded on A's high byte being 0, as the loop leaves it (the handlers pass
 * it on through their TAX and 16-bit loads). */

bool battle_effect_handler_ready(Snes *s) { return battle_effect_page(s) && (s->cpu.a & 0xFF00) == 0; }

static void return_a(Snes *s, uint8_t a)
{
    s->cpu.a = a;
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

/* A handler's return: 8-bit A, whose low byte nothing reads after (DC_AL) and
 * whose high byte is still the 0 it was entered with. */
static void return_from_handler(Snes *s)
{
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

void battle_effect_combatant_line(Snes *s)
{
    move_line(s, false);
    return_from_handler(s);
}

void battle_effect_combatant_jump(Snes *s)
{
    move_jump(s, false);
    return_from_handler(s);
}

void battle_effect_combatant_spiral(Snes *s)
{
    move_spiral(s, false);
    return_from_handler(s);
}

void battle_effect_combatant_orbit(Snes *s)
{
    move_orbit(s, false, false);
    return_from_handler(s);
}

void battle_effect_combatant_jump_to_target(Snes *s)
{
    move_jump_to_target(s, false);
    return_from_handler(s);
}

void battle_effect_combatant_orbit_flat(Snes *s)
{
    move_orbit(s, false, true);
    return_from_handler(s);
}

void battle_effect_flicker(Snes *s)
{
    move_flicker(s);
    return_from_handler(s);
}

void battle_effect_combatant_chase(Snes *s)
{
    move_chase(s, false);
    return_from_handler(s);
}

void battle_effect_anchor_line(Snes *s)
{
    move_line(s, true);
    return_from_handler(s);
}

void battle_effect_anchor_jump(Snes *s)
{
    move_jump(s, true);
    return_from_handler(s);
}

void battle_effect_anchor_spiral(Snes *s)
{
    move_spiral(s, true);
    return_from_handler(s);
}

void battle_effect_anchor_orbit(Snes *s)
{
    move_orbit(s, true, false);
    return_from_handler(s);
}

void battle_effect_anchor_jump_to_target(Snes *s)
{
    move_jump_to_target(s, true);
    return_from_handler(s);
}

void battle_effect_anchor_orbit_facing(Snes *s)
{
    move_orbit_facing(s);
    return_from_handler(s);
}

void battle_effect_anchor_chase(Snes *s)
{
    move_chase(s, true);
    return_from_handler(s);
}

void battle_effect_flicker_step(Snes *s)
{
    s->cpu.a = flicker(s, cell16(s, CELL_SLOT), s->cpu.a);
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

void battle_sine_scale(Snes *s)
{
    uint8_t v = battle_sine_scaled(s, s->cpu.a);
    m_set_nz8(s, v);
    return_a(s, v);
}

void battle_multiply_scale(Snes *s)
{
    uint8_t v = battle_multiply_scaled(s, m_read8(s, RAM_BATTLE_LOW_SCRATCH), m_read16(s, RAM_BATTLE_LOW_SCRATCH + 2));
    m_set_nz8(s, v);
    return_a(s, v);
}

void battle_chase_angle(Snes *s)
{
    return_a(s, angle_to_target(s));
}
