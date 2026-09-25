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
#include "event/calls.h"
#include "event/leaves.h"
#include "event/script.h"

enum { LOWEST_LEVEL = EVENT_LEVELS - 1, FUNCTIONS = 16 };

static bool refusing_calls(const GameState *g, FieldObject object)
{
    return event_object_call_state_refusing(g, object) != 0;
}

/* ---- Levels and places ------------------------------------------------------------------ */

/* The places, a level at a time and an object's a word in each: the place of
 * an object at a level is entry level * 64 + object of event_saved_positions,
 * which holds the eight levels. */
enum { OBJECTS_PER_LEVEL = 64, BYTES_PER_LEVEL = OBJECTS_PER_LEVEL * 2 };

typedef uint16_t Place;   /* an entry of event_saved_positions */

/* What the original leaves as it finds a place: the multiplier holding the
 * level's offset (it multiplies in hardware). */
static void leave_multiplying(GameState *g, uint8_t level)
{
    hw_set_multiplicand(g, level);
    hw_multiply_by(g, BYTES_PER_LEVEL);
}

/* The offset of an object's place at a level among the places, which the
 * original works out in a scratch cell. */
static uint16_t event_place_offset(FieldObject object, uint8_t level)
{
    return (uint16_t)(level * BYTES_PER_LEVEL + field_object_offset(object));
}

static Place place_of(FieldObject object, uint8_t level) { return (Place)(level * OBJECTS_PER_LEVEL + object); }

/* ... and the calls keep its offset in a scratch cell. */
static Place place_found(GameState *g, FieldObject object, uint8_t level)
{
    ORIGINAL_EFFECT(leave_multiplying(g, level));
    ORIGINAL_EFFECT(keep_word(g, KEPT_FIFTH, event_place_offset(object, level)));
    return place_of(object, level);
}

static uint16_t place(const GameState *g, Place p) { return event_saved_positions(g, p); }
static void set_place(GameState *g, Place p, uint16_t at) { set_event_saved_positions(g, p, at); }

/* The most urgent unfinished thing below an object's level: counting down
 * the levels from the one below to the lowest, the first with something to
 * resume. */
typedef struct {
    bool found;
    uint8_t level;
} Resumption;

static Resumption resumption_below(const GameState *g, FieldObject object, uint8_t level)
{
    for (uint8_t below = (uint8_t)(level + 1); below <= LOWEST_LEVEL; below++)
        if (place(g, place_of(object, below)) != 0)
            return (Resumption){ true, below };
    return (Resumption){ false, 0 };
}

/* Where one of an object's functions starts: the table at the head of the
 * script holds 16 offsets for each object. */
static uint16_t function_table(FieldObject object) { return (uint16_t)(object * FUNCTIONS * 2); }

uint16_t object_function_start(const GameState *g, FieldObject object, uint8_t function)
{
    return script_word(g, (uint16_t)(function_table(object) + function * 2));
}

/* A call keeps where the target's functions are as it looks one up. */
static uint16_t function_start(GameState *g, FieldObject object, uint8_t function)
{
    ORIGINAL_EFFECT(keep_word(g, KEPT_SIXTH, function_table(object)));
    return object_function_start(g, object, function);
}

/* ---- Making a call ------------------------------------------------------------------------ */

typedef enum { TAKES_CALLS, REFUSES_CALLS, PASSED_OVER } Answer;

/* A call, as the scripts write it: the level in its high nibble, the function
 * in its low. What the original keeps of it: the target, then the target's
 * level, the call and the level it asks for. */
static uint8_t call_byte(uint8_t level, uint8_t function) { return (uint8_t)(level << 4 | function); }

static void keep_target(GameState *g, FieldObject target) { keep_word(g, KEPT_SECOND, field_object_offset(target)); }

static void keep_levels(GameState *g, uint8_t current, uint8_t level, uint8_t function)
{
    keep_byte(g, KEPT_FIRST, current);
    keep_byte(g, KEPT_THIRD, call_byte(level, function));
    keep_byte(g, KEPT_FOURTH, level);
}

static Answer answer_of(const GameState *g, FieldObject target)
{
    if (refusing_calls(g, target))
        return REFUSES_CALLS;
    return object_unreachable(g, target) ? PASSED_OVER : TAKES_CALLS;
}

static Answer answer(GameState *g, FieldObject target)
{
    ORIGINAL_EFFECT(keep_target(g, target));
    return answer_of(g, target);
}

/* How urgent a call at `level` is against what the target is doing: below 0
 * more, 0 as, above 0 less. */
static int urgency_of(const GameState *g, FieldObject target, uint8_t level)
{
    uint8_t current = event_object_call_state_level(g, target);
    return level < current ? -1 : level > current;
}

static int urgency(GameState *g, FieldObject target, uint8_t level, uint8_t function)
{
    ORIGINAL_EFFECT(keep_levels(g, event_object_call_state_level(g, target), level, function));
    return urgency_of(g, target, level);
}

/* The object keeps its place at the level it runs at, to come back to, and
 * runs script from `start` at `level`, forgetting the move it was making. */
static void run_from(GameState *g, FieldObject object, Place own, uint8_t level, uint16_t start)
{
    set_place(g, own, event_object_script_position(g, object));
    set_event_object_script_position(g, object, start);
    set_event_object_call_state_level(g, object, level);
    object_stop_moving(g, object);
}

/* The target drops what it is doing for the function. */
static void interrupt(GameState *g, FieldObject target, uint8_t level, uint8_t function)
{
    Place own = place_found(g, target, event_object_call_state_level(g, target));
    run_from(g, target, own, level, function_start(g, target, function));
}

/* The field's hand-overs keep the place's offset in another scratch cell
 * than the calls do. */
void object_run_script_from(GameState *g, FieldObject object, uint8_t level, uint16_t start)
{
    uint8_t own = event_object_call_state_level(g, object);
    ORIGINAL_EFFECT(leave_multiplying(g, own));
    ORIGINAL_EFFECT(keep_word(g, KEPT_FIRST, event_place_offset(object, own)));
    run_from(g, object, place_of(object, own), level, start);
    ORIGINAL_EFFECT(object_clear_move_length_copy(g, object));
}

bool object_takes_calls(const GameState *g, FieldObject object) { return answer_of(g, object) == TAKES_CALLS; }

bool object_takes_a_call_now(const GameState *g, FieldObject target, uint8_t level)
{
    return object_takes_calls(g, target) && urgency_of(g, target, level) < 0;
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
    if (!there)
        return NO_CALL;
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
    set_event_object_call_state_level(g, self, r.level);
    return go_to(place(g, place_of(self, r.level)));
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

/* For event/leaves.c, before a call runs: whether it reaches the target's
 * places -- queued or interrupting where it just calls, interrupting where
 * it waits to be taken, and not while its caller waits it out. */
bool event_call_reaches_the_places(const GameState *g, CallKind kind, FieldObject self, FieldObject target, bool there,
                                   uint8_t level)
{
    if ((kind == CALL_AND_WAIT && waiting(g, self)) || !there || answer_of(g, target) != TAKES_CALLS)
        return false;
    int order = urgency_of(g, target, level);
    return kind == CALL_AND_GO ? order != 0 : order < 0;
}

/* The guard's part for a return: at the lowest level, or with something to
 * resume in the levels below it. Past them the original's search reads on
 * past the places, round its 16-bit offset, which only the original runs. */
static bool has_something_to_resume(const GameState *g, FieldObject self)
{
    uint8_t level = event_object_call_state_level(g, self);
    return level == LOWEST_LEVEL || (level < LOWEST_LEVEL && resumption_below(g, self, level).found);
}
EVENT_CHECK(has_something_to_resume, self)

/* And for a call: the level it asks for and the target's are among the eight
 * the places hold (a call byte's level can say up to 15). */
static bool levels_in_the_places(const GameState *g, FieldObject target, bool there, uint8_t level)
{
    return level <= LOWEST_LEVEL && (!there || event_object_call_state_level(g, target) <= LOWEST_LEVEL);
}
EVENT_CHECK(levels_in_the_places, target, there, level)

/* A wait on a party member goes on looking at the member's object, which an
 * empty place doesn't have: the original reads the tables' neighbours
 * there, and runs that itself. */
static bool can_wait_on(const GameState *g, FieldObject self, FieldObject target, bool there, uint8_t level)
{
    return levels_in_the_places(g, target, there, level) && (there || !waiting(g, self));
}
EVENT_CHECK(can_wait_on, self, target, there, level)

/* A call's target is an object, or a party member, who may not be there. */
#define TO_AN_OBJECT .target = OBJECT(1), .there = CONSTANT(true)
#define TO_A_MEMBER .target = MEMBER_OBJECT(1), .there = MEMBER_PRESENT(1)
#define THE_CALL .level = OPERAND_BITS(2, 4, 0x0F), .function = OPERAND_BITS(2, 0, 0x0F)
#define CHECKED CHECK(levels_in_the_places)

EVENT_OPCODES(event_calls_opcodes,
    { 0x02, OPERATION(call_function), TO_AN_OBJECT, THE_CALL, CHECKED, .length = 3 },
    { 0x05, OPERATION(call_function), TO_A_MEMBER, THE_CALL, CHECKED, .length = 3, .member_may_be_absent = true },
    { 0x03, OPERATION(call_when_taken), TO_AN_OBJECT, THE_CALL, CHECKED, .length = 3 },
    { 0x06, OPERATION(call_when_taken), TO_A_MEMBER, THE_CALL, CHECKED, .length = 3, .member_may_be_absent = true },
    { 0x04, OPERATION(call_and_wait), TO_AN_OBJECT, THE_CALL, CHECK(can_wait_on), .length = 3 },
    { 0x07, OPERATION(call_and_wait), TO_A_MEMBER, THE_CALL, CHECK(can_wait_on), .length = 3,
      .member_may_be_absent = true },
    { 0x00, OPERATION(return_from_function), .length = 1, CHECK(has_something_to_resume) },
    { 0x08, OPERATION(take_calls), .takes = CONSTANT(false), .length = 1 },
    { 0x09, OPERATION(take_calls), .takes = CONSTANT(true), .length = 1 },
    { 0x0A, OPERATION(disable_object), .object = OBJECT(1), .length = 2 },
    { 0x0B, OPERATION(make_unreachable), .object = OBJECT(1), .length = 2 },
    { 0x0C, OPERATION(make_reachable), .object = OBJECT(1), .length = 2 });
