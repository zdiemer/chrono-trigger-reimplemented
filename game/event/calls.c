/* Event commands: calling functions in other objects' scripts, and returning.
 *
 * Each object's script starts with 16 function offsets (32 bytes an object,
 * at the start of the script buffer). A call names a target object and a call
 * byte: a priority level in its high nibble (lower numbers win) and a
 * function in its low nibble. An object runs at the level in its call state,
 * and keeps a saved script position for each level (event_saved_positions,
 * level * 64 + slot).
 *
 * A call at a lower level than the target's preempts it: the target's
 * position is saved for its current level and it starts the function at the
 * new level. A call at a higher level is queued: the function becomes the
 * saved position for that level, unless one is already there. A return
 * clears the position of the level it ran at and resumes at the next higher
 * level that has one.
 *
 * Calls skip targets that are disabled (their state, or their step length's
 * bit 7, negative), and skip or wait on targets that refuse calls. The
 * commands keep their operands in the scratch cells, as the original does:
 * $D9 the target's level, $DB the target, $DD the call byte, $DF the called
 * level, $E1 the saved position's index and $E3 the target's function table. */
#include "cpu_io.h"
#include "event/bridge.h"

enum { SAVED_LEVEL_STRIDE = 0x80, LOWEST_LEVEL = 7, FUNCTIONS_PER_OBJECT = 16 };

/* The target of a call, as the commands keep it (its doubled slot, a word). */
static unsigned keep_target(GameState *g, unsigned target)
{
    keep16(g, SCRATCH_DB, (uint16_t)(target * 2));
    return target;
}

typedef enum { TAKES_CALLS, REFUSES_CALLS, DISABLED } Callable;

static bool disabled(const GameState *g, unsigned target)
{
    return (field_object_state_state(g, target) & 0x80) || (field_object_step_length(g, target) & 0x80);
}

static Callable callable(GameState *g, unsigned target)
{
    keep_target(g, target);
    if (event_object_call_state_refusing(g, target))
        return REFUSES_CALLS;
    return disabled(g, target) ? DISABLED : TAKES_CALLS;
}

/* Compare the level the call byte asks for with the target's (<0 lower,
 * which wins; 0 the same; >0 higher). */
static int compare_levels(GameState *g, unsigned target, uint8_t call)
{
    uint8_t current = event_object_call_state_level(g, target);
    keep8(g, SCRATCH_D9, current);
    keep8(g, SCRATCH_DD, call);
    uint8_t level = call >> 4;
    keep8(g, SCRATCH_DF, level);
    return level < current ? -1 : level > current;
}

/* The index of the target's saved position for `level` (in bytes, as the
 * original keeps it): level * $80, by the multiplier, plus the target's
 * doubled slot. The addition's overflow is the step's. */
static uint16_t saved_position_index(GameState *g, uint8_t level, EventStep *st)
{
    hw_set_multiplicand(g, level);
    uint16_t product = hw_multiply_by(g, SAVED_LEVEL_STRIDE);
    uint16_t target = scratch16(g, SCRATCH_DB);
    uint16_t index = (uint16_t)(product + target);
    keep16(g, SCRATCH_E1, index);
    st->overflow = overflows16(product, target, index);
    return index;
}

static uint16_t saved_position(const GameState *g, uint16_t index) { return event_saved_positions(g, index / 2); }
static void set_saved_position(GameState *g, uint16_t index, uint16_t v) { set_event_saved_positions(g, index / 2, v); }

/* Where the function the call byte names starts, in the target's table. */
static uint16_t function_start(GameState *g, EventStep *st)
{
    uint16_t table = (uint16_t)((scratch16(g, SCRATCH_DB) & 0x00FF) << 4);
    keep16(g, SCRATCH_E3, table);
    uint16_t entry = (uint16_t)((scratch16(g, SCRATCH_DD) & 0x000F) << 1);
    uint16_t at = (uint16_t)(entry + table);
    st->overflow = overflows16(entry, table, at);
    return script_word(g, at);
}

/* The target leaves what it was doing for the function, at the called level. */
static void preempt(GameState *g, unsigned target, EventStep *st)
{
    uint16_t index = saved_position_index(g, scratch8(g, SCRATCH_D9), st);
    set_saved_position(g, index, event_object_script_position(g, target));
    set_event_object_script_position(g, target, function_start(g, st));
    set_event_object_call_state_level(g, target, scratch8(g, SCRATCH_DF));
    set_field_object_counted_move(g, target, 0);
    set_field_object_steps_left(g, target, 0);
}

// $C0:603E
/* The function waits at the called level, unless something already does. */
static void queue(GameState *g, EventStep *st)
{
    uint16_t index = saved_position_index(g, scratch8(g, SCRATCH_DF), st);
    if (saved_position(g, index) == 0)
        set_saved_position(g, scratch16(g, SCRATCH_E1), function_start(g, st));
}

// $C0:5FBE
/* Call without waiting: preempt the target or queue the function, unless it
 * refuses calls or is disabled. The call byte is operand 2. */
static EventStep call_now(GameState *g, const EventCommand *c, unsigned target)
{
    EventStep st = next_command(c, 3, 3);
    if (callable(g, target) != TAKES_CALLS)
        return st;
    int order = compare_levels(g, target, operand(g, c, 2));
    if (order > 0)
        queue(g, &st);
    else if (order < 0)
        preempt(g, target, &st);
    return st;
}

// $C0:5FB6
/* $02: call a function of the object in operand 1 (operand 2 the call byte). */
static EventStep call(GameState *g, const EventCommand *c) { return call_now(g, c, object_operand(g, c, 1)); }

// $C0:61E9
/* $05: as $02 for party member (operand 1); skipped if there's no such member. */
static EventStep call_party_member(GameState *g, const EventCommand *c)
{
    if (!party_has_member(g, c, 1))
        return next_command(c, 3, 3);
    return call_now(g, c, member_object(g, c, 1));
}

// $C0:6086
/* Wait until the target takes the call: until it doesn't refuse calls and the
 * call's level beats its own. A disabled target skips the command. */
static EventStep call_when_accepted(GameState *g, const EventCommand *c, unsigned target)
{
    switch (callable(g, target)) {
    case REFUSES_CALLS: return wait_here(c, 0);
    case DISABLED: return next_command(c, 3, 0);
    default: break;
    }
    if (compare_levels(g, target, operand(g, c, 2)) >= 0)
        return wait_here(c, 0);
    EventStep st = next_command(c, 3, 0);
    preempt(g, target, &st);
    return st;
}

// $C0:607E
/* $03: call a function of the object in operand 1 once it takes the call. */
static EventStep call_when_accepted_object(GameState *g, const EventCommand *c)
{
    return call_when_accepted(g, c, object_operand(g, c, 1));
}

// $C0:61FE
/* $06: as $03 for party member (operand 1). */
static EventStep call_party_member_when_accepted(GameState *g, const EventCommand *c)
{
    if (!party_has_member(g, c, 1))
        return next_command(c, 3, 3);
    return call_when_accepted(g, c, member_object(g, c, 1));
}

// $C0:6122
/* A waiting call, first part: as call_when_accepted, but once the call is
 * made the caller marks itself waiting and stays. */
static EventStep start_waiting_call(GameState *g, const EventCommand *c, unsigned target)
{
    switch (callable(g, target)) {
    case REFUSES_CALLS: return wait_here(c, 0);
    case DISABLED: return next_command(c, 3, 0);
    default: break;
    }
    if (compare_levels(g, target, operand(g, c, 2)) >= 0)
        return wait_here(c, 0);
    EventStep st = wait_here(c, 0);
    preempt(g, target, &st);
    set_event_call_waiting(g, current_object(g), 1);
    return st;
}

// $C0:61AF
/* The second part: stay until the target has dropped back below the called
 * level (or is disabled), then take the mark away and go on. */
static EventStep finish_waiting_call(GameState *g, const EventCommand *c, unsigned target)
{
    keep_target(g, target);
    if (!disabled(g, target) && compare_levels(g, target, operand(g, c, 2)) >= 0)
        return wait_here(c, 0);
    set_event_call_waiting(g, current_object(g), 0);
    return next_command(c, 3, 0);
}

static EventStep call_and_wait(GameState *g, const EventCommand *c, unsigned target)
{
    if (event_call_waiting(g, current_object(g)))
        return finish_waiting_call(g, c, target);
    return start_waiting_call(g, c, target);
}

// $C0:6109
/* $04: call a function of the object in operand 1 and wait until it has run. */
static EventStep call_object_and_wait(GameState *g, const EventCommand *c)
{
    return call_and_wait(g, c, object_operand(g, c, 1));
}

// $C0:6214
/* $07: as $04 for party member (operand 1). */
static EventStep call_party_member_and_wait(GameState *g, const EventCommand *c)
{
    if (!event_call_waiting(g, current_object(g)) && !party_has_member(g, c, 1))
        return next_command(c, 3, 3);
    return call_and_wait(g, c, member_object(g, c, 1));
}

// $C0:5F74
/* $00: return from a function: clear the saved position of the level it ran
 * at, and resume at the next higher level with one. At the lowest level,
 * wait here instead. The guard has checked that a level with one is found
 * before the search comes round to where it started. */
static EventStep return_from_function(GameState *g, const EventCommand *c)
{
    unsigned self = current_object(g);
    uint8_t level = event_object_call_state_level(g, self);
    if (level == LOWEST_LEVEL)
        return wait_here(c, 0);
    hw_set_multiplicand(g, level);
    uint16_t product = hw_multiply_by(g, SAVED_LEVEL_STRIDE);
    uint16_t index = (uint16_t)(product + field_current_object(g));
    set_saved_position(g, index, 0);
    uint16_t resume;
    bool overflow;
    do {
        uint16_t next = (uint16_t)(index + SAVED_LEVEL_STRIDE);
        overflow = overflows16(index, SAVED_LEVEL_STRIDE, next);
        index = next;
        set_event_object_call_state_level(g, self, (uint8_t)(event_object_call_state_level(g, self) + 1));
        resume = saved_position(g, index);
    } while (resume == 0);
    return with_overflow(step_at(resume, true, c->at), overflow);
}

// $C0:6240
/* $08: this object refuses calls. */
static EventStep refuse_calls(GameState *g, const EventCommand *c)
{
    set_event_object_call_state_refusing(g, current_object(g), 1);
    return next_command(c, 1, 0);
}

// $C0:624B
/* $09: this object takes calls. */
static EventStep accept_calls(GameState *g, const EventCommand *c)
{
    set_event_object_call_state_refusing(g, current_object(g), 0);
    return next_command(c, 1, 0);
}

// $C0:6254
/* $0A: disable the object in operand 1, and take its sprite away. */
static EventStep disable_object(GameState *g, const EventCommand *c)
{
    unsigned object = object_operand(g, c, 1);
    set_field_object_state_state(g, object, 0x80);
    set_field_object_sprite_flag(g, object, 0);
    return next_command(c, 2, 1);
}

// $C0:6269
/* $0B: calls skip the object in operand 1 (bit 7 of its step length); if
 * that's this object, its turn ends. */
static EventStep make_object_uncallable(GameState *g, const EventCommand *c)
{
    unsigned object = object_operand(g, c, 1);
    set_field_object_step_length(g, object, field_object_step_length(g, object) | 0x80);
    if (object * 2 == field_current_object(g))
        return end_turn(c, 2, 1);
    return next_command(c, 2, 1);
}

// $C0:6282
/* $0C: calls reach the object in operand 1 again. */
static EventStep make_object_callable(GameState *g, const EventCommand *c)
{
    unsigned object = object_operand(g, c, 1);
    set_field_object_step_length(g, object, field_object_step_length(g, object) & 0x7F);
    return next_command(c, 2, 1);
}

// $C0:6295
/* $0D: set what blocks this object's moves (operand 1: bit 0 the map's
 * blocked tiles, bit 1 other objects). */
static EventStep set_blocking(GameState *g, const EventCommand *c)
{
    set_field_object_blocking(g, current_object(g), operand(g, c, 1));
    return next_command(c, 2, 1);
}

// $C0:62A4
/* $0E: set this object's arrival flags (operand 1). */
static EventStep set_arrival_flags(GameState *g, const EventCommand *c)
{
    set_field_object_arrival_flags(g, current_object(g), operand(g, c, 1));
    return next_command(c, 2, 1);
}

EVENT_COMMAND(event_cmd_return, return_from_function)
EVENT_COMMAND(event_cmd_call, call)
EVENT_COMMAND(event_cmd_call_when_accepted, call_when_accepted_object)
EVENT_COMMAND(event_cmd_call_and_wait, call_object_and_wait)
EVENT_COMMAND(event_cmd_call_party_member, call_party_member)
EVENT_COMMAND(event_cmd_call_party_member_when_accepted, call_party_member_when_accepted)
EVENT_COMMAND(event_cmd_call_party_member_and_wait, call_party_member_and_wait)
EVENT_COMMAND(event_cmd_refuse_calls, refuse_calls)
EVENT_COMMAND(event_cmd_accept_calls, accept_calls)
EVENT_COMMAND(event_cmd_disable_object, disable_object)
EVENT_COMMAND(event_cmd_make_object_uncallable, make_object_uncallable)
EVENT_COMMAND(event_cmd_make_object_callable, make_object_callable)
EVENT_COMMAND(event_cmd_set_blocking, set_blocking)
EVENT_COMMAND(event_cmd_set_arrival_flags, set_arrival_flags)
