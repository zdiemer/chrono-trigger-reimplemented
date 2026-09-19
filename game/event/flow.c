/* Event commands: control flow within a script. */
#include "event/event.h"

#include "event/script.h"

#define MEMORY 0x7F0000u     /* byte-addressed memory tested by command $16 */

/* Scratch direct-page cells the commands use. */
enum { LHS = 0xD9, RHS = 0xDB, OPERATOR = 0xDD, RESUME = 0xC3 };

/* INY : TYX : LDA script,X : REP #$20 : AND #$00FF : ASL A : TAX
 * Read an operand byte that indexes the variables; leaves M 16-bit. */
static void load_var_index(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_rep(s, CPU_FLAG_M);
    s->cpu.a = m_asl16(s, m_lda16(s, s->cpu.a & 0x00FF));
    m_tax(s);
}

/* X = X +/- (A & $FF) the way the jump commands compute it (through A, with
 * the distance left in $D9-$DA). Ends with SEP #$20 : SEC : RTS. */
static void jump_by(Snes *s, bool back)
{
    m_rep(s, CPU_FLAG_M);
    uint16_t offset = m_lda16(s, s->cpu.a & 0x00FF);
    m_write16(s, m_dp(s, LHS), offset);
    m_txa(s);
    m_set_flag(s, CPU_FLAG_C, back);
    s->cpu.a = back ? m_sbc16(s, s->cpu.a, offset) : m_adc16(s, s->cpu.a, offset);
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C0:62B5
/* Jump forward: the next command is the operand's offset plus the operand byte. */
void event_cmd_jump_forward(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    jump_by(s, false);
}

// $C0:62CB
/* Jump back: the next command is the operand's offset minus the operand byte. */
void event_cmd_jump_back(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    jump_by(s, true);
}

/* The comparison operators, tables at $C0:6467 (8-bit) and $C0:6477
 * (16-bit), on $D9 and $DB: 0 equal, 1 not equal, 2 greater, 3 less,
 * 4 greater or equal, 5 less or equal, 6 any bits in common, 7 (8-bit only)
 * either nonzero. Each loads $D9 into A (ANDed or ORed with $DB for 6 and 7)
 * and returns carry clear if the condition holds. N and Z aren't reproduced:
 * every caller reloads X right after. */
static bool condition_holds(Snes *s, unsigned op, bool wide)
{
    uint16_t lhs, rhs;
    if (wide) {
        lhs = m_read16(s, m_dp(s, LHS));
        rhs = m_read16(s, m_dp(s, RHS));
    } else {
        lhs = m_read8(s, m_dp(s, LHS));
        rhs = m_read8(s, m_dp(s, RHS));
    }
    uint16_t a = lhs;
    bool holds;
    switch (op) {
    case 0: holds = lhs == rhs; break;
    case 1: holds = lhs != rhs; break;
    case 2: holds = lhs > rhs; break;
    case 3: holds = lhs < rhs; break;
    case 4: holds = lhs >= rhs; break;
    case 5: holds = lhs <= rhs; break;
    case 6: a = lhs & rhs; holds = a != 0; break;
    default: a = lhs | rhs; holds = a != 0; break;
    }
    if (wide)
        s->cpu.a = a;
    else
        m_set_a8(s, (uint8_t)a);
    return holds;
}

/* Shared end of the conditional commands, from the ASL A before the operator
 * call at `call_site` (a JSR (table,X)). A holds the operator byte, and $C3
 * the offset of the jump distance byte that follows it. If the condition
 * holds, continue after that byte; otherwise jump forward by it ($C0:6349). */
static void finish_conditional(Snes *s, uint32_t call_site, bool wide)
{
    uint8_t index = m_asl8(s, (uint8_t)s->cpu.a);
    m_set_a8(s, index);
    m_sep(s, CPU_FLAG_X);
    m_tax(s);
    if (index >= (wide ? 14 : 16)) {
        /* An operator outside the tables; leave it to the original. */
        m_stop_at(s, call_site, 0);
        return;
    }
    m_jsr_stack_bytes(s, (uint16_t)(call_site + 2));
    bool holds = condition_holds(s, index >> 1, wide);
    m_sep(s, CPU_FLAG_M);

    m_rep(s, CPU_FLAG_X);
    m_ldx(s, m_read16(s, m_dp(s, RESUME)));
    if (holds) {
        m_inx(s);
        m_set_flag(s, CPU_FLAG_C, true);
        m_rts(s);
        return;
    }
    m_lda8(s, script_byte(s, s->cpu.x));
    jump_by(s, false);
}

// $C0:62DE
/* If variable (operand 1) compares with constant byte (operand 2) under the
 * operator (operand 3), continue; otherwise jump forward by operand 4. */
void event_cmd_if_var_const(Snes *s)
{
    load_var_index(s);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_dp(s, LHS), m_lda8(s, m_read8(s, VARS + s->cpu.x)));
    m_iny(s);
    m_tyx(s);
    m_write8(s, m_dp(s, RHS), m_lda8(s, script_byte(s, s->cpu.x)));
    m_inx(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_inx(s);
    m_write16(s, m_dp(s, RESUME), s->cpu.x);
    finish_conditional(s, 0xC06307, false);
}

// $C0:6313
/* As $12 with a 16-bit variable and a 2-byte constant. */
void event_cmd_if_var_const16(Snes *s)
{
    load_var_index(s);
    m_write16(s, m_dp(s, LHS), m_lda16(s, m_read16(s, VARS + s->cpu.x)));
    m_iny(s);
    m_tyx(s);
    m_write16(s, m_dp(s, RHS), m_lda16(s, m_read16(s, SCRIPT + s->cpu.x)));
    m_inx(s);
    m_inx(s);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_inx(s);
    m_write16(s, m_dp(s, RESUME), s->cpu.x);
    finish_conditional(s, 0xC0633D, true);
}

// $C0:6361
/* If variable (operand 1) compares with variable (operand 2) under the
 * operator (operand 3), continue; otherwise jump forward by operand 4. */
void event_cmd_if_var_var(Snes *s)
{
    load_var_index(s);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_dp(s, LHS), m_lda8(s, m_read8(s, VARS + s->cpu.x)));
    load_var_index(s);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_dp(s, RHS), m_lda8(s, m_read8(s, VARS + s->cpu.x)));
    m_iny(s);
    m_tyx(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_inx(s);
    m_write16(s, m_dp(s, RESUME), s->cpu.x);
    finish_conditional(s, 0xC06398, false);
}

// $C0:63A4
/* As $14 with 16-bit variables. */
void event_cmd_if_var_var16(Snes *s)
{
    load_var_index(s);
    m_write16(s, m_dp(s, LHS), m_lda16(s, m_read16(s, VARS + s->cpu.x)));
    /* The second index is read with the accumulator still 16-bit. */
    m_iny(s);
    m_tyx(s);
    m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
    s->cpu.a = m_asl16(s, m_lda16(s, s->cpu.a & 0x00FF));
    m_tax(s);
    m_write16(s, m_dp(s, RHS), m_lda16(s, m_read16(s, VARS + s->cpu.x)));
    m_iny(s);
    m_tyx(s);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_inx(s);
    m_write16(s, m_dp(s, RESUME), s->cpu.x);
    finish_conditional(s, 0xC063D7, true);
}

// $C0:63E6
/* If the memory byte at $7F:0000 + operand 1 compares with constant (operand
 * 2) under the operator (operand 3, bit 7 selecting $7F:0100 + operand 1
 * instead), continue; otherwise jump forward by operand 4. */
void event_cmd_if_mem_const(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_write8(s, m_dp(s, LHS), m_lda8(s, script_byte(s, s->cpu.x)));
    m_inx(s);
    m_write8(s, m_dp(s, RHS), m_lda8(s, script_byte(s, s->cpu.x)));
    m_inx(s);
    uint8_t op = m_lda8(s, script_byte(s, s->cpu.x));
    m_inx(s);
    m_write16(s, m_dp(s, RESUME), s->cpu.x);
    uint16_t page = 0x0000;
    if (op & 0x80) {
        op = m_lda8(s, op & 0x7F);
        page = 0x0100;
    }
    m_write8(s, m_dp(s, OPERATOR), op);
    m_lda8(s, m_read8(s, m_dp(s, LHS)));
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, (uint16_t)((s->cpu.a & 0x00FF) | page));
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_dp(s, LHS), m_lda8(s, m_read8(s, MEMORY + s->cpu.x)));
    m_lda8(s, m_read8(s, m_dp(s, OPERATOR)));
    finish_conditional(s, 0xC06433, false);
}

/* X is at a jump distance byte: X += that byte, the jump commands' way
 * (LDA script,X then jump_by). */
static void jump_from_here(Snes *s)
{
    m_lda8(s, script_byte(s, s->cpu.x));
    jump_by(s, false);
}

// $C0:6442
/* $18: if the byte at $7F:0000 is less than operand 1, continue after
 * operand 2; otherwise jump forward by operand 2. */
void event_cmd_if_mem0_below(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    uint8_t value = m_lda8(s, m_read8(s, MEMORY));
    uint8_t limit = script_byte(s, s->cpu.x);
    m_cmp8(s, value, limit);
    if (value < limit) {
        m_inx(s);
        m_inx(s);
        m_set_flag(s, CPU_FLAG_C, true);
        m_rts(s);
        return;
    }
    m_inx(s);
    jump_from_here(s);
}

// $C0:6568
/* $1A: if the object's $7F:0A80 byte equals operand 1, continue after
 * operand 2; otherwise jump forward by operand 2. */
void event_cmd_if_object_value(Snes *s)
{
    ldx_current_object(s);
    uint8_t value = m_lda8(s, m_read8(s, 0x7F0A80u + s->cpu.x));
    m_iny(s);
    m_tyx(s);
    uint8_t expected = script_byte(s, s->cpu.x);
    m_cmp8(s, value, expected);
    if (value == expected) {
        m_inx(s);
        m_inx(s);
        m_set_flag(s, CPU_FLAG_C, true);
        m_rts(s);
        return;
    }
    m_inx(s);
    jump_from_here(s);
}

// $C0:662C
/* $27: if the object in operand 1 is in view ($0F00,X nonzero; see
 * field_update_object_visibility), continue after operand 2; otherwise jump
 * forward by operand 2. */
void event_cmd_if_object_in_view(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_tdc(s);
    m_xba(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_tax(s);
    if (m_lda8(s, m_read8(s, m_abs(s, 0x0F00) + s->cpu.x)) != 0) {
        m_tyx(s);
        m_inx(s);
        m_inx(s);
        m_set_flag(s, CPU_FLAG_C, true);
        m_rts(s);
        return;
    }
    m_tyx(s);
    m_inx(s);
    jump_from_here(s);
}

/* LDA v : SEC : SBC $cell : BEQ out : CMP #$01 : BEQ out : CMP #limit : BCS out
 * True if v - $cell is from 2 to limit-1. */
static bool in_band(Snes *s, uint8_t v, uint8_t cell, uint8_t limit)
{
    m_lda8(s, v);
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t d = m_sbc8(s, v, m_read8(s, m_dp(s, cell)));
    m_set_a8(s, d);
    if (d == 0)
        return false;
    m_cmp8(s, d, 0x01);
    if (d == 0x01)
        return false;
    m_cmp8(s, d, limit);
    return d < limit;
}

// $C0:6655
/* $28: if the object in operand 1 is well inside the screen -- its tile 2 to
 * 13 columns right of and 2 to 12 rows below the screen origin ($1D0A/$1D0E,
 * halved) -- continue after operand 2; otherwise jump forward by operand 2. */
void event_cmd_if_object_on_screen(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_tdc(s);
    m_xba(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_tax(s);
    uint8_t v = m_read8(s, m_abs(s, 0x1D0A));
    m_set_flag(s, CPU_FLAG_C, v & 1);
    m_write8(s, m_dp(s, 0xDB), m_lda8(s, v >> 1));
    v = m_read8(s, m_abs(s, 0x1D0E));
    m_set_flag(s, CPU_FLAG_C, v & 1);
    m_write8(s, m_dp(s, 0xDD), m_lda8(s, v >> 1));
    if (in_band(s, m_read8(s, m_abs(s, 0x1801) + s->cpu.x), 0xDB, 0x0E) &&
        in_band(s, m_read8(s, m_abs(s, 0x1881) + s->cpu.x), 0xDD, 0x0D)) {
        m_tyx(s);
        m_inx(s);
        m_inx(s);
        m_set_flag(s, CPU_FLAG_C, true);
        m_rts(s);
        return;
    }
    m_tyx(s);
    m_inx(s);
    jump_from_here(s);
}

// $C0:2F96
/* Wait: end the object's turn for operand frames, counting in $7F:0A01,X
 * (X = the object's slot). When the count is reached, reset it and continue
 * with the next command this frame. A count already past the operand is reset
 * and the command runs again. */
void event_cmd_wait(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    uint8_t frames = m_lda8(s, script_byte(s, s->cpu.x));
    ldx_current_object(s);
    uint32_t counter = 0x7F0A01u + s->cpu.x;
    uint8_t count = m_read8(s, counter);
    m_cmp8(s, frames, count);
    if (frames == count) {
        m_lda8(s, 0x00);
        m_write8(s, counter, 0x00);
        m_tyx(s);
        m_inx(s);
        m_inx(s);
        m_set_flag(s, CPU_FLAG_C, true);
    } else if (frames > count) {
        uint8_t next = (uint8_t)(count + 1);
        m_lda8(s, count);
        m_set_a8(s, next);
        m_set_nz8(s, next);
        m_write8(s, counter, next);
        m_tyx(s);
        m_set_flag(s, CPU_FLAG_C, false);
    } else {
        m_lda8(s, 0x00);
        m_write8(s, counter, 0x00);
        m_tyx(s);
        m_set_flag(s, CPU_FLAG_C, true);
    }
    m_rts(s);
}

/* $C0:2FD4, A = frames: the fixed-length waits, as $AD without an operand. */
static void wait_frames(Snes *s, uint8_t frames)
{
    m_lda8(s, frames);
    ldx_current_object(s);
    uint32_t counter = 0x7F0A01u + s->cpu.x;
    uint8_t count = m_read8(s, counter);
    m_cmp8(s, frames, count);
    if (frames == count) {
        m_lda8(s, 0x00);
        m_write8(s, counter, 0x00);
        m_tyx(s);
        m_inx(s);
        m_set_flag(s, CPU_FLAG_C, true);
    } else if (frames > count) {
        uint8_t next = m_lda8(s, (uint8_t)(count + 1));
        m_write8(s, counter, next);
        m_tyx(s);
        m_set_flag(s, CPU_FLAG_C, false);
    } else {
        m_lda8(s, 0x00);
        m_write8(s, counter, 0x00);
        m_tyx(s);
        m_set_flag(s, CPU_FLAG_C, true);
    }
    m_rts(s);
}

// $C0:2FC6
/* $B9: wait 4 frames. */
void event_cmd_wait_4(Snes *s) { wait_frames(s, 0x04); }

// $C0:2FCA
/* $BA: wait 8 frames. */
void event_cmd_wait_8(Snes *s) { wait_frames(s, 0x08); }

// $C0:2FD2
/* $BC: wait 16 frames. */
void event_cmd_wait_16(Snes *s) { wait_frames(s, 0x10); }

// $C0:2FCE
/* $BD: wait 32 frames. */
void event_cmd_wait_32(Snes *s) { wait_frames(s, 0x20); }

// $C0:2FFD
/* $AF: run $B0 (the state routine command) and continue after this command. */
void event_cmd_run_state_routine_once(Snes *s)
{
    m_jsr(s, 0xC02FFD, 0xC03002);
    if (s->cpu.e || (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
        return;
    m_inx(s);
    m_rts(s);
}

// $C0:353F
/* Yield: end the object's turn; continue after the command next frame. */
void event_cmd_yield(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:3543
/* Halt: end the object's turn without advancing, so the script stays here. */
void event_cmd_halt(Snes *s)
{
    m_tyx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}
