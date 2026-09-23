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
 * mustn't be interrupted), and unreachable objects are passed over
 * (field/object.h). */
#include "cpu_io.h"
#include "event/leaves.h"
#include "event/script.h"

enum { LOWEST_LEVEL = 7, FUNCTIONS = 16 };

static bool refusing_calls(const GameState *g, FieldObject object)
{
    return event_object_call_state_refusing(g, object) != 0;
}

/* ---- Levels and places ------------------------------------------------------------------ */

/* The places, a level at a time and an object's a word in each: the place of
 * an object at a level is entry level * 64 + object of event_saved_positions.
 * The original reaches them by a 16-bit byte offset, which wraps: a search
 * that runs past the last level comes round to the first. */
enum { OBJECTS_PER_LEVEL = 64, PLACES = 0x8000, BYTES_PER_LEVEL = OBJECTS_PER_LEVEL * 2, LEVELS_ROUND = PLACES / OBJECTS_PER_LEVEL };

typedef uint16_t Place;   /* an entry of event_saved_positions */

/* What the original leaves as it finds a place: the multiplier holding the
 * level's offset (it multiplies in hardware), the offset in a scratch cell,
 * and the V of the addition. */
static void leave_place_found(GameState *g, FieldObject object, uint8_t level)
{
    uint16_t row = (uint16_t)(level * BYTES_PER_LEVEL), column = (uint16_t)(object * 2);
    hw_set_multiplicand(g, level);
    hw_multiply_by(g, BYTES_PER_LEVEL);
    keep_word(g, KEPT_FIFTH, (uint16_t)(row + column));
    leave_overflow_of_adding16(row, column);
}

static Place place_of(FieldObject object, uint8_t level) { return (Place)(level * OBJECTS_PER_LEVEL + object); }

static Place place_found(GameState *g, FieldObject object, uint8_t level)
{
    ORIGINAL_EFFECT(leave_place_found(g, object, level));
    return place_of(object, level);
}

/* The place the next level down keeps. */
static Place place_below(Place p) { return (Place)((p + OBJECTS_PER_LEVEL) % PLACES); }

static uint16_t place(const GameState *g, Place p) { return event_saved_positions(g, p); }
static void set_place(GameState *g, Place p, uint16_t at) { set_event_saved_positions(g, p, at); }

/* The most urgent unfinished thing below an object's level: counting down
 * the levels from the one below, the first place with something to resume,
 * and the level it is at. The search comes round past the last level to the
 * first; it finds nothing if it comes round to where it started. */
typedef struct {
    bool found;
    Place place;
    uint8_t level;
} Resumption;

static Resumption resumption_below(const GameState *g, FieldObject object, uint8_t level)
{
    Place p = place_of(object, level);
    for (unsigned k = 1; k < LEVELS_ROUND; k++) {
        p = place_below(p);
        if (place(g, p) != 0)
            return (Resumption){ true, p, (uint8_t)(level + k) };
    }
    return (Resumption){ false, 0, 0 };
}

/* Where one of an object's functions starts: the table at the head of the
 * script holds 16 offsets for each object. */
static uint16_t function_start(GameState *g, FieldObject object, uint8_t function)
{
    uint16_t table = (uint16_t)(object * FUNCTIONS * 2), entry = (uint16_t)(function * 2);
    ORIGINAL_EFFECT(keep_word(g, KEPT_SIXTH, table));
    ORIGINAL_EFFECT(leave_overflow_of_adding16(entry, table));
    return script_word(g, (uint16_t)(table + entry));
}

/* ---- Making a call ------------------------------------------------------------------------ */

typedef enum { TAKES_CALLS, REFUSES_CALLS, PASSED_OVER } Answer;

/* A call, as the scripts write it: the level in its high nibble, the function
 * in its low. What the original keeps of it: the target, then the target's
 * level, the call and the level it asks for. */
static uint8_t call_byte(uint8_t level, uint8_t function) { return (uint8_t)(level << 4 | function); }

static void keep_target(GameState *g, FieldObject target)
{
    keep_word(g, KEPT_SECOND, field_link_byte(field_link_to(target)));
}

static void keep_levels(GameState *g, uint8_t current, uint8_t level, uint8_t function)
{
    keep_byte(g, KEPT_FIRST, current);
    keep_byte(g, KEPT_THIRD, call_byte(level, function));
    keep_byte(g, KEPT_FOURTH, level);
}

static Answer answer(GameState *g, FieldObject target)
{
    ORIGINAL_EFFECT(keep_target(g, target));
    if (refusing_calls(g, target))
        return REFUSES_CALLS;
    return object_unreachable(g, target) ? PASSED_OVER : TAKES_CALLS;
}

/* How urgent a call at `level` is against what the target is doing: below 0
 * more, 0 as, above 0 less. */
static int urgency(GameState *g, FieldObject target, uint8_t level, uint8_t function)
{
    uint8_t current = event_object_call_state_level(g, target);
    ORIGINAL_EFFECT(keep_levels(g, current, level, function));
    return level < current ? -1 : level > current;
}

/* The target drops what it is doing for the function. */
static void interrupt(GameState *g, FieldObject target, uint8_t level, uint8_t function)
{
    Place own = place_found(g, target, event_object_call_state_level(g, target));
    set_place(g, own, event_object_script_position(g, target));
    set_event_object_script_position(g, target, function_start(g, target, function));
    set_event_object_call_state_level(g, target, level);
    object_stop_moving(g, target);
}

// $C0:603E
/* The function waits at its level, unless something already does. */
static void queue(GameState *g, FieldObject target, uint8_t level, uint8_t function)
{
    Place p = place_found(g, target, level);
    if (place(g, p) == 0)
        set_place(g, p, function_start(g, target, function));
}

// $C0:5FBE
/* Call a function of another object and go on: it interrupts the target or
 * waits its turn, unless the target refuses calls or is passed over. A call
 * to a party member who isn't there is no call. */
static EventFlow call_function(GameState *g, FieldObject target, bool there, uint8_t level, uint8_t function)
{
    if (!there || answer(g, target) != TAKES_CALLS)
        return next_command();
    int order = urgency(g, target, level, function);
    if (order > 0)
        queue(g, target, level, function);
    else if (order < 0)
        interrupt(g, target, level, function);
    return next_command();
}
EVENT_OPERATION(call_function, target, there, level, function)

/* Whether the target takes a call: it doesn't if it isn't there (a party
 * member's place empty) or is passed over, not yet if it refuses calls or
 * the call is no more urgent than what it is doing; otherwise it drops what
 * it is doing for the function. */
typedef enum { NO_CALL, NOT_YET, TAKEN } Taking;

static Taking take_the_call(GameState *g, FieldObject target, bool there, uint8_t level, uint8_t function)
{
    if (!there) {
        ORIGINAL_EFFECT(leave_parse_at(3));
        return NO_CALL;
    }
    switch (answer(g, target)) {
    case REFUSES_CALLS: return NOT_YET;
    case PASSED_OVER: return NO_CALL;
    default: break;
    }
    if (urgency(g, target, level, function) >= 0)
        return NOT_YET;
    interrupt(g, target, level, function);
    return TAKEN;
}

// $C0:6086
/* Call, waiting until the target takes the call. */
static EventFlow call_when_taken(GameState *g, FieldObject target, bool there, uint8_t level, uint8_t function)
{
    return take_the_call(g, target, there, level, function) == NOT_YET ? stay() : next_command();
}
EVENT_OPERATION(call_when_taken, target, there, level, function)

static bool waiting(const GameState *g, FieldObject self) { return event_call_waiting(g, self) != 0; }
static void set_waiting(GameState *g, FieldObject self, bool on) { set_event_call_waiting(g, self, on); }

// $C0:6109
/* Call and wait until the function has run: the caller waits for the target
 * to take the call, then marks itself waiting and stays until the target has
 * come back down from the call's level (or is passed over). */
static EventFlow call_and_wait(GameState *g, FieldObject self, FieldObject target, bool there, uint8_t level,
                               uint8_t function)
{
    if (waiting(g, self)) {
        ORIGINAL_EFFECT(keep_target(g, target));
        if (!object_unreachable(g, target) && urgency(g, target, level, function) >= 0)
            return stay();
        set_waiting(g, self, false);
        return next_command();
    }
    switch (take_the_call(g, target, there, level, function)) {
    case NO_CALL: return next_command();
    case NOT_YET: return stay();
    default: set_waiting(g, self, true); return stay();
    }
}
EVENT_OPERATION(call_and_wait, self, target, there, level, function)

/* What the original's search leaves: the overflow of its last step, from the
 * place before the one it found to it. */
static void leave_search_overflow(Place found)
{
    Place before = (Place)((found + PLACES - OBJECTS_PER_LEVEL) % PLACES);
    leave_overflow_of_adding16((uint16_t)(before * 2), BYTES_PER_LEVEL);
}

// $C0:5F74
/* A function ends: the object clears the place of the level it ran at and
 * resumes the most urgent unfinished thing below it. At the lowest level
 * there is nothing below, and the script stays here. (The guard has turned
 * away an object with nothing to resume.) */
static EventFlow return_from_function(GameState *g, FieldObject self)
{
    uint8_t level = event_object_call_state_level(g, self);
    if (level == LOWEST_LEVEL)
        return stay();
    ORIGINAL_EFFECT(hw_set_multiplicand(g, level));
    ORIGINAL_EFFECT(hw_multiply_by(g, BYTES_PER_LEVEL));   /* the original multiplies in hardware */
    set_place(g, place_of(self, level), 0);
    Resumption r = resumption_below(g, self, level);
    ORIGINAL_EFFECT(leave_search_overflow(r.place));
    set_event_object_call_state_level(g, self, r.level);
    return go_to(place(g, r.place));
}
EVENT_OPERATION(return_from_function, self)

// $C0:6240
/* The object stops, or starts again, taking calls. */
static EventFlow take_calls(GameState *g, FieldObject self, bool takes)
{
    set_event_object_call_state_refusing(g, self, !takes);
    return next_command();
}
EVENT_OPERATION(take_calls, self, takes)

// $C0:6254
/* Take an object off the map: disabled, and its sprite gone. */
static EventFlow disable_object(GameState *g, FieldObject object)
{
    object_take_off_the_map(g, object);
    return next_command();
}
EVENT_OPERATION(disable_object, object)

// $C0:6269
/* Calls pass an object over from now on; if that is the object running, its
 * turn ends. */
static EventFlow make_unreachable(GameState *g, FieldObject self, FieldObject object)
{
    object_set_unreachable(g, object, true);
    return object == self ? end_turn() : next_command();
}
EVENT_OPERATION(make_unreachable, self, object)

// $C0:6282
/* Calls reach an object again. */
static EventFlow make_reachable(GameState *g, FieldObject object)
{
    object_set_unreachable(g, object, false);
    return next_command();
}
EVENT_OPERATION(make_reachable, object)

/* The guard's part for a return: at the lowest level, or with something to
 * resume below it (the original never stops searching otherwise). */
static bool has_something_to_resume(const GameState *g, FieldObject self)
{
    uint8_t level = event_object_call_state_level(g, self);
    return level == LOWEST_LEVEL || resumption_below(g, self, level).found;
}
EVENT_CHECK(has_something_to_resume, self)

/* A call's target is an object, or a party member, who may not be there. */
#define TO_AN_OBJECT .target = OBJECT(1), .there = CONSTANT(true)
#define TO_A_MEMBER .target = MEMBER_OBJECT(1), .there = MEMBER_PRESENT(1)
#define THE_CALL .level = OPERAND_BITS(2, 4, 0x0F), .function = OPERAND_BITS(2, 0, 0x0F)

EVENT_OPCODES(event_calls_opcodes,
    { 0x02, OPERATION(call_function), TO_AN_OBJECT, THE_CALL, .length = 3 },
    { 0x05, OPERATION(call_function), TO_A_MEMBER, THE_CALL, .length = 3, .member_may_be_absent = true },
    { 0x03, OPERATION(call_when_taken), TO_AN_OBJECT, THE_CALL, .length = 3 },
    { 0x06, OPERATION(call_when_taken), TO_A_MEMBER, THE_CALL, .length = 3, .member_may_be_absent = true },
    { 0x04, OPERATION(call_and_wait), TO_AN_OBJECT, THE_CALL, .length = 3 },
    /* the wait goes on with the member's place even when it is empty, so the
     * marker there must name an object as the original indexes it */
    { 0x07, OPERATION(call_and_wait), TO_A_MEMBER, THE_CALL, .length = 3 },
    { 0x00, OPERATION(return_from_function), .length = 1, CHECK(has_something_to_resume) },
    { 0x08, OPERATION(take_calls), .takes = CONSTANT(false), .length = 1 },
    { 0x09, OPERATION(take_calls), .takes = CONSTANT(true), .length = 1 },
    { 0x0A, OPERATION(disable_object), .object = OBJECT(1), .length = 2 },
    { 0x0B, OPERATION(make_unreachable), .object = OBJECT(1), .length = 2 },
    { 0x0C, OPERATION(make_reachable), .object = OBJECT(1), .length = 2 });
