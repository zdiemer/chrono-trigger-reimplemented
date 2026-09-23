/* Objects calling on each other: how one object's script makes another do
 * something -- a guard turn to face the party, a door open, a character join
 * a cutscene.
 *
 * Every object's script starts with a table of 16 functions, the places in
 * its script where each begins. A call names a target object, one of its
 * functions and a priority level (0 the most urgent, 7 the least). An object
 * runs at a level, and keeps a place in its script for every level it has
 * left something unfinished at:
 *
 * - a call more urgent than what the target is doing interrupts it: the
 *   target keeps its place at its level and starts the function;
 * - a less urgent call waits its turn: the function becomes the target's
 *   place at that level, unless something already waits there;
 * - a function that returns clears its level's place and the object resumes
 *   the most urgent unfinished thing below it.
 *
 * A caller can just call, wait until the target takes the call, or wait until
 * the function has run. Objects can refuse calls for a while (a script that
 * mustn't be interrupted), and disabled objects are skipped. */
#include "cpu_io.h"
#include "event/leaves.h"
#include "event/script.h"

enum { LOWEST_LEVEL = 7, FUNCTIONS = 16 };

/* ---- What the field keeps of an object, that calls look at ------------------------------
 *
 * The field's (game/field/), until its object module lands. */

enum { DISABLED = 0x80 };        /* in the object's state, and in its step length: calls skip it */
enum { NO_SPRITE_SHOWN = 0 };

static bool calls_skip(const GameState *g, EventObject object)
{
    return (field_object_state_state(g, object) & DISABLED) || (field_object_step_length(g, object) & DISABLED);
}

static void set_calls_skip(GameState *g, EventObject object, bool skip)
{
    uint8_t length = field_object_step_length(g, object);
    set_field_object_step_length(g, object, skip ? length | DISABLED : length & (uint8_t)~DISABLED);
}

/* An object leaves the map: disabled, and its sprite gone. */
static void take_off_the_map(GameState *g, EventObject object)
{
    set_field_object_state_state(g, object, DISABLED);
    set_field_object_sprite_flag(g, object, NO_SPRITE_SHOWN);
}

/* An interrupted object forgets the move it was making. */
static void stop_moving(GameState *g, EventObject object)
{
    set_field_object_counted_move(g, object, 0);
    set_field_object_steps_left(g, object, 0);
}

/* ---- Levels and places ------------------------------------------------------------------ */

static uint8_t level_of(uint8_t call) { return call >> 4; }
static uint8_t function_of(uint8_t call) { return call & 0x0F; }

static bool refusing_calls(const GameState *g, EventObject object)
{
    return event_object_call_state_refusing(g, object) != 0;
}

/* The places, a level at a time and an object's a word in each: the place of
 * an object at a level is entry level * 64 + object of event_saved_positions.
 * The original reaches them by a 16-bit byte offset, which wraps: a search
 * that runs past the last level comes round to the first. */
enum { OBJECTS_PER_LEVEL = 64, PLACES = 0x8000, BYTES_PER_LEVEL = OBJECTS_PER_LEVEL * 2 };

typedef uint16_t Place;   /* an entry of event_saved_positions */

/* What the original leaves as it finds a place: the multiplier holding the
 * level's offset (it multiplies in hardware), the offset in a scratch cell,
 * and the V of the addition. */
static void leave_place_found(GameState *g, EventObject object, uint8_t level)
{
    uint16_t row = (uint16_t)(level * BYTES_PER_LEVEL), column = (uint16_t)(object * 2);
    hw_set_multiplicand(g, level);
    hw_multiply_by(g, BYTES_PER_LEVEL);
    keep_word(g, KEPT_FIFTH, (uint16_t)(row + column));
    leave_overflow_of_adding16(row, column);
}

static Place place_of(GameState *g, EventObject object, uint8_t level)
{
    ORIGINAL_EFFECT(leave_place_found(g, object, level));
    return (Place)(level * OBJECTS_PER_LEVEL + object);
}

/* The place the next level down keeps. */
static Place place_below(Place p) { return (Place)((p + OBJECTS_PER_LEVEL) % PLACES); }

static uint16_t place(const GameState *g, Place p) { return event_saved_positions(g, p); }
static void set_place(GameState *g, Place p, uint16_t at) { set_event_saved_positions(g, p, at); }

/* Where one of an object's functions starts: the table at the head of the
 * script holds 16 offsets for each object. */
static uint16_t function_start(GameState *g, EventObject object, uint8_t function)
{
    uint16_t table = (uint16_t)(object * FUNCTIONS * 2), entry = (uint16_t)(function * 2);
    ORIGINAL_EFFECT(keep_word(g, KEPT_SIXTH, table));
    ORIGINAL_EFFECT(leave_overflow_of_adding16(entry, table));
    return script_word(g, (uint16_t)(table + entry));
}

/* ---- Making a call ------------------------------------------------------------------------ */

typedef enum { TAKES_CALLS, REFUSES_CALLS, SKIPPED } Answer;

/* What the original keeps of a call: the target, then the target's level,
 * the call and the level it asks for. */
static void keep_target(GameState *g, EventObject target) { keep_word(g, KEPT_SECOND, (uint16_t)(target * 2)); }

static void keep_levels(GameState *g, uint8_t current, uint8_t call)
{
    keep_byte(g, KEPT_FIRST, current);
    keep_byte(g, KEPT_THIRD, call);
    keep_byte(g, KEPT_FOURTH, level_of(call));
}

static Answer answer(GameState *g, EventObject target)
{
    ORIGINAL_EFFECT(keep_target(g, target));
    if (refusing_calls(g, target))
        return REFUSES_CALLS;
    return calls_skip(g, target) ? SKIPPED : TAKES_CALLS;
}

/* How urgent the call is against what the target is doing: below 0 more,
 * 0 as, above 0 less. */
static int urgency(GameState *g, EventObject target, uint8_t call)
{
    uint8_t current = event_object_call_state_level(g, target);
    ORIGINAL_EFFECT(keep_levels(g, current, call));
    return level_of(call) < current ? -1 : level_of(call) > current;
}

/* The target drops what it is doing for the function. */
static void interrupt(GameState *g, EventObject target, uint8_t call)
{
    Place own = place_of(g, target, event_object_call_state_level(g, target));
    set_place(g, own, event_object_script_position(g, target));
    set_event_object_script_position(g, target, function_start(g, target, function_of(call)));
    set_event_object_call_state_level(g, target, level_of(call));
    stop_moving(g, target);
}

// $C0:603E
/* The function waits at its level, unless something already does. */
static void queue(GameState *g, EventObject target, uint8_t call)
{
    Place p = place_of(g, target, level_of(call));
    if (place(g, p) == 0)
        set_place(g, p, function_start(g, target, function_of(call)));
}

// $C0:5FBE
/* Call a function of another object and go on: it interrupts the target or
 * waits its turn, unless the target refuses calls or is skipped. A call to a
 * party member who isn't there is no call. */
static EventFlow call_function(GameState *g, EventObject target, bool there, uint8_t call)
{
    if (!there || answer(g, target) != TAKES_CALLS)
        return next_command();
    int order = urgency(g, target, call);
    if (order > 0)
        queue(g, target, call);
    else if (order < 0)
        interrupt(g, target, call);
    return next_command();
}

/* Whether the target takes a call: it doesn't if it isn't there (a party
 * member's place empty) or is skipped, not yet if it refuses calls or the
 * call is no more urgent than what it is doing; otherwise it drops what it is
 * doing for the function. */
typedef enum { NO_CALL, NOT_YET, TAKEN } Taking;

static Taking take_the_call(GameState *g, EventObject target, bool there, uint8_t call)
{
    if (!there) {
        ORIGINAL_EFFECT(leave_parse_at(3));
        return NO_CALL;
    }
    switch (answer(g, target)) {
    case REFUSES_CALLS: return NOT_YET;
    case SKIPPED: return NO_CALL;
    default: break;
    }
    if (urgency(g, target, call) >= 0)
        return NOT_YET;
    interrupt(g, target, call);
    return TAKEN;
}

// $C0:6086
/* Call, waiting until the target takes the call. */
static EventFlow call_when_taken(GameState *g, EventObject target, bool there, uint8_t call)
{
    return take_the_call(g, target, there, call) == NOT_YET ? stay() : next_command();
}

static bool waiting(const GameState *g, EventObject self) { return event_call_waiting(g, self) != 0; }
static void set_waiting(GameState *g, EventObject self, bool on) { set_event_call_waiting(g, self, on); }

// $C0:6109
/* Call and wait until the function has run: the caller waits for the target
 * to take the call, then marks itself waiting and stays until the target has
 * come back down from the call's level (or been skipped). */
static EventFlow call_and_wait(GameState *g, EventObject self, EventObject target, bool there, uint8_t call)
{
    if (waiting(g, self)) {
        ORIGINAL_EFFECT(keep_target(g, target));
        if (!calls_skip(g, target) && urgency(g, target, call) >= 0)
            return stay();
        set_waiting(g, self, false);
        return next_command();
    }
    switch (take_the_call(g, target, there, call)) {
    case NO_CALL: return next_command();
    case NOT_YET: return stay();
    default: set_waiting(g, self, true); return stay();
    }
}

// $C0:5F74
/* A function ends: the object clears the place of the level it ran at and
 * resumes the most urgent unfinished thing below it. At the lowest level
 * there is nothing below, and the script stays here. (The guard has checked
 * that there is something to resume before the search would come round.) */
static EventFlow return_from_function(GameState *g, EventObject self)
{
    uint8_t level = event_object_call_state_level(g, self);
    if (level == LOWEST_LEVEL)
        return stay();
    ORIGINAL_EFFECT(hw_set_multiplicand(g, level));
    ORIGINAL_EFFECT(hw_multiply_by(g, BYTES_PER_LEVEL));   /* the original multiplies in hardware */
    Place p = (Place)(level * OBJECTS_PER_LEVEL + self);
    set_place(g, p, 0);
    uint16_t resume;
    do {
        ORIGINAL_EFFECT(leave_overflow_of_adding16((uint16_t)(p * 2), BYTES_PER_LEVEL));
        p = place_below(p);
        level++;
        resume = place(g, p);
    } while (resume == 0);
    set_event_object_call_state_level(g, self, level);
    return go_to(resume);
}

// $C0:6240
/* The object stops, or starts again, taking calls. */
static EventFlow take_calls(GameState *g, EventObject self, bool takes)
{
    set_event_object_call_state_refusing(g, self, !takes);
    return next_command();
}

// $C0:6254
/* Take an object off the map: disabled, and its sprite gone. */
static EventFlow disable_object(GameState *g, EventObject object)
{
    take_off_the_map(g, object);
    return next_command();
}

// $C0:6269
/* Calls skip an object from now on; if that is the object running, its turn ends. */
static EventFlow make_unreachable(GameState *g, EventObject self, EventObject object)
{
    set_calls_skip(g, object, true);
    return object == self ? end_turn() : next_command();
}

// $C0:6282
/* Calls reach an object again. */
static EventFlow make_reachable(GameState *g, EventObject object)
{
    set_calls_skip(g, object, false);
    return next_command();
}

/* The finding the guard makes for a return: counting up from the object's
 * level, a place with something to resume turns up before the search comes
 * round to where it started (the original never stops otherwise). */
static bool finds_a_level(const GameState *g, uint16_t at)
{
    (void)at;
    EventObject self = running_object(g);
    uint8_t level = event_object_call_state_level(g, self);
    if (level == LOWEST_LEVEL)
        return true;
    Place p = (Place)(level * OBJECTS_PER_LEVEL + self);
    for (unsigned k = 1; k < PLACES / OBJECTS_PER_LEVEL; k++)
        if (place(g, p = place_below(p)) != 0)
            return true;
    return false;
}

static EventFlow run_call(GameState *g, const EventArgs *a) { return call_function(g, a->arg[0], a->arg[1], a->arg[2]); }
static EventFlow run_call_when_taken(GameState *g, const EventArgs *a)
{
    return call_when_taken(g, a->arg[0], a->arg[1], a->arg[2]);
}
static EventFlow run_call_and_wait(GameState *g, const EventArgs *a)
{
    return call_and_wait(g, a->self, a->arg[0], a->arg[1], a->arg[2]);
}
static EventFlow run_return(GameState *g, const EventArgs *a) { return return_from_function(g, a->self); }
static EventFlow run_take_calls(GameState *g, const EventArgs *a) { return take_calls(g, a->self, a->arg[0]); }
static EventFlow run_disable_object(GameState *g, const EventArgs *a) { return disable_object(g, a->arg[0]); }
static EventFlow run_make_unreachable(GameState *g, const EventArgs *a)
{
    return make_unreachable(g, a->self, a->arg[0]);
}
static EventFlow run_make_reachable(GameState *g, const EventArgs *a) { return make_reachable(g, a->arg[0]); }

#define THERE CONSTANT(true)

/* A call's target is an object, or a party member, who may not be there. */
EVENT_OPCODES(event_calls_opcodes,
    { 0x02, run_call, { OBJECT(1), THERE, BYTE(2) }, 3, .routine = 0xC05FB6 },
    { 0x05, run_call, { MEMBER_OBJECT(1), MEMBER_PRESENT(1), BYTE(2) }, 3, .member_may_be_absent = true,
      .routine = 0xC061E9 },
    { 0x03, run_call_when_taken, { OBJECT(1), THERE, BYTE(2) }, 3, .routine = 0xC0607E },
    { 0x06, run_call_when_taken, { MEMBER_OBJECT(1), MEMBER_PRESENT(1), BYTE(2) }, 3,
      .member_may_be_absent = true, .routine = 0xC061FE },
    { 0x04, run_call_and_wait, { OBJECT(1), THERE, BYTE(2) }, 3, .routine = 0xC06109 },
    /* the wait goes on with the member's place even when it is empty, so the
     * marker there must name an object as the original indexes it */
    { 0x07, run_call_and_wait, { MEMBER_OBJECT(1), MEMBER_PRESENT(1), BYTE(2) }, 3, .routine = 0xC06214 },
    { 0x00, run_return, { { 0 } }, 1, .accepts = finds_a_level, .routine = 0xC05F74 },
    { 0x08, run_take_calls, { CONSTANT(false) }, 1, .routine = 0xC06240 },
    { 0x09, run_take_calls, { CONSTANT(true) }, 1, .routine = 0xC0624B },
    { 0x0A, run_disable_object, { OBJECT(1) }, 2, .routine = 0xC06254 },
    { 0x0B, run_make_unreachable, { OBJECT(1) }, 2, .routine = 0xC06269 },
    { 0x0C, run_make_reachable, { OBJECT(1) }, 2, .routine = 0xC06282 });
