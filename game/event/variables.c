/* Scripts computing with their variables (event/variables.h): setting them
 * from numbers, from each other and from the story; counting; adding and
 * subtracting; treating one as a set of flags; and rolling a random number.
 *
 * Nearly every operation here has a byte and a word form, and takes its
 * amount as a number in the script or as another variable's value; the rows
 * at the end say which opcode is which. */
#include "event/event_data.h"
#include "event/leaves.h"
#include "event/script.h"
#include "event/variables.h"

// $C0:68A5
/* A variable takes a value. */
static EventFlow assign(GameState *g, EventVariable v, uint16_t value, Width w)
{
    set_variable(g, v, value, w);
    return next_command();
}

// $C0:6A1F
/* Add to a variable: a score, a count of tries, a position worked out. */
static EventFlow add(GameState *g, EventVariable v, uint16_t amount, Width w)
{
    set_variable(g, v, (uint16_t)(variable(g, v, w) + amount), w);
    return next_command();
}

// $C0:6AA3
/* Take from a variable. */
static EventFlow subtract(GameState *g, EventVariable v, uint16_t amount, Width w)
{
    set_variable(g, v, (uint16_t)(variable(g, v, w) - amount), w);
    return next_command();
}


/* A byte variable as a set of flags: raise some, keep only some, or turn
 * some over. */

// $C0:6BF7
static EventFlow set_flags(GameState *g, EventVariable v, uint8_t flags)
{
    set_variable_byte(g, v, (uint8_t)(variable_byte(g, v) | flags));
    return next_command();
}

// $C0:6BD2
static EventFlow keep_only_flags(GameState *g, EventVariable v, uint8_t flags)
{
    set_variable_byte(g, v, (uint8_t)(variable_byte(g, v) & flags));
    return next_command();
}

// $C0:6C1C
static EventFlow toggle_flags(GameState *g, EventVariable v, uint8_t flags)
{
    set_variable_byte(g, v, (uint8_t)(variable_byte(g, v) ^ flags));
    return next_command();
}

// $C0:6C41
/* Halve a byte variable `times` times; 0 times means 256. */
static EventFlow halve(GameState *g, EventVariable v, uint8_t times)
{
    unsigned n = times ? times : 256;
    set_variable_byte(g, v, n >= 8 ? 0 : (uint8_t)(variable_byte(g, v) >> n));
    /* the original counts the halvings down in its scratch cell */
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, 0));
    return next_command();
}

/* The game's random numbers: a fixed table of 256 bytes it steps through,
 * one step for each number drawn. */
static uint8_t draw_random(GameState *g)
{
    uint8_t step = (uint8_t)(field_random_index(g) + 1);
    set_field_random_index(g, step);
    return random_byte(g, step);
}

// $C0:6D09
/* A byte variable takes a random number: a chance in a script's hands. */
static EventFlow roll(GameState *g, EventVariable v)
{
    uint8_t value = draw_random(g);
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, value));
    set_variable_byte(g, v, value);
    return next_command();
}

static EventFlow run_assign(GameState *g, const EventArgs *a) { return assign(g, a->arg[0], a->arg[1], a->arg[2]); }
static EventFlow run_add(GameState *g, const EventArgs *a) { return add(g, a->arg[0], a->arg[1], a->arg[2]); }
static EventFlow run_subtract(GameState *g, const EventArgs *a) { return subtract(g, a->arg[0], a->arg[1], a->arg[2]); }
static EventFlow run_set_flags(GameState *g, const EventArgs *a) { return set_flags(g, a->arg[0], a->arg[1]); }
static EventFlow run_keep_only_flags(GameState *g, const EventArgs *a) { return keep_only_flags(g, a->arg[0], a->arg[1]); }
static EventFlow run_toggle_flags(GameState *g, const EventArgs *a) { return toggle_flags(g, a->arg[0], a->arg[1]); }
static EventFlow run_halve(GameState *g, const EventArgs *a) { return halve(g, a->arg[0], a->arg[1]); }
static EventFlow run_roll(GameState *g, const EventArgs *a) { return roll(g, a->arg[0]); }

#define BYTES CONSTANT(AS_BYTE)
#define WORDS CONSTANT(AS_WORD)

/* The original keeps its first operand in a scratch cell (event/leaves.h),
 * and its additions leave their overflow: both are columns here, not the
 * operations' business. */
EVENT_OPCODES(event_variables_opcodes,
    { 0x4F, run_assign, { VARIABLE(2), BYTE(1), BYTES }, 3, .routine = 0xC068A5 },
    { 0x50, run_assign, { VARIABLE(3), WORD(1), WORDS }, 4, .routine = 0xC068C6 },
    { 0x51, run_assign, { VARIABLE(2), VARIABLE_WORD(1), BYTES }, 3, .routine = 0xC068E9 },
    { 0x52, run_assign, { VARIABLE(2), VARIABLE_WORD(1), WORDS }, 3, .routine = 0xC06913 },
    { 0x53, run_assign, { VARIABLE(3), STORY_WORD(1), BYTES }, 4, .routine = 0xC0693D },
    { 0x54, run_assign, { VARIABLE(3), STORY_WORD(1), WORDS }, 4, .routine = 0xC06964 },
    { 0x55, run_assign, { VARIABLE(1), STORY_PROGRESS, BYTES }, 2, .routine = 0xC0698B },
    { 0x75, run_assign, { VARIABLE(1), CONSTANT(1), BYTES }, 2, .routine = 0xC06CBD },
    { 0x76, run_assign, { VARIABLE(1), CONSTANT(1), WORDS }, 2, .routine = 0xC06CD6 },
    { 0x77, run_assign, { VARIABLE(1), CONSTANT(0), BYTES }, 2, .routine = 0xC06CF0 },

    { 0x5B, run_add, { VARIABLE(2), BYTE(1), BYTES }, 3, .routine = 0xC06A1F },
    { 0x5D, run_add, { VARIABLE(2), VARIABLE_WORD(1), BYTES }, 3, .routine = 0xC06A45 },
    { 0x5E, run_add, { VARIABLE(2), VARIABLE_WORD(1), WORDS }, 3, .routine = 0xC06A74 },
    { 0x5F, run_subtract, { VARIABLE(2), BYTE(1), BYTES }, 3, .routine = 0xC06AA3 },
    { 0x60, run_subtract, { VARIABLE(3), WORD(1), WORDS }, 4, .routine = 0xC06AC9 },
    { 0x61, run_subtract, { VARIABLE(2), VARIABLE_WORD(1), BYTES }, 3, .routine = 0xC06AF1 },

    { 0x71, run_add, { VARIABLE(1), CONSTANT(1), BYTES }, 2, .routine = 0xC06C69 },
    { 0x72, run_add, { VARIABLE(1), CONSTANT(1), WORDS }, 2, .routine = 0xC06C85 },
    { 0x73, run_subtract, { VARIABLE(1), CONSTANT(1), BYTES }, 2, .routine = 0xC06CA1 },

    { 0x63, run_set_flags, { VARIABLE(2), BIT(1) }, 3, .routine = 0xC06B20 },
    { 0x69, run_set_flags, { VARIABLE(2), BYTE(1) }, 3, .routine = 0xC06BF7 },
    { 0x64, run_keep_only_flags, { VARIABLE(2), ALL_BUT_BIT(1) }, 3, .routine = 0xC06B4B },
    { 0x67, run_keep_only_flags, { VARIABLE(2), BYTE(1) }, 3, .routine = 0xC06BD2 },
    { 0x6B, run_toggle_flags, { VARIABLE(2), BYTE(1) }, 3, .routine = 0xC06C1C },
    { 0x6F, run_halve, { VARIABLE(2), BYTE(1) }, 3, .routine = 0xC06C41 },
    { 0x7F, run_roll, { VARIABLE(1) }, 2, .routine = 0xC06D09 });
