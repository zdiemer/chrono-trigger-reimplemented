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
 * position worked out each frame by a position mode: a place the animation
 * scripts name (battle/script_places.c). */
#include "battle/animation.h"

#include "battle/animation_data.h"
#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/script_language.h"
#include "charge.h"
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

/* The box reaches 8 pixels below the object, unless the party stands off the
 * battlefield's grid. */
enum { BOX_BELOW = 8 };

// $C1:285A
/* Work out the object's box from the position it was last given. */
void battle_update_bounds(GameState *g, unsigned slot)
{
    uint8_t x = battle_bounds_x(g, slot), y = battle_bounds_y(g, slot);
    set_battle_bounds_left(g, slot, bound_before(x, battle_object_half_width(g, slot)));
    set_battle_bounds_right(g, slot, bound_after(x, battle_object_half_width(g, slot)));
    set_battle_bounds_top(g, slot, bound_before(y, battle_object_half_height(g, slot)));
    bool below = !(battle_setup_flags(g) & BATTLE_SETUP_PARTY_OFF_GRID);
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
        battle_update_bounds(g, slot);
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

/* An object's frames are in bank $E4: its animation's list of frame times
 * and its list of frames, each facing's part of the frame list a stride after
 * the last. A time of 0 ends the animation. */
enum { OBJECT_LISTS_BANK = 0xE4 };

static uint8_t list_byte(const GameState *g, uint16_t at) { return battle_frame_list_byte(g, battle_at(OBJECT_LISTS_BANK, at)); }

static uint16_t facing_stride(const GameState *g, unsigned slot)
{
    return (uint16_t)(battle_facing_stride_low(g, slot) | battle_facing_stride_high(g, slot) << 8);
}

/* The object whose graphics the upload takes. */
static void name_upload_object(GameState *g, unsigned slot)
{
    set_battle_graphics_object(g, (uint16_t)((battle_graphics_object(g) & 0xFF00) | (uint8_t)slot));
}

/* What the original leaves in its cells: the object, and its facing's part
 * of the frame list. */
enum { LEFT_OBJECT = 0x00, LEFT_FACING_PART = 0x02 };
static void leave_step(GameState *g, unsigned slot, uint16_t facing_part)
{
    set_battle_work(g, LEFT_OBJECT, (uint8_t)slot);
    set_battle_work(g, LEFT_OBJECT + 1, 0);
    set_battle_work(g, LEFT_FACING_PART, (uint8_t)facing_part);
    set_battle_work(g, LEFT_FACING_PART + 1, (uint8_t)(facing_part >> 8));
}

static void build_graphics(GameState *g, unsigned slot, uint8_t frame)
{
    if (battle_object_builds_graphics(g, slot))
        battle_build_frame_graphics(g, slot, frame);
}

/* The frame after `frame` that has a time: a looping animation comes round
 * to its first frame past its last, one played once ends there (NO_FRAME).
 * An animation whose first frame had no time would start over for ever in
 * the original; none has one, and the port ends it. */
enum { NO_FRAME = 0x100 };

static unsigned frame_with_time(GameState *g, unsigned slot, uint8_t frame)
{
    uint16_t times = battle_scheduled_delay_lists(g, slot);
    if (list_byte(g, (uint16_t)(times + frame)) != 0)
        return frame;
    if (battle_animating(g, slot) != PLAY_LOOPING || list_byte(g, times) == 0) {
        set_battle_animating(g, slot, 0);
        return NO_FRAME;
    }
    set_battle_animation_frame(g, slot, 0);
    return 0;
}

// $C1:4269
/* The object due takes its animation's next frame: the frame's time, and
 * its graphics, built for the frame list's part for the way it faces, to be
 * uploaded at the frame after next. The scheduler goes on from the next
 * object. While the sprites are frozen, or graphics already wait for their
 * upload, the object waits for the next pass. While a script waits on the
 * object's animation it holds the frame the wait's mark names (one past it)
 * and the animation ends there. */
enum { UPLOAD_AFTER_NEXT_FRAME = 2 };

static void take_animation_step(GameState *g, unsigned slot)
{
    if (battle_sprites_frozen(g) || battle_graphics_upload_request(g))
        return;
    set_battle_animation_cursor(g, (uint8_t)((slot + 1) % BATTLE_COMBATANTS));
    uint8_t facing = battle_object_facing(g, slot);
    set_battle_object_facing_shown(g, slot, facing);
    uint16_t facing_part = (uint16_t)battle_multiply_long(g, facing, facing_stride(g, slot));
    name_upload_object(g, slot);
    ORIGINAL_EFFECT(leave_step(g, slot, facing_part));

    uint8_t frame = (uint8_t)(battle_animation_frame(g, slot) + 1);
    uint8_t held = battle_animation_wait(g, slot);
    if (held != 0) {
        frame = (uint8_t)(held - 1);
        set_battle_animating(g, slot, 0);
    }
    set_battle_animation_frame(g, slot, frame);
    unsigned next = frame_with_time(g, slot, frame);
    if (next == NO_FRAME)
        return;
    set_battle_animation_delay(g, slot, list_byte(g, (uint16_t)(battle_scheduled_delay_lists(g, slot) + next)));
    uint8_t graphics = list_byte(g, (uint16_t)(battle_scheduled_frame_lists(g, slot) + facing_part + next));
    set_battle_graphics_frame(g, graphics);
    TIMED_AS(g, 0xC11C4A, build_graphics(g, slot, graphics));
    set_battle_graphics_upload_request(g, UPLOAD_AFTER_NEXT_FRAME);
}

/* ---- The animation slots' positions ------------------------------------------------------- */

enum { POSITION_SLOTS = 6 };

/* Keep the position the slot's place worked out. */
static void keep_found_position(GameState *g, unsigned slot, BattlePoint at)
{
    set_battle_animation_positions(g, slot, (uint16_t)(at.x | at.y << 8));
}

/* The original counts the slots in a cell, which the places don't read. The
 * count it ends at, POSITION_SLOTS, is the lift the original's jumps to the
 * script's point take from there (battle/script_language.h). */
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
bool battle_bounds_carry(const GameState *g, unsigned slot)
{
    uint8_t y = battle_bounds_y(g, slot);
    if (!(battle_setup_flags(g) & BATTLE_SETUP_PARTY_OFF_GRID))
        return y + BOX_BELOW > 0xFF;
    return y >= battle_object_half_height(g, slot);
}

void battle_update_object_bounds(Snes *s)
{
    GameState *g = state_of(s);
    unsigned slot = s->cpu.x;
    bool clamped = past_near_edge(battle_bounds_x(g, slot), battle_object_half_width(g, slot))
                   || past_near_edge(battle_bounds_y(g, slot), battle_object_half_height(g, slot));
    battle_update_bounds(g, slot);
    if (clamped)
        s->cpu.a = 0;
    m_set_flag(s, CPU_FLAG_C, battle_bounds_carry(g, slot));
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

/* The scheduler's pass: the object due, if any, takes its step. It leaves A's
 * high byte clear. */
void battle_find_due_animation(Snes *s)
{
    GameState *g = state_of(s);
    unsigned slot = find_due_animation(g);
    if (slot != BATTLE_COMBATANTS)
        take_animation_step(g, slot);
    s->cpu.a = 0;
    m_rts(s);
}

/* The object the pass will find due, without counting the times down: one
 * whose time is up now or runs out this frame. */
static unsigned due_after_count(const GameState *g)
{
    unsigned slot = battle_animation_cursor(g);
    for (unsigned n = 0; n < BATTLE_COMBATANTS; n++) {
        if (battle_animating(g, slot) && battle_animation_delay(g, slot) <= 1)
            return slot;
        if (++slot == BATTLE_COMBATANTS)
            slot = 0;
    }
    return BATTLE_COMBATANTS;
}

/* The scheduler's place names an object, and the object due has graphics the
 * battle builds, as every object the game animates has. */
bool battle_scheduler_in_place(Snes *s)
{
    vars_peek(true);
    GameState *g = state_of(s);
    bool in_place = battle_names_combatant(battle_animation_cursor(g));
    unsigned due = in_place ? due_after_count(g) : BATTLE_COMBATANTS;
    bool known = due == BATTLE_COMBATANTS || !battle_object_builds_graphics(g, due) || battle_object_graphics_known(g, due);
    vars_peek(false);
    return battle_on_page(s) && in_place && known;
}

/* Each slot's position is the place its mode names (battle/script_places.c),
 * worked out afresh. A mode naming a place the game doesn't have is the
 * original's to work out ($C1:762E, which picks it by the mode in A). */
void battle_update_animation_positions(Snes *s)
{
    GameState *g = state_of(s);
    for (unsigned slot = 0; slot < POSITION_SLOTS; slot++) {
        ORIGINAL_EFFECT(leave_position_slot(g, slot));
        uint8_t place;
        if (!animation_slot_follows(g, slot, &place))
            continue;
        BattlePoint at;
        if (script_place_known(g, place)) {
            at = script_find_place(g, place);
        } else {
            s->cpu.a = place;
            if (!battle_call(s, 0xC1421E, 0xC1762E, DC_X | DC_Y | DC_N | DC_Z | DC_C))
                return;
            at = (BattlePoint){ battle_position_found_x(g), battle_position_found_y(g) };
        }
        keep_found_position(g, slot, at);
    }
    ORIGINAL_EFFECT(leave_position_slot(g, POSITION_SLOTS));
    m_rts(s);
}
