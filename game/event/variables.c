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
#include "field/field.h"

// $C0:68A5
/* A variable takes a value. */
static EventFlow assign(GameState *g, EventVariable variable, uint16_t value, Width width)
{
    set_variable(g, variable, value, width);
    return next_command();
}
EVENT_OPERATION(assign, variable, value, width)

// $C0:6A1F
/* Add to a variable: a score, a count of tries, a position worked out. */
static EventFlow add(GameState *g, EventVariable counted, uint16_t amount, Width width)
{
    set_variable(g, counted, (uint16_t)(variable(g, counted, width) + amount), width);
    return next_command();
}
EVENT_OPERATION(add, variable, amount, width)

// $C0:6AA3
/* Take from a variable. */
static EventFlow subtract(GameState *g, EventVariable counted, uint16_t amount, Width width)
{
    set_variable(g, counted, (uint16_t)(variable(g, counted, width) - amount), width);
    return next_command();
}
EVENT_OPERATION(subtract, variable, amount, width)


/* A byte variable as a set of flags: raise some, keep only some, or turn
 * some over. */

// $C0:6BF7
static EventFlow set_flags(GameState *g, EventVariable variable, uint8_t flags)
{
    set_variable_byte(g, variable, (uint8_t)(variable_byte(g, variable) | flags));
    return next_command();
}
EVENT_OPERATION(set_flags, variable, flags)

// $C0:6BD2
static EventFlow keep_only_flags(GameState *g, EventVariable variable, uint8_t flags)
{
    set_variable_byte(g, variable, (uint8_t)(variable_byte(g, variable) & flags));
    return next_command();
}
EVENT_OPERATION(keep_only_flags, variable, flags)

// $C0:6C1C
static EventFlow toggle_flags(GameState *g, EventVariable variable, uint8_t flags)
{
    set_variable_byte(g, variable, (uint8_t)(variable_byte(g, variable) ^ flags));
    return next_command();
}
EVENT_OPERATION(toggle_flags, variable, flags)

// $C0:6C41
/* Halve a byte variable `times` times; 0 times means 256. */
static EventFlow halve(GameState *g, EventVariable variable, uint8_t times)
{
    unsigned n = times ? times : 256;
    set_variable_byte(g, variable, n >= 8 ? 0 : (uint8_t)(variable_byte(g, variable) >> n));
    /* the original counts the halvings down in its scratch cell */
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, 0));
    return next_command();
}
EVENT_OPERATION(halve, variable, times)

// $C0:6D09
/* A byte variable takes a random number: a chance in a script's hands. */
static EventFlow roll(GameState *g, EventVariable variable)
{
    uint8_t value = field_draw_random(g);
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, value));
    set_variable_byte(g, variable, value);
    return next_command();
}
EVENT_OPERATION(roll, variable)

/* The original keeps the value or the amount in a scratch cell, and its
 * additions leave their overflow (event/leaves.c). */
EVENT_OPCODES(event_variables_opcodes,
    { 0x4F, OPERATION(assign), .variable = VARIABLE(2), .value = OPERAND(1), .width = BYTES, .length = 3 },
    { 0x50, OPERATION(assign), .variable = VARIABLE(3), .value = OPERAND_WORD(1), .width = WORDS, .length = 4 },
    { 0x51, OPERATION(assign), .variable = VARIABLE(2), .value = VARIABLE_WORD(1), .width = BYTES, .length = 3 },
    { 0x52, OPERATION(assign), .variable = VARIABLE(2), .value = VARIABLE_WORD(1), .width = WORDS, .length = 3 },
    { 0x53, OPERATION(assign), .variable = VARIABLE(3), .value = STORY_WORD(1), .width = BYTES, .length = 4 },
    { 0x54, OPERATION(assign), .variable = VARIABLE(3), .value = STORY_WORD(1), .width = WORDS, .length = 4 },
    { 0x55, OPERATION(assign), .variable = VARIABLE(1), .value = STORY_PROGRESS, .width = BYTES, .length = 2 },
    { 0x75, OPERATION(assign), .variable = VARIABLE(1), .value = CONSTANT(1), .width = BYTES, .length = 2 },
    { 0x76, OPERATION(assign), .variable = VARIABLE(1), .value = CONSTANT(1), .width = WORDS, .length = 2 },
    { 0x77, OPERATION(assign), .variable = VARIABLE(1), .value = CONSTANT(0), .width = BYTES, .length = 2 },

    { 0x5B, OPERATION(add), .variable = VARIABLE(2), .amount = OPERAND(1), .width = BYTES, .length = 3 },
    { 0x5D, OPERATION(add), .variable = VARIABLE(2), .amount = VARIABLE_WORD(1), .width = BYTES, .length = 3 },
    { 0x5E, OPERATION(add), .variable = VARIABLE(2), .amount = VARIABLE_WORD(1), .width = WORDS, .length = 3 },
    { 0x5F, OPERATION(subtract), .variable = VARIABLE(2), .amount = OPERAND(1), .width = BYTES, .length = 3 },
    { 0x60, OPERATION(subtract), .variable = VARIABLE(3), .amount = OPERAND_WORD(1), .width = WORDS, .length = 4 },
    { 0x61, OPERATION(subtract), .variable = VARIABLE(2), .amount = VARIABLE_WORD(1), .width = BYTES, .length = 3 },

    { 0x71, OPERATION(add), .variable = VARIABLE(1), .amount = CONSTANT(1), .width = BYTES, .length = 2 },
    { 0x72, OPERATION(add), .variable = VARIABLE(1), .amount = CONSTANT(1), .width = WORDS, .length = 2 },
    { 0x73, OPERATION(subtract), .variable = VARIABLE(1), .amount = CONSTANT(1), .width = BYTES, .length = 2 },

    { 0x63, OPERATION(set_flags), .variable = VARIABLE(2), .flags = BIT(1), .length = 3 },
    { 0x69, OPERATION(set_flags), .variable = VARIABLE(2), .flags = OPERAND(1), .length = 3 },
    { 0x64, OPERATION(keep_only_flags), .variable = VARIABLE(2), .flags = ALL_BUT_BIT(1), .length = 3 },
    { 0x67, OPERATION(keep_only_flags), .variable = VARIABLE(2), .flags = OPERAND(1), .length = 3 },
    { 0x6B, OPERATION(toggle_flags), .variable = VARIABLE(2), .flags = OPERAND(1), .length = 3 },
    { 0x6F, OPERATION(halve), .variable = VARIABLE(2), .times = OPERAND(1), .length = 3 },
    { 0x7F, OPERATION(roll), .variable = VARIABLE(1), .length = 2 });
