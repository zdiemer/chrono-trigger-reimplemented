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
#include "vars.h"

// $C1:4CF0
/* The channel stops for good. */
ScriptStep script_end(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)form;
    set_battle_script_running(g, ch.channel, 0);
    return script_moved(false);
}

// $C1:4CF7
/* The channel holds here, and while the numbers over the combatants aren't
 * showing, counts a signal up each frame, which the battle's wait for the
 * scripts watches. The language's unused bytes do the same. */
ScriptStep script_halt(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)ch, (void)form;
    if (battle_number_step(g) != 0)
        return script_stays();
    set_battle_script_signal(g, (uint8_t)(battle_script_signal(g) + 1));
    return script_moved(false);
}

// $C1:56CC
/* The channel goes on in a subroutine of the scripts', coming back after the
 * command. The return keeps the byte after the pointer with the bank, as the
 * original's word does, and the subroutine starts with it clear. */
enum { CALL_LENGTH = 2 };

ScriptStep script_call(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)form;
    uint32_t at = battle_script_pointer(g);
    uint32_t back = (at & 0xFFFF) + CALL_LENGTH;
    uint16_t bank = (uint16_t)((at >> 16) | battle_script_pointer_pad(g) << 8);
    if (back > 0xFFFF)
        bank++;
    set_battle_script_returns_place(g, ch.channel, (uint16_t)back);
    set_battle_script_returns_bank(g, ch.channel, (uint8_t)bank);
    set_battle_script_returns_pad(g, ch.channel, (uint8_t)(bank >> 8));
    set_battle_script_pointer(g, battle_script_subroutine(g, script_operand(g, 1)));
    set_battle_script_pointer_pad(g, 0);
    return script_moved(true);
}

static void go_back(GameState *g, uint8_t channel)
{
    set_battle_script_pointer(g, (uint32_t)battle_script_returns_bank(g, channel) << 16 | battle_script_returns_place(g, channel));
    set_battle_script_pointer_pad(g, battle_script_returns_pad(g, channel));
}

// $C1:56FB
ScriptStep script_return(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)form;
    go_back(g, ch.channel);
    return script_moved(true);
}

// $C1:5722
/* Back from the subroutine where the point is at the top, or on past the
 * command, its frame ending. */
ScriptStep script_return_unless_point_low(GameState *g, ScriptChannel ch, uint8_t form)
{
    if (battle_script_point_y(g) == 0)
        return script_return(g, ch, form);
    return script_next_frame(1);
}

// $C1:5714
/* The channel sits out as many frames as the operand says. */
ScriptStep script_wait(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)form;
    set_battle_script_waits(g, ch.channel, script_operand(g, 1));
    return script_next_frame(2);
}

// $C1:5D18
/* Nothing, past an operand. */
ScriptStep script_skip(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)g, (void)ch, (void)form;
    return script_next_frame(2);
}

// $C1:5769
/* The channel ends where the other targets have no target at the operand's
 * place in their list. */
ScriptStep script_end_unless_listed(GameState *g, ScriptChannel ch, uint8_t form)
{
    uint8_t target;
    if (!script_listed(g, OTHER_TARGETS_START + script_operand(g, 1), &target))
        return script_end(g, ch, form);
    return script_next(2);
}

/* ---- The values ------------------------------------------------------------------------ */

/* A variable by the number an operand names: the original indexes on from
 * the first, so a number past the 27 reaches the values after them. */
static uint8_t variable(const GameState *g, uint8_t n)
{
    return n < BATTLE_SCRIPT_VARIABLES_COUNT ? battle_script_variables(g, n) : battle_script_variables_unchecked(g, n);
}
static void set_variable(GameState *g, uint8_t n, uint8_t v)
{
    if (n < BATTLE_SCRIPT_VARIABLES_COUNT)
        set_battle_script_variables(g, n, v);
    else
        set_battle_script_variables_unchecked(g, n, v);
}

static uint8_t value_of(const GameState *g, ScriptValue which, uint8_t n)
{
    switch (which) {
    case SCRIPT_VARIABLE: return variable(g, n);
    case SCRIPT_MARK_A: return battle_script_marks(g, 0);
    case SCRIPT_MARK_B: return battle_script_marks(g, 1);
    case SCRIPT_POINT_X: return battle_script_point_x(g);
    default: return battle_script_point_y(g);
    }
}
static void set_value_of(GameState *g, ScriptValue which, uint8_t n, uint8_t v)
{
    switch (which) {
    case SCRIPT_VARIABLE: set_variable(g, n, v); break;
    case SCRIPT_MARK_A: set_battle_script_marks(g, 0, v); break;
    case SCRIPT_MARK_B: set_battle_script_marks(g, 1, v); break;
    case SCRIPT_POINT_X: set_battle_script_point_x(g, v); break;
    default: set_battle_script_point_y(g, v); break;
    }
}

/* A variable is named by the first operand, which the command's other
 * operands then follow; a mark needs no name. */
static uint8_t named(const GameState *g, ScriptValue which) { return which == SCRIPT_VARIABLE ? script_operand(g, 1) : 0; }
static uint8_t after_name(ScriptValue which) { return which == SCRIPT_VARIABLE ? 2 : 1; }

// $C1:572C, $C1:5743, $C1:5756
/* The channel waits until the value is the operand's, then goes on. */
ScriptStep script_wait_for_value(GameState *g, ScriptChannel ch, uint8_t value)
{
    (void)ch;
    ScriptValue which = (ScriptValue)value;
    uint8_t at = after_name(which);
    if (value_of(g, which, named(g, which)) != script_operand(g, at))
        return script_stays();
    return script_next((uint8_t)(at + 1));
}

// $C1:58EC, $C1:58FE, $C1:590C
ScriptStep script_set_value(GameState *g, ScriptChannel ch, uint8_t value)
{
    (void)ch;
    ScriptValue which = (ScriptValue)value;
    uint8_t at = after_name(which);
    set_value_of(g, which, named(g, which), script_operand(g, at));
    return script_next((uint8_t)(at + 1));
}

// $C1:5931, $C1:5940, $C1:594B
ScriptStep script_count_up(GameState *g, ScriptChannel ch, uint8_t value)
{
    (void)ch;
    ScriptValue which = (ScriptValue)value;
    uint8_t n = named(g, which);
    set_value_of(g, which, n, (uint8_t)(value_of(g, which, n) + 1));
    return script_next(after_name(which));
}

// $C1:5956, $C1:5965, $C1:5970
ScriptStep script_count_down(GameState *g, ScriptChannel ch, uint8_t value)
{
    (void)ch;
    ScriptValue which = (ScriptValue)value;
    uint8_t n = named(g, which);
    set_value_of(g, which, n, (uint8_t)(value_of(g, which, n) - 1));
    return script_next(after_name(which));
}

// $C1:597B, $C1:5991
/* The operand added to the value, a byte's worth. */
ScriptStep script_add_to_value(GameState *g, ScriptChannel ch, uint8_t value)
{
    (void)ch;
    ScriptValue which = (ScriptValue)value;
    uint8_t n = named(g, which), at = after_name(which);
    set_value_of(g, which, n, (uint8_t)(value_of(g, which, n) + script_operand(g, at)));
    return script_next((uint8_t)(at + 1));
}

// $C1:59A3
ScriptStep script_add_variable_to_mark_a(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)ch, (void)form;
    set_battle_script_marks(g, 0, (uint8_t)(battle_script_marks(g, 0) + variable(g, script_operand(g, 1))));
    return script_next(2);
}

// $C1:59E4
/* The variable the first operand names, into the one the second names. */
ScriptStep script_copy_variable(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)ch, (void)form;
    set_variable(g, script_operand(g, 2), variable(g, script_operand(g, 1)));
    return script_next(3);
}

// $C1:59FA
ScriptStep script_store_mark_a(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)ch, (void)form;
    set_variable(g, script_operand(g, 1), battle_script_marks(g, 0));
    return script_next(2);
}

// $C1:5A70
/* A signed amount added to a variable, held within a byte: a sum past its most
 * stops there, and one taken below nothing stops at nothing. The original
 * keeps the amount in the first work cell, which a later command reads as it
 * finds it (the trail's periods, battle/script_poses.c). */
enum { HELD_MOST = 0xFF, HELD_LEAST = 0x00, AMOUNT_NEGATIVE = 0x80 };

ScriptStep script_add_held(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)ch, (void)form;
    uint8_t n = script_operand(g, 1), amount = script_operand(g, 2);
    set_battle_work(g, 0, amount);
    unsigned sum = variable(g, n) + amount;
    uint8_t held;
    if (!(amount & AMOUNT_NEGATIVE))
        held = sum > 0xFF ? HELD_MOST : (uint8_t)sum;
    else
        held = sum > 0xFF ? (uint8_t)sum : HELD_LEAST;
    set_variable(g, n, held);
    return script_next(3);
}

// $C1:5A99, $C1:5AAF, $C1:5AC5
/* Mark a and another value change places. */
ScriptStep script_swap_with_mark_a(GameState *g, ScriptChannel ch, uint8_t value)
{
    (void)ch;
    ScriptValue which = (ScriptValue)value;
    uint8_t a = battle_script_marks(g, 0);
    set_battle_script_marks(g, 0, value_of(g, which, 0));
    set_value_of(g, which, 0, a);
    return script_next(1);
}
