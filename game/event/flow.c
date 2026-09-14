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
        s->cpu.pb = call_site >> 16;
        s->cpu.pc = call_site & 0xFFFF;
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
