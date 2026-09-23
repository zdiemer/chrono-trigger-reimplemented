/* Event commands: control flow within a script -- jumps, conditions, waits.
 *
 * A jump is relative to its own operand, the jump byte: forward by it, or
 * back. A condition goes on after itself when it holds, and otherwise jumps
 * forward by its last operand. The comparisons ($12-$16) keep their operands
 * in the scratch cells ($D9 left, $DB right) and the offset of their jump
 * byte in event_condition_resume, as the original does. */
#include "event/bridge.h"

// $C0:62B5
/* $10: jump forward by operand 1. */
static EventStep jump_forward(GameState *g, const EventCommand *c)
{
    return jump_from(g, c, 1, operand(g, c, 1), false, 1);
}

// $C0:62CB
/* $11: jump back by operand 1. */
static EventStep jump_back(GameState *g, const EventCommand *c)
{
    return jump_from(g, c, 1, operand(g, c, 1), true, 1);
}

/* The comparison operators ($C0:6467 for bytes, $C0:6477 for words): equal,
 * not equal, greater, less, greater or equal, less or equal, any bits in
 * common, and (bytes only) either nonzero. The guards turn away an operator
 * past the table, which no script has. */
enum { EQUAL, NOT_EQUAL, GREATER, LESS, AT_LEAST, AT_MOST, ANY_COMMON, EITHER };

static bool compare(unsigned op, uint16_t lhs, uint16_t rhs)
{
    switch (op) {
    case EQUAL: return lhs == rhs;
    case NOT_EQUAL: return lhs != rhs;
    case GREATER: return lhs > rhs;
    case LESS: return lhs < rhs;
    case AT_LEAST: return lhs >= rhs;
    case AT_MOST: return lhs <= rhs;
    case ANY_COMMON: return (lhs & rhs) != 0;
    default: return (lhs | rhs) != 0;
    }
}

/* Compare the left and right operands the command kept, with the operator at
 * operand `length - 2`; the jump byte after it is the last operand. The
 * original doubles the operator into a table index with an 8-bit shift, which
 * drops its bit 7, and calls it with 8-bit index registers, which leaves only
 * the low byte of the parse position. */
static EventStep compare_kept(GameState *g, const EventCommand *c, unsigned op, bool words, uint16_t length,
                              uint16_t parsed)
{
    set_event_condition_resume(g, (uint16_t)(c->at + length - 1));
    uint16_t lhs = words ? scratch16(g, SCRATCH_D9) : scratch8(g, SCRATCH_D9);
    uint16_t rhs = words ? scratch16(g, SCRATCH_DB) : scratch8(g, SCRATCH_DB);
    EventStep st = branch_unless(g, c, compare(op & 0x7F, lhs, rhs), length, parsed);
    st.cursor &= 0x00FF;
    return st;
}

// $C0:62DE
/* $12: if variable (operand 1) compares with operand 2 under operator 3. */
static EventStep if_var_const(GameState *g, const EventCommand *c)
{
    keep8(g, SCRATCH_D9, var8(g, variable_operand(g, c, 1)));
    keep8(g, SCRATCH_DB, operand(g, c, 2));
    return compare_kept(g, c, operand(g, c, 3), false, 5, 2);
}

// $C0:6313
/* $13: if 16-bit variable (operand 1) compares with operands 2-3 under operator 4. */
static EventStep if_var_const16(GameState *g, const EventCommand *c)
{
    keep16(g, SCRATCH_D9, var16(g, variable_operand(g, c, 1)));
    keep16(g, SCRATCH_DB, operand_word(g, c, 2));
    return compare_kept(g, c, operand(g, c, 4), true, 6, 2);
}

// $C0:6361
/* $14: if variable (operand 1) compares with variable (operand 2) under operator 3. */
static EventStep if_var_var(GameState *g, const EventCommand *c)
{
    keep8(g, SCRATCH_D9, var8(g, variable_operand(g, c, 1)));
    keep8(g, SCRATCH_DB, var8(g, variable_operand(g, c, 2)));
    return compare_kept(g, c, operand(g, c, 3), false, 5, 3);
}

// $C0:63A4
/* $15: as $14 with 16-bit variables. */
static EventStep if_var_var16(GameState *g, const EventCommand *c)
{
    keep16(g, SCRATCH_D9, var16(g, variable_operand(g, c, 1)));
    keep16(g, SCRATCH_DB, var16(g, variable_operand(g, c, 2)));
    return compare_kept(g, c, operand(g, c, 3), true, 5, 3);
}

// $C0:63E6
/* $16: if the byte of event memory at operand 1 compares with operand 2 under
 * operator 3, whose bit 7 picks the memory's second page. The operator is
 * kept in the scratch cell at $DD. */
static EventStep if_memory(GameState *g, const EventCommand *c)
{
    keep8(g, SCRATCH_D9, operand(g, c, 1));
    keep8(g, SCRATCH_DB, operand(g, c, 2));
    uint8_t op = operand(g, c, 3);
    uint16_t page = op & 0x80 ? 0x0100 : 0x0000;
    op &= 0x7F;
    keep8(g, SCRATCH_DD, op);
    keep8(g, SCRATCH_D9, memory8(g, (uint16_t)(page | scratch8(g, SCRATCH_D9))));
    return compare_kept(g, c, op, false, 5, 1);
}

// $C0:6442
/* $18: if the first byte of event memory is below operand 1. */
static EventStep if_memory0_below(GameState *g, const EventCommand *c)
{
    return branch_unless(g, c, memory8(g, 0) < operand(g, c, 1), 3, 1);
}

// $C0:6568
/* $1A: if this object's result value (its low byte) is operand 1. */
static EventStep if_result(GameState *g, const EventCommand *c)
{
    return branch_unless(g, c, (uint8_t)event_object_result(g, current_object(g)) == operand(g, c, 1), 3, 1);
}

// $C0:662C
/* $27: if the object in operand 1 is in view (field_object_in_view). */
static EventStep if_object_in_view(GameState *g, const EventCommand *c)
{
    return branch_unless(g, c, field_object_in_view(g, object_operand(g, c, 1)) != 0, 3, 1);
}

/* How far `v` is past `from`, if that is 2 up to `limit`: the overflow of the
 * subtraction is what the step leaves. */
static bool within(uint8_t v, uint8_t from, uint8_t limit, bool *overflow)
{
    uint8_t d = (uint8_t)(v - from);
    *overflow = overflows8(v, (uint8_t)~from, d);
    return d >= 2 && d < limit;
}

// $C0:6655
/* $28: if the object in operand 1 stands well inside the screen: 2 to 13
 * tiles right of its left edge and 2 to 12 below its top (in 16-pixel tiles,
 * the screen's edges being in 8-pixel ones). The halved edges are kept in the
 * scratch cells. */
static EventStep if_object_on_screen(GameState *g, const EventCommand *c)
{
    unsigned object = object_operand(g, c, 1);
    uint8_t left = (uint8_t)(field_screen_left(g) >> 1), top = (uint8_t)(field_screen_top(g) >> 1);
    keep8(g, SCRATCH_DB, left);
    keep8(g, SCRATCH_DD, top);
    bool overflow;
    bool inside = within(field_object_x_tile(g, object), left, 0x0E, &overflow) &&
                  within(field_object_y_tile(g, object), top, 0x0D, &overflow);
    if (inside)
        return with_overflow(next_command(c, 3, 1), overflow);
    return branch_unless(g, c, false, 3, 1);
}

/* Count this object's frames of a wait: `frames` counted, go on after the
 * command's `length` bytes this frame; short of it, end the turn on the
 * command. A count already past `frames` starts again, and the command runs
 * again at once. */
static EventStep count_frames(GameState *g, const EventCommand *c, uint8_t frames, uint16_t length)
{
    unsigned object = current_object(g);
    uint8_t count = event_object_wait_frames(g, object);
    if (frames > count) {
        set_event_object_wait_frames(g, object, (uint8_t)(count + 1));
        return wait_here(c, 0);
    }
    set_event_object_wait_frames(g, object, 0);
    if (frames == count)
        return next_command(c, length, 0);
    return next_command(c, 0, 0);
}

// $C0:2F96
/* $AD: wait operand 1 frames. */
static EventStep wait_for(GameState *g, const EventCommand *c) { return count_frames(g, c, operand(g, c, 1), 2); }

// $C0:2FC6
/* $B9: wait 4 frames. */
static EventStep wait_4(GameState *g, const EventCommand *c) { return count_frames(g, c, 4, 1); }

// $C0:2FCA
/* $BA: wait 8 frames. */
static EventStep wait_8(GameState *g, const EventCommand *c) { return count_frames(g, c, 8, 1); }

// $C0:2FD2
/* $BC: wait 16 frames. */
static EventStep wait_16(GameState *g, const EventCommand *c) { return count_frames(g, c, 16, 1); }

// $C0:2FCE
/* $BD: wait 32 frames. */
static EventStep wait_32(GameState *g, const EventCommand *c) { return count_frames(g, c, 32, 1); }

// $C0:353F
/* $B1: end the object's turn; its script goes on after the command next frame. */
static EventStep yield(GameState *g, const EventCommand *c)
{
    (void)g;
    return end_turn(c, 1, 0);
}

// $C0:3543
/* $B2: end the object's turn on this command, for good. */
static EventStep halt(GameState *g, const EventCommand *c)
{
    (void)g;
    return wait_here(c, 0);
}

/* Back from a call-out, which returns to `returns_to`: true if the callee
 * left the machine as the command runs (event_machine_back); otherwise the
 * port stops there, declaring it, and the original carries on. */
static bool back(Snes *s, uint32_t returns_to, bool x8)
{
    if (event_machine_back(s, x8))
        return true;
    m_stop_at_own(s, returns_to, 0);
    return false;
}

// $C0:2FFD
/* $AF: run the object's state routine once ($B0), then go on after the
 * command. $B0 is a handler with call-outs of its own (movement.c), so this
 * calls it the original's way and moves its result on by the command. */
void event_cmd_run_state_routine_once(Snes *s)
{
    m_jsr(s, 0xC02FFD, 0xC03002);
    if (!back(s, 0xC03000, false))
        return;
    s->cpu.x++;
    m_rts(s);
}

EVENT_COMMAND(event_cmd_jump_forward, jump_forward)
EVENT_COMMAND(event_cmd_jump_back, jump_back)
EVENT_COMMAND(event_cmd_if_var_const, if_var_const)
EVENT_COMMAND(event_cmd_if_var_const16, if_var_const16)
EVENT_COMMAND(event_cmd_if_var_var, if_var_var)
EVENT_COMMAND(event_cmd_if_var_var16, if_var_var16)
EVENT_COMMAND(event_cmd_if_memory, if_memory)
EVENT_COMMAND(event_cmd_if_memory0_below, if_memory0_below)
EVENT_COMMAND(event_cmd_if_result, if_result)
EVENT_COMMAND(event_cmd_if_object_in_view, if_object_in_view)
EVENT_COMMAND(event_cmd_if_object_on_screen, if_object_on_screen)
EVENT_COMMAND(event_cmd_wait, wait_for)
EVENT_COMMAND(event_cmd_wait_4, wait_4)
EVENT_COMMAND(event_cmd_wait_8, wait_8)
EVENT_COMMAND(event_cmd_wait_16, wait_16)
EVENT_COMMAND(event_cmd_wait_32, wait_32)
EVENT_COMMAND(event_cmd_yield, yield)
EVENT_COMMAND(event_cmd_halt, halt)
