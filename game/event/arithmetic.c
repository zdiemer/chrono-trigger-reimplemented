/* Event commands: assigning to and calculating with script variables (8- and
 * 16-bit values in $7F:0200, addressed by operand * 2). */
#include "event/event.h"

#include "event/script.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint8_t var8(Snes *s) { return m_read8(s, VARS + s->cpu.x); }
static void set_var8(Snes *s, uint8_t v) { m_write8(s, VARS + s->cpu.x, v); }
static uint16_t var16(Snes *s) { return m_read16(s, VARS + s->cpu.x); }
static void set_var16(Snes *s, uint16_t v) { m_write16(s, VARS + s->cpu.x, v); }

/* INY : TYX : LDA script,X : STA $D9 : INX, then the variable offset for the
 * next operand : SEP #$20 -- a constant byte, then a variable. */
static void constant_then_variable(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    set_dp8(s, 0xD9, m_lda8(s, script_byte(s, s->cpu.x)));
    m_inx(s);
    var_offset_here(s);
    m_sep(s, CPU_FLAG_M);
}

/* Two variable operands: $D9-$DA = the first variable (16-bit), X = the second
 * variable's offset; leaves M 16-bit. The second index is read 16-bit. */
static void variable_then_variable(Snes *s)
{
    var_offset_next(s);
    m_write16(s, m_dp(s, 0xD9), m_lda16(s, var16(s)));
    m_iny(s);
    m_tyx(s);
    m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
    s->cpu.a = m_asl16(s, m_lda16(s, s->cpu.a & 0x00FF));
    m_tax(s);
}

/* INY : INY : TYX : SEC : RTS */
static void skip_constant_and_variable(Snes *s)
{
    m_iny(s);
    iny_tyx_sec_rts(s);
}

// $C0:6788
/* $47: set $6B to operand 1. */
void event_cmd_set_6b(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    set_dp8(s, 0x6B, m_lda8(s, script_byte(s, s->cpu.x)));
    iny_tyx_sec_rts(s);
}

// $C0:68A5
/* $4F: variable (operand 2) = operand 1. */
void event_cmd_var_set(Snes *s)
{
    constant_then_variable(s);
    set_var8(s, m_lda8(s, dp8(s, 0xD9)));
    tyx_inx_sec_rts(s, 2);
}

// $C0:68C6
/* $50: 16-bit variable (operand 3) = operands 1-2. */
void event_cmd_var_set16(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_rep(s, CPU_FLAG_M);
    m_write16(s, m_dp(s, 0xD9), m_lda16(s, m_read16(s, SCRIPT + s->cpu.x)));
    m_inx(s);
    m_inx(s);
    m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
    s->cpu.a = m_asl16(s, m_lda16(s, s->cpu.a & 0x00FF));
    m_tax(s);
    set_var16(s, m_lda16(s, m_read16(s, m_dp(s, 0xD9))));
    m_sep(s, CPU_FLAG_M);
    tyx_inx_sec_rts(s, 3);
}

// $C0:68E9
/* $51: variable (operand 2) = variable (operand 1). */
void event_cmd_var_copy(Snes *s)
{
    variable_then_variable(s);
    m_sep(s, CPU_FLAG_M);
    set_var8(s, m_lda8(s, dp8(s, 0xD9)));
    tyx_inx_sec_rts(s, 1);
}

// $C0:6913
/* $52: 16-bit variable (operand 2) = 16-bit variable (operand 1). */
void event_cmd_var_copy16(Snes *s)
{
    variable_then_variable(s);
    set_var16(s, m_lda16(s, m_read16(s, m_dp(s, 0xD9))));
    m_sep(s, CPU_FLAG_M);
    tyx_inx_sec_rts(s, 1);
}

// $C0:6A1F
/* $5B: variable (operand 2) += operand 1. */
void event_cmd_var_add(Snes *s)
{
    constant_then_variable(s);
    m_lda8(s, var8(s));
    m_set_flag(s, CPU_FLAG_C, false);
    uint8_t v = m_adc8(s, (uint8_t)s->cpu.a, dp8(s, 0xD9));
    m_set_a8(s, v);
    set_var8(s, v);
    skip_constant_and_variable(s);
}

// $C0:6A45
/* $5D: variable (operand 2) += variable (operand 1). */
void event_cmd_var_add_var(Snes *s)
{
    variable_then_variable(s);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, var8(s));
    m_set_flag(s, CPU_FLAG_C, false);
    uint8_t v = m_adc8(s, (uint8_t)s->cpu.a, dp8(s, 0xD9));
    m_set_a8(s, v);
    set_var8(s, v);
    iny_tyx_sec_rts(s);
}

// $C0:6A74
/* $5E: 16-bit variable (operand 2) += 16-bit variable (operand 1). */
void event_cmd_var_add_var16(Snes *s)
{
    variable_then_variable(s);
    m_lda16(s, var16(s));
    m_set_flag(s, CPU_FLAG_C, false);
    uint16_t v = m_adc16(s, s->cpu.a, m_read16(s, m_dp(s, 0xD9)));
    s->cpu.a = v;
    set_var16(s, v);
    m_sep(s, CPU_FLAG_M);
    iny_tyx_sec_rts(s);
}

// $C0:6AA3
/* $5F: variable (operand 2) -= operand 1. */
void event_cmd_var_subtract(Snes *s)
{
    constant_then_variable(s);
    m_lda8(s, var8(s));
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t v = m_sbc8(s, (uint8_t)s->cpu.a, dp8(s, 0xD9));
    m_set_a8(s, v);
    set_var8(s, v);
    skip_constant_and_variable(s);
}

// $C0:6AC9
/* $60: 16-bit variable (operand 3) -= operands 1-2. */
void event_cmd_var_subtract16(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_rep(s, CPU_FLAG_M);
    m_write16(s, m_dp(s, 0xD9), m_lda16(s, m_read16(s, SCRIPT + s->cpu.x)));
    m_inx(s);
    m_inx(s);
    m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
    s->cpu.a = m_asl16(s, m_lda16(s, s->cpu.a & 0x00FF));
    m_tax(s);
    m_lda16(s, var16(s));
    m_set_flag(s, CPU_FLAG_C, true);
    uint16_t v = m_sbc16(s, s->cpu.a, m_read16(s, m_dp(s, 0xD9)));
    s->cpu.a = v;
    set_var16(s, v);
    m_sep(s, CPU_FLAG_M);
    m_iny(s);
    m_iny(s);
    iny_tyx_sec_rts(s);
}

// $C0:6AF1
/* $61: variable (operand 2) -= variable (operand 1). */
void event_cmd_var_subtract_var(Snes *s)
{
    variable_then_variable(s);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, var8(s));
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t v = m_sbc8(s, (uint8_t)s->cpu.a, dp8(s, 0xD9));
    m_set_a8(s, v);
    set_var8(s, v);
    iny_tyx_sec_rts(s);
}

/* INY : TYX : TDC : XBA : LDA script,X : TAX : LDA table,X : STA $D9, then
 * the variable offset for the next operand : SEP #$20 -- a bit mask from one of
 * the tables at $FF20 (single bits) and $FF28 (all but one bit). */
static void mask_then_variable(Snes *s, uint16_t table)
{
    m_iny(s);
    m_tyx(s);
    m_tdc(s);
    m_xba(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_tax(s);
    set_dp8(s, 0xD9, m_lda8(s, m_read8(s, m_abs(s, table) + s->cpu.x)));
    var_offset_next(s);
    m_sep(s, CPU_FLAG_M);
}

// $C0:6B20
/* $63: set bit (operand 1, via the table at $FF20) in variable (operand 2). */
void event_cmd_var_set_bit(Snes *s)
{
    mask_then_variable(s, 0xFF20);
    set_var8(s, m_lda8(s, (uint8_t)(var8(s) | dp8(s, 0xD9))));
    iny_tyx_sec_rts(s);
}

// $C0:6B4B
/* $64: clear bits (operand 1, via the table at $FF28) in variable (operand 2). */
void event_cmd_var_clear_bit(Snes *s)
{
    mask_then_variable(s, 0xFF28);
    set_var8(s, m_lda8(s, (uint8_t)(var8(s) & dp8(s, 0xD9))));
    iny_tyx_sec_rts(s);
}

/* constant_then_variable, then LDA var : <op> $D9 : STA var, and skip both operands. */
static void var_logic(Snes *s, char op)
{
    constant_then_variable(s);
    uint8_t v = var8(s), d = dp8(s, 0xD9);
    v = op == '&' ? (uint8_t)(v & d) : op == '|' ? (uint8_t)(v | d) : (uint8_t)(v ^ d);
    set_var8(s, m_lda8(s, v));
    skip_constant_and_variable(s);
}

// $C0:6BD2
/* $67: variable (operand 2) &= operand 1. */
void event_cmd_var_and(Snes *s) { var_logic(s, '&'); }

// $C0:6BF7
/* $69: variable (operand 2) |= operand 1. */
void event_cmd_var_or(Snes *s) { var_logic(s, '|'); }

// $C0:6C1C
/* $6B: variable (operand 2) ^= operand 1. */
void event_cmd_var_xor(Snes *s) { var_logic(s, '^'); }

// $C0:6C41
/* $6F: variable (operand 2) >>= operand 1 (0 shifts 256 times). */
void event_cmd_var_shift_right(Snes *s)
{
    constant_then_variable(s);
    uint8_t v = m_lda8(s, var8(s));
    uint8_t count;
    do {
        v = m_lsr8(s, v);
        m_set_a8(s, v);
        count = (uint8_t)(dp8(s, 0xD9) - 1);
        set_dp8(s, 0xD9, count);
        m_set_nz8(s, count);
    } while (count != 0);
    set_var8(s, v);
    skip_constant_and_variable(s);
}

// $C0:6C69
/* $71: variable (operand 1) += 1. */
void event_cmd_var_increment(Snes *s)
{
    var_offset_next(s);
    m_sep(s, CPU_FLAG_M);
    set_var8(s, m_lda8(s, (uint8_t)(var8(s) + 1)));
    iny_tyx_sec_rts(s);
}

// $C0:6C85
/* $72: 16-bit variable (operand 1) += 1. */
void event_cmd_var_increment16(Snes *s)
{
    var_offset_next(s);
    set_var16(s, m_lda16(s, (uint16_t)(var16(s) + 1)));
    m_sep(s, CPU_FLAG_M);
    iny_tyx_sec_rts(s);
}

// $C0:6CA1
/* $73: variable (operand 1) -= 1. */
void event_cmd_var_decrement(Snes *s)
{
    var_offset_next(s);
    m_sep(s, CPU_FLAG_M);
    set_var8(s, m_lda8(s, (uint8_t)(var8(s) - 1)));
    iny_tyx_sec_rts(s);
}

// $C0:6CBD
/* $75: variable (operand 1) = 1. */
void event_cmd_var_set_1(Snes *s)
{
    var_offset_next(s);
    m_sep(s, CPU_FLAG_M);
    set_var8(s, m_lda8(s, 0x01));
    iny_tyx_sec_rts(s);
}

// $C0:6CD6
/* $76: 16-bit variable (operand 1) = 1. */
void event_cmd_var_set16_1(Snes *s)
{
    var_offset_next(s);
    set_var16(s, m_lda16(s, 0x0001));
    m_sep(s, CPU_FLAG_M);
    iny_tyx_sec_rts(s);
}

// $C0:6CF0
/* $77: variable (operand 1) = 0. */
void event_cmd_var_clear(Snes *s)
{
    var_offset_next(s);
    m_sep(s, CPU_FLAG_M);
    set_var8(s, m_lda8(s, 0x00));
    iny_tyx_sec_rts(s);
}

// $C0:6D09
/* $7F: variable (operand 1) = the next byte of the table at $FE00, stepping
 * the index in $F8. */
void event_cmd_var_random(Snes *s)
{
    m_tdc(s);
    m_xba(s);
    uint8_t index = m_lda8(s, (uint8_t)(dp8(s, 0xF8) + 1));
    set_dp8(s, 0xF8, index);
    m_tax(s);
    set_dp8(s, 0xD9, m_lda8(s, m_read8(s, m_abs(s, 0xFE00) + s->cpu.x)));
    var_offset_next(s);
    m_sep(s, CPU_FLAG_M);
    set_var8(s, m_lda8(s, dp8(s, 0xD9)));
    iny_tyx_sec_rts(s);
}
