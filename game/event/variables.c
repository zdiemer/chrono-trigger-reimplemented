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
static EventFlow assign(GameState *g, const EventCommand *c)
{
    set_variable(g, c->variable, c->value, c->width);
    return next_command();
}

// $C0:6A1F
/* Add to a variable: a score, a count of tries, a position worked out. */
static EventFlow add(GameState *g, const EventCommand *c)
{
    set_variable(g, c->variable, (uint16_t)(variable(g, c->variable, c->width) + c->amount), c->width);
    return next_command();
}

// $C0:6AA3
/* Take from a variable. */
static EventFlow subtract(GameState *g, const EventCommand *c)
{
    set_variable(g, c->variable, (uint16_t)(variable(g, c->variable, c->width) - c->amount), c->width);
    return next_command();
}


/* A byte variable as a set of flags: raise some, keep only some, or turn
 * some over. */

// $C0:6BF7
static EventFlow set_flags(GameState *g, const EventCommand *c)
{
    set_variable_byte(g, c->variable, (uint8_t)(variable_byte(g, c->variable) | c->flags));
    return next_command();
}

// $C0:6BD2
static EventFlow keep_only_flags(GameState *g, const EventCommand *c)
{
    set_variable_byte(g, c->variable, (uint8_t)(variable_byte(g, c->variable) & c->flags));
    return next_command();
}

// $C0:6C1C
static EventFlow toggle_flags(GameState *g, const EventCommand *c)
{
    set_variable_byte(g, c->variable, (uint8_t)(variable_byte(g, c->variable) ^ c->flags));
    return next_command();
}

// $C0:6C41
/* Halve a byte variable `times` times; 0 times means 256. */
static EventFlow halve(GameState *g, const EventCommand *c)
{
    unsigned n = c->times ? c->times : 256;
    set_variable_byte(g, c->variable, n >= 8 ? 0 : (uint8_t)(variable_byte(g, c->variable) >> n));
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
static EventFlow roll(GameState *g, const EventCommand *c)
{
    uint8_t value = draw_random(g);
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, value));
    set_variable_byte(g, c->variable, value);
    return next_command();
}

/* The original keeps the value or the amount in a scratch cell, and its
 * additions leave their overflow (event/leaves.c). */
EVENT_OPCODES(event_variables_opcodes,
    { 0x4F, assign, .variable = VARIABLE(2), .value = OPERAND(1), .width = BYTES, .length = 3, .routine = 0xC068A5 },
    { 0x50, assign, .variable = VARIABLE(3), .value = OPERAND_WORD(1), .width = WORDS, .length = 4,
      .routine = 0xC068C6 },
    { 0x51, assign, .variable = VARIABLE(2), .value = VARIABLE_WORD(1), .width = BYTES, .length = 3,
      .routine = 0xC068E9 },
    { 0x52, assign, .variable = VARIABLE(2), .value = VARIABLE_WORD(1), .width = WORDS, .length = 3,
      .routine = 0xC06913 },
    { 0x53, assign, .variable = VARIABLE(3), .value = STORY_WORD(1), .width = BYTES, .length = 4, .routine = 0xC0693D },
    { 0x54, assign, .variable = VARIABLE(3), .value = STORY_WORD(1), .width = WORDS, .length = 4, .routine = 0xC06964 },
    { 0x55, assign, .variable = VARIABLE(1), .value = STORY_PROGRESS, .width = BYTES, .length = 2,
      .routine = 0xC0698B },
    { 0x75, assign, .variable = VARIABLE(1), .value = CONSTANT(1), .width = BYTES, .length = 2, .routine = 0xC06CBD },
    { 0x76, assign, .variable = VARIABLE(1), .value = CONSTANT(1), .width = WORDS, .length = 2, .routine = 0xC06CD6 },
    { 0x77, assign, .variable = VARIABLE(1), .value = CONSTANT(0), .width = BYTES, .length = 2, .routine = 0xC06CF0 },

    { 0x5B, add, .variable = VARIABLE(2), .amount = OPERAND(1), .width = BYTES, .length = 3, .routine = 0xC06A1F },
    { 0x5D, add, .variable = VARIABLE(2), .amount = VARIABLE_WORD(1), .width = BYTES, .length = 3,
      .routine = 0xC06A45 },
    { 0x5E, add, .variable = VARIABLE(2), .amount = VARIABLE_WORD(1), .width = WORDS, .length = 3,
      .routine = 0xC06A74 },
    { 0x5F, subtract, .variable = VARIABLE(2), .amount = OPERAND(1), .width = BYTES, .length = 3, .routine = 0xC06AA3 },
    { 0x60, subtract, .variable = VARIABLE(3), .amount = OPERAND_WORD(1), .width = WORDS, .length = 4,
      .routine = 0xC06AC9 },
    { 0x61, subtract, .variable = VARIABLE(2), .amount = VARIABLE_WORD(1), .width = BYTES, .length = 3,
      .routine = 0xC06AF1 },

    { 0x71, add, .variable = VARIABLE(1), .amount = CONSTANT(1), .width = BYTES, .length = 2, .routine = 0xC06C69 },
    { 0x72, add, .variable = VARIABLE(1), .amount = CONSTANT(1), .width = WORDS, .length = 2, .routine = 0xC06C85 },
    { 0x73, subtract, .variable = VARIABLE(1), .amount = CONSTANT(1), .width = BYTES, .length = 2,
      .routine = 0xC06CA1 },

    { 0x63, set_flags, .variable = VARIABLE(2), .flags = BIT(1), .length = 3, .routine = 0xC06B20 },
    { 0x69, set_flags, .variable = VARIABLE(2), .flags = OPERAND(1), .length = 3, .routine = 0xC06BF7 },
    { 0x64, keep_only_flags, .variable = VARIABLE(2), .flags = ALL_BUT_BIT(1), .length = 3, .routine = 0xC06B4B },
    { 0x67, keep_only_flags, .variable = VARIABLE(2), .flags = OPERAND(1), .length = 3, .routine = 0xC06BD2 },
    { 0x6B, toggle_flags, .variable = VARIABLE(2), .flags = OPERAND(1), .length = 3, .routine = 0xC06C1C },
    { 0x6F, halve, .variable = VARIABLE(2), .times = OPERAND(1), .length = 3, .routine = 0xC06C41 },
    { 0x7F, roll, .variable = VARIABLE(1), .length = 2, .routine = 0xC06D09 });
