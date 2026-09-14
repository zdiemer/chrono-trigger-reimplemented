/* Event commands: conditions on controller input.
 *
 * Field mode keeps controller state in page zero (absolute addresses, not the
 * direct page, which field mode moves to $0100): $00F3 holds the D-pad (Right
 * $01, Left $02, Down $04) and $00F2 buttons (A $80, Start $01; the other bits
 * aren't identified yet), with $00F8/$00F9 filtered copies of them. Each
 * frame the field main loop ($C0:0097) ORs $00F6 into $50 and $00F0 into $51,
 * so bits there stay set until a command clears them.
 *
 * Each command takes one operand, a jump distance: if the condition holds the
 * script continues after it, otherwise it jumps forward by it from the
 * operand's offset. */
#include "event/event.h"

#include "event/script.h"

/* $C0:6701: INY : TYX : SEC : RTS -- the condition holds. */
static void holds(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* $C0:66D0: TYX : LDA script,X : REP #$20 : AND #$00FF : STA $D9 : TXA : CLC :
 * ADC $D9 : TAX : SEP #$20 : SEC : RTS -- jump forward by the operand. */
static void fails(Snes *s)
{
    m_tyx(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_rep(s, CPU_FLAG_M);
    uint16_t distance = m_lda16(s, s->cpu.a & 0x00FF);
    m_write16(s, m_dp(s, 0xD9), distance);
    m_txa(s);
    m_set_flag(s, CPU_FLAG_C, false);
    s->cpu.a = m_adc16(s, s->cpu.a, distance);
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* INY : LDA cell : BIT #mask : BEQ fails : holds */
static void if_bit(Snes *s, uint16_t cell, uint8_t mask)
{
    m_iny(s);
    m_lda8(s, m_read8(s, m_abs(s, cell)));
    m_bit_imm8(s, mask);
    if ((uint8_t)s->cpu.a & mask)
        holds(s);
    else
        fails(s);
}

/* INY : LDA dp : AND #mask : BEQ fails : TRB dp : holds -- consumes the bit. */
static void if_latched_bit(Snes *s, uint8_t cell, uint8_t mask)
{
    m_iny(s);
    uint8_t v = m_lda8(s, (uint8_t)(m_read8(s, m_dp(s, cell)) & mask));
    if (v == 0) {
        fails(s);
        return;
    }
    m_trb8(s, m_dp(s, cell));
    holds(s);
}

// $C0:66A5
/* $2D: if $00F8 or $00F9 is nonzero. */
void event_cmd_if_any_input(Snes *s)
{
    m_iny(s);
    if (m_lda8(s, m_read8(s, m_abs(s, 0x00F8))) != 0 || m_lda8(s, m_read8(s, m_abs(s, 0x00F9))) != 0)
        holds(s);
    else
        fails(s);
}

// $C0:66B2
/* $30: if $00F8 bit 1. */
void event_cmd_if_input_f8_bit1(Snes *s) { if_bit(s, 0x00F8, 0x02); }

// $C0:66BC
/* $31: if $00F8 bit 7 (the A button). */
void event_cmd_if_input_f8_a(Snes *s) { if_bit(s, 0x00F8, 0x80); }

// $C0:66C6
/* $34: if $00F2 bit 7 (the A button). */
void event_cmd_if_input_f2_a(Snes *s) { if_bit(s, 0x00F2, 0x80); }

// $C0:66E5
/* $35: if $00F2 bit 3. */
void event_cmd_if_input_f2_bit3(Snes *s) { if_bit(s, 0x00F2, 0x08); }

// $C0:66EF
/* $36: if $00F2 bit 6. */
void event_cmd_if_input_f2_bit6(Snes *s) { if_bit(s, 0x00F2, 0x40); }

// $C0:66F9
/* $37: if $00F2 bit 2. */
void event_cmd_if_input_f2_bit2(Snes *s) { if_bit(s, 0x00F2, 0x04); }

// $C0:6705
/* $38: if $00F2 bit 5. */
void event_cmd_if_input_f2_bit5(Snes *s) { if_bit(s, 0x00F2, 0x20); }

// $C0:670F
/* $39: if $00F2 bit 4. */
void event_cmd_if_input_f2_bit4(Snes *s) { if_bit(s, 0x00F2, 0x10); }

// $C0:6719
/* $3B: if latched bit 1 of $50 is set; clears it. */
void event_cmd_if_latched_50_bit1(Snes *s) { if_latched_bit(s, 0x50, 0x02); }

// $C0:6724
/* $3C: if latched bit 7 of $50 is set; clears it. */
void event_cmd_if_latched_50_bit7(Snes *s) { if_latched_bit(s, 0x50, 0x80); }

// $C0:6732
/* $3F: if latched bit 7 of $51 is set; clears it. */
void event_cmd_if_latched_51_bit7(Snes *s) { if_latched_bit(s, 0x51, 0x80); }
