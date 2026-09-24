/* Battle: the objects' screen bounds, and where their animations stand.
 *
 * Each combatant's object has a box on the screen around its position,
 * half its width to either side and half its height up, and 8 pixels down
 * from it (nothing down when the party stands off the battlefield's grid),
 * clamped to the screen's edges; the targeting and the effects' paths go by
 * it. The battle keeps the position the box was worked out from beside it,
 * since an effect may work it out for a position the object is only moving
 * to.
 *
 * The animation scheduler plays scripted animations on the objects (a tech's
 * swing, a spell's cast) a frame's time at a time: each frame every object's
 * time counts down, and the scheduler takes the next object due, round the
 * objects from where it last stopped. And six animation slots can have their
 * position worked out each frame by a position mode, whose code is still the
 * original's. */
#include "battle/animation.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "original_effects.h"
#include "vars.h"

/* ---- Screen bounds ---------------------------------------------------------------------- */

/* The screen's edges in the bounds' 8-bit coordinates. A position in the
 * screen's left (top) half can't have a bound past its left (top) edge, and
 * one in its right (bottom) half can't have one past its right (bottom) edge;
 * the other way round, a bound wraps as the byte does. */
enum { SCREEN_NEAR_EDGE = 0x00, SCREEN_FAR_EDGE = 0xFF, SCREEN_HALF = 0x80 };

static bool in_far_half(uint8_t v) { return (v & SCREEN_HALF) != 0; }

static bool past_near_edge(uint8_t at, uint8_t half_size) { return !in_far_half(at) && in_far_half((uint8_t)(at - half_size)); }

static uint8_t bound_before(uint8_t at, uint8_t half_size)
{
    return past_near_edge(at, half_size) ? (uint8_t)SCREEN_NEAR_EDGE : (uint8_t)(at - half_size);
}

static uint8_t bound_after(uint8_t at, uint8_t half_size)
{
    uint8_t bound = (uint8_t)(at + half_size);
    return in_far_half(at) && !in_far_half(bound) ? (uint8_t)SCREEN_FAR_EDGE : bound;
}

/* The party stands off the battlefield's grid (battle_setup_flags). */
enum { PARTY_OFF_GRID = 0x04, BOX_BELOW = 8 };

// $C1:285A
/* Work out the object's box from the position it was last given. */
static void update_bounds(GameState *g, unsigned slot)
{
    uint8_t x = battle_bounds_x(g, slot), y = battle_bounds_y(g, slot);
    set_battle_bounds_left(g, slot, bound_before(x, battle_object_half_width(g, slot)));
    set_battle_bounds_right(g, slot, bound_after(x, battle_object_half_width(g, slot)));
    set_battle_bounds_top(g, slot, bound_before(y, battle_object_half_height(g, slot)));
    bool below = !(battle_setup_flags(g) & PARTY_OFF_GRID);
    set_battle_bounds_bottom(g, slot, below ? (uint8_t)(y + BOX_BELOW) : y);
}

// $C1:283D
/* Work out every combatant's box from where it stands now. */
static void update_all_bounds(GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        if (!battle_slot_present(g, slot))
            continue;
        set_battle_bounds_x(g, slot, battle_object_x(g, battle_combatant_object(slot)));
        set_battle_bounds_y(g, slot, battle_object_y(g, battle_combatant_object(slot)));
        update_bounds(g, slot);
    }
}

/* ---- The animation scheduler ------------------------------------------------------------ */

// $C1:423A
/* Count every object's time down a frame, and find the next object due: one
 * with an animation playing whose time is up, looking round the objects from
 * the scheduler's place. BATTLE_COMBATANTS if there is none. */
static unsigned find_due_animation(GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        uint8_t left = battle_animation_delay(g, slot);
        if (left != 0)
            set_battle_animation_delay(g, slot, (uint8_t)(left - 1));
    }
    unsigned slot = battle_animation_cursor(g);
    for (unsigned n = 0; n < BATTLE_COMBATANTS; n++) {
        if (battle_animating(g, slot) && battle_animation_delay(g, slot) == 0)
            return slot;
        if (++slot == BATTLE_COMBATANTS)
            slot = 0;
    }
    return BATTLE_COMBATANTS;
}

/* ---- The animation slots' positions ------------------------------------------------------- */

enum { POSITION_SLOTS = 6, NO_POSITION_MODE = 0 };

/* The slot's position mode, less the "none" that 0 is. */
static bool has_position_mode(const GameState *g, unsigned slot)
{
    return battle_animation_position_modes(g, slot) != NO_POSITION_MODE;
}
static uint8_t position_mode(const GameState *g, unsigned slot)
{
    return (uint8_t)(battle_animation_position_modes(g, slot) - 1);
}

/* Keep the position the slot's mode worked out. */
static void keep_found_position(GameState *g, unsigned slot)
{
    set_battle_animation_positions(g, slot, (uint16_t)(battle_position_found_x(g) | battle_position_found_y(g) << 8));
}

/* The original keeps the slot it is at in a cell; the modes' code doesn't
 * read it. */
enum { LEFT_POSITION_SLOT = 0x10 };
static void leave_position_slot(GameState *g, unsigned slot)
{
    set_battle_work(g, LEFT_POSITION_SLOT, (uint8_t)slot);
    set_battle_work(g, LEFT_POSITION_SLOT + 1, 0);
}

/* ---- The entries ------------------------------------------------------------------------ */

/* Each takes the object as the original's callers give it, in X. The box's
 * last sum leaves the carry for the callers: the bottom's, or the top's
 * when the box has none below; and a bound clamped at the near edge leaves A's
 * high byte clear (the original clamps with its direct page, 0). */
static bool bounds_carry(const GameState *g, unsigned slot)
{
    uint8_t y = battle_bounds_y(g, slot);
    if (!(battle_setup_flags(g) & PARTY_OFF_GRID))
        return y + BOX_BELOW > 0xFF;
    return y >= battle_object_half_height(g, slot);
}

void battle_update_object_bounds(Snes *s)
{
    GameState *g = state_of(s);
    unsigned slot = s->cpu.x;
    bool clamped = past_near_edge(battle_bounds_x(g, slot), battle_object_half_width(g, slot))
                   || past_near_edge(battle_bounds_y(g, slot), battle_object_half_height(g, slot));
    update_bounds(g, slot);
    if (clamped)
        s->cpu.a = 0;
    m_set_flag(s, CPU_FLAG_C, bounds_carry(g, slot));
    m_rts(s);
}

/* It leaves A's high byte clear and the carry the compare that ends its loop
 * leaves. */
void battle_update_all_object_bounds(Snes *s)
{
    update_all_bounds(state_of(s));
    s->cpu.a = 0;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

bool battle_names_bounds_object(Snes *s) { return battle_on_page(s) && battle_names_combatant(s->cpu.x); }

/* The scheduler goes on with the object due at $C1:4269, with it in X; with
 * none due it returns. */
enum { TAKE_ANIMATION_STEP = 0xC14269 };

void battle_find_due_animation(Snes *s)
{
    unsigned slot = find_due_animation(state_of(s));
    s->cpu.a = 0;
    if (slot == BATTLE_COMBATANTS) {
        m_rts(s);
        return;
    }
    s->cpu.x = (uint16_t)slot;
    m_stop_at_own(s, TAKE_ANIMATION_STEP, DC_Y | DC_N | DC_V | DC_Z | DC_C | DC_AL);
}

bool battle_scheduler_in_place(Snes *s)
{
    vars_peek(true);
    bool in_place = battle_names_combatant(battle_animation_cursor(state_of(s)));
    vars_peek(false);
    return battle_on_page(s) && in_place;
}

/* Each slot's position is worked out by its mode's code ($C1:762E, which
 * picks it by the mode in A), a call per slot. */
void battle_update_animation_positions(Snes *s)
{
    GameState *g = state_of(s);
    for (unsigned slot = 0; slot < POSITION_SLOTS; slot++) {
        ORIGINAL_EFFECT(leave_position_slot(g, slot));
        if (!has_position_mode(g, slot))
            continue;
        s->cpu.a = position_mode(g, slot);
        if (!battle_call(s, 0xC1421E, 0xC1762E, DC_X | DC_Y | DC_N | DC_Z | DC_C))
            return;
        keep_found_position(g, slot);
    }
    ORIGINAL_EFFECT(leave_position_slot(g, POSITION_SLOTS));
    m_rts(s);
}
