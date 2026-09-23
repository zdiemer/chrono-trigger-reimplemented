/* Event commands: assigning to and calculating with the script variables.
 *
 * A variable is a word at $7F:0200 + 2 * its number (event_variables); the
 * 8-bit commands work on its low byte. A command keeps its first operand in
 * the scratch cell at $D9 (a byte, or the word it works with), which the
 * handlers still write. */
#include "event/bridge.h"
#include "event/event_data.h"

/* The scratch cell's first operand, as the 8-bit commands keep it. The
 * operands after it are read once it is kept, as the original reads them: an
 * operand past the end of bank $7F is read through the mirror of low RAM,
 * where it can be the scratch cell. */
static uint8_t keep_value(GameState *g, uint8_t v)
{
    keep8(g, SCRATCH_D9, v);
    return v;
}

// $C0:6788
/* $47: set how far down the screen the objects' animation updates may run
 * each frame (the low byte of field_animation_line_limit; the scanline the
 * update loop stops at, $78 after a map loads). */
static EventStep set_animation_line_limit(GameState *g, const EventCommand *c)
{
    set_field_animation_line_limit(g, (uint16_t)((field_animation_line_limit(g) & 0xFF00) | operand(g, c, 1)));
    return next_command(c, 2, 2);
}

// $C0:68A5
/* $4F: variable (operand 2) = operand 1. */
static EventStep var_set(GameState *g, const EventCommand *c)
{
    uint8_t value = keep_value(g, operand(g, c, 1));
    set_var8(g, operand(g, c, 2), value);
    return next_command(c, 3, 1);
}

// $C0:68C6
/* $50: 16-bit variable (operand 3) = operands 1-2. */
static EventStep var_set16(GameState *g, const EventCommand *c)
{
    uint16_t value = operand_word(g, c, 1);
    keep16(g, SCRATCH_D9, value);
    set_var16(g, operand(g, c, 3), value);
    return next_command(c, 4, 1);
}

/* The variable operand 1 names, as the two-variable commands keep it (a word). */
static uint16_t keep_first_variable(GameState *g, const EventCommand *c)
{
    uint16_t value = var16(g, variable_operand(g, c, 1));
    keep16(g, SCRATCH_D9, value);
    return value;
}

// $C0:68E9
/* $51: variable (operand 2) = variable (operand 1). */
static EventStep var_copy(GameState *g, const EventCommand *c)
{
    uint8_t value = (uint8_t)keep_first_variable(g, c);
    set_var8(g, variable_operand(g, c, 2), value);
    return next_command(c, 3, 2);
}

// $C0:6913
/* $52: 16-bit variable (operand 2) = 16-bit variable (operand 1). */
static EventStep var_copy16(GameState *g, const EventCommand *c)
{
    uint16_t value = keep_first_variable(g, c);
    set_var16(g, variable_operand(g, c, 2), value);
    return next_command(c, 3, 2);
}

/* Variable v += d (8-bit), or -= d; the step leaves the operation's overflow. */
static EventStep add8(GameState *g, const EventCommand *c, uint8_t v, uint8_t d, bool subtract)
{
    uint8_t a = var8(g, v);
    uint8_t b = subtract ? (uint8_t)~d : d;
    uint8_t sum = (uint8_t)(a + b + subtract);
    set_var8(g, v, sum);
    return with_overflow(next_command(c, 3, 3), overflows8(a, b, sum));
}

static EventStep add16(GameState *g, const EventCommand *c, uint8_t v, uint16_t d, bool subtract,
                       uint16_t length)
{
    uint16_t a = var16(g, v);
    uint16_t b = subtract ? (uint16_t)~d : d;
    uint16_t sum = (uint16_t)(a + b + subtract);
    set_var16(g, v, sum);
    return with_overflow(next_command(c, length, length), overflows16(a, b, sum));
}

// $C0:6A1F
/* $5B: variable (operand 2) += operand 1. */
static EventStep var_add(GameState *g, const EventCommand *c)
{
    uint8_t d = keep_value(g, operand(g, c, 1));
    return add8(g, c, operand(g, c, 2), d, false);
}

// $C0:6A45
/* $5D: variable (operand 2) += variable (operand 1). */
static EventStep var_add_var(GameState *g, const EventCommand *c)
{
    uint8_t d = (uint8_t)keep_first_variable(g, c);
    return add8(g, c, variable_operand(g, c, 2), d, false);
}

// $C0:6A74
/* $5E: 16-bit variable (operand 2) += 16-bit variable (operand 1). */
static EventStep var_add_var16(GameState *g, const EventCommand *c)
{
    uint16_t d = keep_first_variable(g, c);
    return add16(g, c, variable_operand(g, c, 2), d, false, 3);
}

// $C0:6AA3
/* $5F: variable (operand 2) -= operand 1. */
static EventStep var_subtract(GameState *g, const EventCommand *c)
{
    uint8_t d = keep_value(g, operand(g, c, 1));
    return add8(g, c, operand(g, c, 2), d, true);
}

// $C0:6AC9
/* $60: 16-bit variable (operand 3) -= operands 1-2. */
static EventStep var_subtract16(GameState *g, const EventCommand *c)
{
    uint16_t value = operand_word(g, c, 1);
    keep16(g, SCRATCH_D9, value);
    return add16(g, c, operand(g, c, 3), value, true, 4);
}

// $C0:6AF1
/* $61: variable (operand 2) -= variable (operand 1). */
static EventStep var_subtract_var(GameState *g, const EventCommand *c)
{
    uint8_t d = (uint8_t)keep_first_variable(g, c);
    return add8(g, c, variable_operand(g, c, 2), d, true);
}

// $C0:6B20
/* $63: set bit (operand 1) of variable (operand 2). */
static EventStep var_set_bit(GameState *g, const EventCommand *c)
{
    uint8_t mask = keep_value(g, bit_mask(g, operand(g, c, 1)));
    uint8_t v = variable_operand(g, c, 2);
    set_var8(g, v, (uint8_t)(var8(g, v) | mask));
    return next_command(c, 3, 3);
}

// $C0:6B4B
/* $64: clear bit (operand 1) of variable (operand 2). */
static EventStep var_clear_bit(GameState *g, const EventCommand *c)
{
    uint8_t mask = keep_value(g, bit_clear_mask(g, operand(g, c, 1)));
    uint8_t v = variable_operand(g, c, 2);
    set_var8(g, v, (uint8_t)(var8(g, v) & mask));
    return next_command(c, 3, 3);
}

typedef enum { AND, OR, XOR } Logic;

/* Variable (operand 2) combined with operand 1. */
static EventStep var_logic(GameState *g, const EventCommand *c, Logic op)
{
    uint8_t d = keep_value(g, operand(g, c, 1));
    uint8_t v = variable_operand(g, c, 2);
    uint8_t a = var8(g, v);
    set_var8(g, v, (uint8_t)(op == AND ? a & d : op == OR ? a | d : a ^ d));
    return next_command(c, 3, 3);
}

// $C0:6BD2
/* $67: variable (operand 2) &= operand 1. */
static EventStep var_and(GameState *g, const EventCommand *c) { return var_logic(g, c, AND); }

// $C0:6BF7
/* $69: variable (operand 2) |= operand 1. */
static EventStep var_or(GameState *g, const EventCommand *c) { return var_logic(g, c, OR); }

// $C0:6C1C
/* $6B: variable (operand 2) ^= operand 1. */
static EventStep var_xor(GameState *g, const EventCommand *c) { return var_logic(g, c, XOR); }

// $C0:6C41
/* $6F: variable (operand 2) >>= operand 1, where 0 shifts 256 times. The
 * scratch cell counts the shifts down to 0. */
static EventStep var_shift_right(GameState *g, const EventCommand *c)
{
    uint8_t count = keep_value(g, operand(g, c, 1));
    unsigned shifts = count ? count : 256;
    uint8_t v = variable_operand(g, c, 2);
    keep8(g, SCRATCH_D9, 0);
    set_var8(g, v, shifts >= 8 ? 0 : (uint8_t)(var8(g, v) >> shifts));
    return next_command(c, 3, 3);
}

// $C0:6C69
/* $71: variable (operand 1) += 1. */
static EventStep var_increment(GameState *g, const EventCommand *c)
{
    uint8_t v = variable_operand(g, c, 1);
    set_var8(g, v, (uint8_t)(var8(g, v) + 1));
    return next_command(c, 2, 2);
}

// $C0:6C85
/* $72: 16-bit variable (operand 1) += 1. */
static EventStep var_increment16(GameState *g, const EventCommand *c)
{
    uint8_t v = variable_operand(g, c, 1);
    set_var16(g, v, (uint16_t)(var16(g, v) + 1));
    return next_command(c, 2, 2);
}

// $C0:6CA1
/* $73: variable (operand 1) -= 1. */
static EventStep var_decrement(GameState *g, const EventCommand *c)
{
    uint8_t v = variable_operand(g, c, 1);
    set_var8(g, v, (uint8_t)(var8(g, v) - 1));
    return next_command(c, 2, 2);
}

// $C0:6CBD
/* $75: variable (operand 1) = 1. */
static EventStep var_set_1(GameState *g, const EventCommand *c)
{
    set_var8(g, variable_operand(g, c, 1), 1);
    return next_command(c, 2, 2);
}

// $C0:6CD6
/* $76: 16-bit variable (operand 1) = 1. */
static EventStep var_set16_1(GameState *g, const EventCommand *c)
{
    set_var16(g, variable_operand(g, c, 1), 1);
    return next_command(c, 2, 2);
}

// $C0:6CF0
/* $77: variable (operand 1) = 0. */
static EventStep var_clear(GameState *g, const EventCommand *c)
{
    set_var8(g, variable_operand(g, c, 1), 0);
    return next_command(c, 2, 2);
}

// $C0:6D09
/* $7F: variable (operand 1) = the next of the game's random bytes. */
static EventStep var_random(GameState *g, const EventCommand *c)
{
    uint8_t i = (uint8_t)(field_random_index(g) + 1);
    set_field_random_index(g, i);
    uint8_t value = keep_value(g, random_byte(g, i));
    set_var8(g, variable_operand(g, c, 1), value);
    return next_command(c, 2, 2);
}

EVENT_COMMAND(event_cmd_set_animation_line_limit, set_animation_line_limit)
EVENT_COMMAND(event_cmd_var_set, var_set)
EVENT_COMMAND(event_cmd_var_set16, var_set16)
EVENT_COMMAND(event_cmd_var_copy, var_copy)
EVENT_COMMAND(event_cmd_var_copy16, var_copy16)
EVENT_COMMAND(event_cmd_var_add, var_add)
EVENT_COMMAND(event_cmd_var_add_var, var_add_var)
EVENT_COMMAND(event_cmd_var_add_var16, var_add_var16)
EVENT_COMMAND(event_cmd_var_subtract, var_subtract)
EVENT_COMMAND(event_cmd_var_subtract16, var_subtract16)
EVENT_COMMAND(event_cmd_var_subtract_var, var_subtract_var)
EVENT_COMMAND(event_cmd_var_set_bit, var_set_bit)
EVENT_COMMAND(event_cmd_var_clear_bit, var_clear_bit)
EVENT_COMMAND(event_cmd_var_and, var_and)
EVENT_COMMAND(event_cmd_var_or, var_or)
EVENT_COMMAND(event_cmd_var_xor, var_xor)
EVENT_COMMAND(event_cmd_var_shift_right, var_shift_right)
EVENT_COMMAND(event_cmd_var_increment, var_increment)
EVENT_COMMAND(event_cmd_var_increment16, var_increment16)
EVENT_COMMAND(event_cmd_var_decrement, var_decrement)
EVENT_COMMAND(event_cmd_var_set_1, var_set_1)
EVENT_COMMAND(event_cmd_var_set16_1, var_set16_1)
EVENT_COMMAND(event_cmd_var_clear, var_clear)
EVENT_COMMAND(event_cmd_var_random, var_random)
