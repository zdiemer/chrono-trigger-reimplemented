/* Battle: the animation scripts' flow and their own values -- how a channel
 * ends, waits, calls a subroutine and comes back, and the variables, marks and
 * point its commands keep and test.
 *
 * A script has one return place a channel (a subroutine calling another
 * replaces it), and a handful of values the channels share: 27 variables a
 * command names by number, two marks with commands of their own, and a point
 * the placing commands leave (battle/script_language.h). The waiting commands
 * stay on themselves until a value is what they ask, which is how the
 * channels keep in step with each other and with the battle. */
#include "battle/script_language.h"

#include "battle/animation_data.h"
#include "original_effects.h"
#include "vars.h"

/* ---- Flow ------------------------------------------------------------------------------ */

// $C1:4CF0
/* The channel stops for good. */
static ScriptFlow end(GameState *g, ScriptChannel channel)
{
    set_battle_script_running(g, channel.channel, 0);
    return SCRIPT_STAY;
}
SCRIPT_OPERATION(end, channel)

// $C1:4CF7
/* The channel holds here, and while the numbers over the combatants aren't
 * showing, counts a signal up each frame, which the battle's wait for the
 * scripts watches. The language's unused bytes do the same. */
static ScriptFlow halt(GameState *g)
{
    if (battle_number_step(g) == 0)
        set_battle_script_signal(g, (uint8_t)(battle_script_signal(g) + 1));
    return SCRIPT_STAY;
}
SCRIPT_OPERATION_TAKING_NOTHING(halt)

/* A channel's return place: where its script goes on after a subroutine. */
static ScriptPlace return_place(const GameState *g, uint8_t channel)
{
    return (ScriptPlace){ (uint32_t)battle_script_returns_bank(g, channel) << 16 | battle_script_returns_place(g, channel),
                          battle_script_returns_pad(g, channel) };
}
static void set_return_place(GameState *g, uint8_t channel, ScriptPlace place)
{
    set_battle_script_returns_place(g, channel, (uint16_t)place.at);
    set_battle_script_returns_bank(g, channel, (uint8_t)(place.at >> 16));
    set_battle_script_returns_pad(g, channel, place.beyond);
}

// $C1:56CC
/* The channel goes on in a subroutine of the scripts', coming back after the
 * command; the subroutine starts in its own bank. */
static ScriptFlow call(GameState *g, ScriptChannel channel, uint8_t length, uint8_t subroutine)
{
    set_return_place(g, channel.channel, script_place_plus(script_place(g), length));
    set_script_place(g, (ScriptPlace){ battle_script_subroutine(g, subroutine), 0 });
    return SCRIPT_MOVED;
}
SCRIPT_OPERATION(call, channel, length, subroutine)

// $C1:56FB
/* The channel comes back from its subroutine, after the call. */
static ScriptFlow return_from_call(GameState *g, ScriptChannel channel)
{
    set_script_place(g, return_place(g, channel.channel));
    return SCRIPT_MOVED;
}
SCRIPT_OPERATION(return_from_call, channel)

// $C1:5722
/* Back from the subroutine where the point is at the top of the screen (its
 * Y 0), or on past the command, its frame ending. */
static ScriptFlow return_at_the_top(GameState *g, ScriptChannel channel)
{
    if (battle_script_point_y(g) == 0)
        return return_from_call(g, channel);
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(return_at_the_top, channel)

// $C1:5714
/* The channel sits out as many frames as the operand says. */
static ScriptFlow wait_frames(GameState *g, ScriptChannel channel, uint8_t frames)
{
    set_battle_script_waits(g, channel.channel, frames);
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(wait_frames, channel, frames)

// $C1:5D18
/* Nothing, past an operand: the channel's frame ends. */
static ScriptFlow skip(GameState *g)
{
    (void)g;
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION_TAKING_NOTHING(skip)

bool script_ends_unless_listed(const GameState *g, uint8_t listed)
{
    uint8_t target;
    return !script_listed(g, OTHER_TARGETS_START + listed, &target);
}

// $C1:5769
/* The channel ends where the other targets have no target at the operand's
 * place in their list. */
static ScriptFlow end_unless_listed(GameState *g, ScriptChannel channel, uint8_t listed)
{
    if (script_ends_unless_listed(g, listed))
        return end(g, channel);
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(end_unless_listed, channel, listed)

/* ---- The values ------------------------------------------------------------------------ */

/* A variable by the number an operand names: the original indexes on from
 * the first, so a number past the 27 reaches the values after them. */
static uint8_t variable_value(const GameState *g, uint8_t n)
{
    return n < BATTLE_SCRIPT_VARIABLES_COUNT ? battle_script_variables(g, n) : battle_script_variables_unchecked(g, n);
}
static void set_variable_value(GameState *g, uint8_t n, uint8_t v)
{
    if (n < BATTLE_SCRIPT_VARIABLES_COUNT)
        set_battle_script_variables(g, n, v);
    else
        set_battle_script_variables_unchecked(g, n, v);
}

/* A value of the script's: a variable (by its number), a mark or the point's
 * X or Y. */
static uint8_t value_of(const GameState *g, ScriptValue which, uint8_t variable)
{
    switch (which) {
    case SCRIPT_VARIABLE: return variable_value(g, variable);
    case SCRIPT_MARK_A: return battle_script_marks(g, 0);
    case SCRIPT_MARK_B: return battle_script_marks(g, 1);
    case SCRIPT_POINT_X: return battle_script_point_x(g);
    default: return battle_script_point_y(g);
    }
}
static void set_value_of(GameState *g, ScriptValue which, uint8_t variable, uint8_t v)
{
    switch (which) {
    case SCRIPT_VARIABLE: set_variable_value(g, variable, v); break;
    case SCRIPT_MARK_A: set_battle_script_marks(g, 0, v); break;
    case SCRIPT_MARK_B: set_battle_script_marks(g, 1, v); break;
    case SCRIPT_POINT_X: set_battle_script_point_x(g, v); break;
    default: set_battle_script_point_y(g, v); break;
    }
}

// $C1:572C, $C1:5743, $C1:5756
/* The channel waits until the value is the operand's, then goes on: how one
 * channel waits for another to reach a point in its script. */
static ScriptFlow wait_for_value(GameState *g, ScriptValue which, uint8_t variable, uint8_t value)
{
    return value_of(g, which, variable) == value ? SCRIPT_NEXT : SCRIPT_STAY;
}
SCRIPT_OPERATION(wait_for_value, which, variable, value)

// $C1:58EC, $C1:58FE, $C1:590C
/* The value takes the operand: a signal to the channels waiting on it. */
static ScriptFlow set_value(GameState *g, ScriptValue which, uint8_t variable, uint8_t value)
{
    set_value_of(g, which, variable, value);
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(set_value, which, variable, value)

// $C1:597B, $C1:5991, and the counts up and down (the amount fixed by their rows)
// $C1:5931, $C1:5940, $C1:594B, $C1:5956, $C1:5965, $C1:5970
/* The value goes up by the amount, a byte's worth (down, by an amount
 * of $FF): a count the script keeps, of hits or of frames. */
static ScriptFlow add_to_value(GameState *g, ScriptValue which, uint8_t variable, uint8_t amount)
{
    set_value_of(g, which, variable, (uint8_t)(value_of(g, which, variable) + amount));
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(add_to_value, which, variable, amount)

// $C1:59A3
/* Mark a goes up by a variable's value. */
static ScriptFlow add_variable_to_mark_a(GameState *g, uint8_t variable)
{
    set_battle_script_marks(g, 0, (uint8_t)(battle_script_marks(g, 0) + variable_value(g, variable)));
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(add_variable_to_mark_a, variable)

// $C1:59E4
/* The variable the first operand names is copied into the one the second
 * names. */
static ScriptFlow copy_variable(GameState *g, uint8_t variable, uint8_t other)
{
    set_variable_value(g, other, variable_value(g, variable));
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(copy_variable, variable, other)

// $C1:59FA
/* Mark a is kept in a variable. */
static ScriptFlow store_mark_a(GameState *g, uint8_t variable)
{
    set_variable_value(g, variable, battle_script_marks(g, 0));
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(store_mark_a, variable)

// $C1:5A70
/* A signed amount added to a variable, held within a byte: a sum past its
 * most stops there, and one taken below nothing stops at nothing. The
 * original leaves the amount in the first work cell. */
enum { HELD_MOST = 0xFF, HELD_LEAST = 0x00, LEFT_AMOUNT = 0x00 };

static ScriptFlow add_held(GameState *g, uint8_t variable, uint8_t amount)
{
    ORIGINAL_EFFECT(script_leave(g, LEFT_AMOUNT, amount));
    int sum = variable_value(g, variable) + (int8_t)amount;
    set_variable_value(g, variable, (uint8_t)(sum > HELD_MOST ? HELD_MOST : sum < HELD_LEAST ? HELD_LEAST : sum));
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(add_held, variable, amount)

// $C1:5A99, $C1:5AAF, $C1:5AC5
/* Mark a and another value change places. */
static ScriptFlow swap_with_mark_a(GameState *g, ScriptValue which)
{
    uint8_t a = battle_script_marks(g, 0);
    set_battle_script_marks(g, 0, value_of(g, which, 0));
    set_value_of(g, which, 0, a);
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(swap_with_mark_a, which)

/* ---- The table ------------------------------------------------------------------------- */

#define HALT(b) { b, OPERATION(halt), .length = 1 }
#define BY_ONE CONSTANT(1)
#define BY_MINUS_ONE CONSTANT(0xFF)

SCRIPT_OPCODES(script_flow_opcodes,
    { 0x00, OPERATION(end), .length = 1 },
    { 0x1E, OPERATION(call), .subroutine = OPERAND(1), .length = 2 },
    { 0x1F, OPERATION(return_from_call), .length = 1 },
    { 0x20, OPERATION(wait_frames), .frames = OPERAND(1), .length = 2 },
    { 0x21, OPERATION(return_at_the_top), .length = 1 },
    { 0x22, OPERATION(wait_for_value), .which = CONSTANT(SCRIPT_VARIABLE), .variable = OPERAND(1), .value = OPERAND(2), .length = 3 },
    { 0x23, OPERATION(wait_for_value), .which = CONSTANT(SCRIPT_MARK_A), .value = OPERAND(1), .length = 2 },
    { 0x24, OPERATION(wait_for_value), .which = CONSTANT(SCRIPT_MARK_B), .value = OPERAND(1), .length = 2 },
    { 0x25, OPERATION(end_unless_listed), .listed = OPERAND(1), .length = 2 },
    { 0x5F, OPERATION(skip), .length = 2 },

    { 0x30, OPERATION(set_value), .which = CONSTANT(SCRIPT_VARIABLE), .variable = OPERAND(1), .value = OPERAND(2), .length = 3 },
    { 0x31, OPERATION(set_value), .which = CONSTANT(SCRIPT_MARK_A), .value = OPERAND(1), .length = 2 },
    { 0x32, OPERATION(set_value), .which = CONSTANT(SCRIPT_MARK_B), .value = OPERAND(1), .length = 2 },
    { 0x34, OPERATION(add_to_value), .which = CONSTANT(SCRIPT_VARIABLE), .variable = OPERAND(1), .amount = BY_ONE, .length = 2 },
    { 0x35, OPERATION(add_to_value), .which = CONSTANT(SCRIPT_MARK_A), .amount = BY_ONE, .length = 1 },
    { 0x36, OPERATION(add_to_value), .which = CONSTANT(SCRIPT_MARK_B), .amount = BY_ONE, .length = 1 },
    { 0x37, OPERATION(add_to_value), .which = CONSTANT(SCRIPT_VARIABLE), .variable = OPERAND(1), .amount = BY_MINUS_ONE, .length = 2 },
    { 0x38, OPERATION(add_to_value), .which = CONSTANT(SCRIPT_MARK_A), .amount = BY_MINUS_ONE, .length = 1 },
    { 0x39, OPERATION(add_to_value), .which = CONSTANT(SCRIPT_MARK_B), .amount = BY_MINUS_ONE, .length = 1 },
    { 0x3A, OPERATION(add_to_value), .which = CONSTANT(SCRIPT_VARIABLE), .variable = OPERAND(1), .amount = OPERAND(2), .length = 3 },
    { 0x3B, OPERATION(add_to_value), .which = CONSTANT(SCRIPT_MARK_A), .amount = OPERAND(1), .length = 2 },
    { 0x3C, OPERATION(add_variable_to_mark_a), .variable = OPERAND(1), .length = 2 },
    { 0x41, OPERATION(copy_variable), .variable = OPERAND(1), .other = OPERAND(2), .length = 3 },
    { 0x42, OPERATION(store_mark_a), .variable = OPERAND(1), .length = 2 },
    { 0x49, OPERATION(add_held), .variable = OPERAND(1), .amount = OPERAND(2), .length = 3 },
    { 0x4A, OPERATION(swap_with_mark_a), .which = CONSTANT(SCRIPT_MARK_B), .length = 1 },
    { 0x4D, OPERATION(swap_with_mark_a), .which = CONSTANT(SCRIPT_MARK_B), .length = 1 },
    { 0x4E, OPERATION(swap_with_mark_a), .which = CONSTANT(SCRIPT_POINT_X), .length = 1 },
    { 0x4F, OPERATION(swap_with_mark_a), .which = CONSTANT(SCRIPT_POINT_Y), .length = 1 },

    /* $01, and the bytes no script uses that the original's table gives the
       same handler */
    HALT(0x01), HALT(0x2F), HALT(0x4B), HALT(0x4C), HALT(0x56), HALT(0x57), HALT(0x58), HALT(0x59), HALT(0x5A),
    HALT(0x5B), HALT(0x5C), HALT(0x7E), HALT(0x7F), HALT(0x86), HALT(0x87), HALT(0x88), HALT(0x89), HALT(0x8A),
    HALT(0x8B), HALT(0x8C), HALT(0x8D), HALT(0x8E), HALT(0x8F), HALT(0x90), HALT(0x91), HALT(0x92), HALT(0x93),
    HALT(0x94), HALT(0x95), HALT(0x96), HALT(0x97), HALT(0x9E), HALT(0x9F), HALT(0xA0), HALT(0xA1), HALT(0xA3),
    HALT(0xA6), HALT(0xA7), HALT(0xAA), HALT(0xAB), HALT(0xAC), HALT(0xAD), HALT(0xAE), HALT(0xAF), HALT(0xB0),
    HALT(0xB1), HALT(0xB2), HALT(0xB3), HALT(0xB4), HALT(0xB5), HALT(0xB6), HALT(0xB7), HALT(0xB8), HALT(0xB9),
    HALT(0xBA), HALT(0xBB), HALT(0xBC), HALT(0xBD), HALT(0xBE), HALT(0xBF), HALT(0xC6), HALT(0xC7), HALT(0xC8),
    HALT(0xC9), HALT(0xCA), HALT(0xCB), HALT(0xCC), HALT(0xCD), HALT(0xCE), HALT(0xCF));
