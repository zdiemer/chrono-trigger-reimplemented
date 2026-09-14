/* Event commands: screen effects -- brightness, color math, scrolling -- and
 * a few others nearby.
 *
 * Field mode's direct page is $0100. The field NMI (field/nmi.c) writes
 * brightness from $19 to $2100 and the fixed color from $21 to $2132. */
#include "event/event.h"

#include "event/script.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }

// $C0:3C05
/* $E3: set $1F to operand 1. */
void event_cmd_set_1f(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    set_dp8(s, 0x1F, m_lda8(s, script_byte(s, s->cpu.x)));
    iny_tyx_sec_rts(s);
}

/* LDA $D9 : LSR A x4 : AND #$0F -- operand 1's high nibble. */
static uint8_t high_nibble(Snes *s)
{
    uint8_t v = m_lda8(s, dp8(s, 0xD9));
    for (int i = 0; i < 4; i++)
        v = m_lsr8(s, v);
    return m_lda8(s, v & 0x0F);
}

// $C0:3CD0
/* $F0: brightness. With a nonzero low nibble in operand 1, start changing
 * the brightness toward the high nibble ($1B) at the rate in the low nibble
 * ($1C/$1D, bit 1 of $18 marks it running); otherwise set the brightness ($19)
 * to the high nibble at once. Ends the turn. */
void event_cmd_brightness(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    uint8_t op = m_lda8(s, script_byte(s, s->cpu.x));
    m_inx(s);
    set_dp8(s, 0xD9, op);
    uint8_t rate = m_lda8(s, op & 0x0F);
    if (rate != 0) {
        set_dp8(s, 0x1C, rate);
        set_dp8(s, 0x1D, rate);
        set_dp8(s, 0x1B, high_nibble(s));
        m_lda8(s, 0x02);
        m_tsb8(s, m_dp(s, 0x18));
    } else {
        set_dp8(s, 0x19, high_nibble(s));
    }
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* TYX : LDA $18 : BIT #bit : BNE + : INX : + CLC : RTS -- wait while the bit is set. */
static void wait_while_18(Snes *s, uint8_t bit)
{
    m_tyx(s);
    uint8_t v = m_lda8(s, dp8(s, 0x18));
    m_bit_imm8(s, bit);
    if (!(v & bit))
        m_inx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:3CFD
/* $F2: wait until a brightness change started by $F0 has finished. */
void event_cmd_wait_brightness(Snes *s) { wait_while_18(s, 0x02); }

// $C0:3D7A
/* $F3: wait until bit 3 of $18 (set by $F1's gradual changes) clears. */
void event_cmd_wait_color(Snes *s) { wait_while_18(s, 0x08); }

/* LDA $22 : AND #$E0 : STA $D9 : LDA $21 : AND #$1F : ORA $D9 : STA $21 --
 * take the color channel bits of $22 into $21. */
static void merge_color_channels(Snes *s)
{
    set_dp8(s, 0xD9, m_lda8(s, dp8(s, 0x22) & 0xE0));
    uint8_t v = m_lda8(s, dp8(s, 0x21) & 0x1F);
    set_dp8(s, 0x21, m_lda8(s, (uint8_t)(v | dp8(s, 0xD9))));
}

// $C0:3D07
/* $F1: fixed color. Operand 1 zero: fixed color $E0 (black, all channels) and
 * $0BE0 back to $0BDF; ends after one operand byte. Otherwise operand 1 goes
 * to $22 and its channel bits into $21; operand 2 zero (or $80) sets $21 =
 * $22 at once with $0BE0 = $17 (or $97), anything else starts a gradual
 * change at that rate ($23/$24, bits 3-4 of $18). */
void event_cmd_fixed_color(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    uint8_t color = m_lda8(s, script_byte(s, s->cpu.x));
    if (color == 0) {
        set_dp8(s, 0x22, m_lda8(s, 0xE0));
        set_dp8(s, 0x21, 0xE0);
        m_write8(s, m_abs(s, 0x0BE0), m_lda8(s, m_read8(s, m_abs(s, 0x0BDF))));
        m_inx(s);
        m_set_flag(s, CPU_FLAG_C, true);
        m_rts(s);
        return;
    }
    m_inx(s);
    set_dp8(s, 0x22, color);
    merge_color_channels(s);
    uint8_t rate = m_lda8(s, script_byte(s, s->cpu.x));
    uint8_t math = 0x17;
    if (rate & 0x80) {
        rate = m_lda8(s, rate & 0x7F);
        math = 0x97;
    }
    if (rate == 0) {
        set_dp8(s, 0x21, m_lda8(s, dp8(s, 0x22)));
        m_write8(s, m_abs(s, 0x0BE0), m_lda8(s, math));
        m_inx(s);
        m_set_flag(s, CPU_FLAG_C, true);
        m_rts(s);
        return;
    }
    set_dp8(s, 0x23, rate);
    set_dp8(s, 0x24, rate);
    m_write8(s, m_abs(s, 0x0BE0), m_lda8(s, math));
    merge_color_channels(s);
    m_lda8(s, 0x18);
    m_tsb8(s, m_dp(s, 0x18));
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:3D84
/* $F4: set $BC to operand 1; zero also clears $1D4E and $1D4F. */
void event_cmd_set_bc(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    uint8_t v = m_lda8(s, script_byte(s, s->cpu.x));
    set_dp8(s, 0xBC, v);
    if (v == 0) {
        m_write8(s, m_abs(s, 0x1D4E), 0x00);
        m_write8(s, m_abs(s, 0x1D4F), 0x00);
    }
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* TYX : INX : REP #$20 : $3E, $40, $42 = 16-bit operands : SEP #$20 : $44 =
 * operand 7. Leaves X at operand 7. */
static void load_tile_copy(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    m_rep(s, CPU_FLAG_M);
    for (uint8_t cell = 0x3E; cell <= 0x42; cell += 2) {
        m_write16(s, m_dp(s, cell), m_lda16(s, m_read16(s, SCRIPT + s->cpu.x)));
        m_inx(s);
        m_inx(s);
    }
    m_sep(s, CPU_FLAG_M);
    set_dp8(s, 0x44, m_lda8(s, script_byte(s, s->cpu.x)));
}

// $C0:3D97
/* $E4: copy map tiles now: parameters in $3E-$44 for $C0:AF4E (called out). */
void event_cmd_copy_tiles(Snes *s)
{
    load_tile_copy(s);
    m_write16(s, m_dp(s, 0xC7), s->cpu.x);
    m_jsr(s, 0xC03DBD, 0xC0AF4E);
    if (s->cpu.e || (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
        return;
    m_ldx(s, m_read16(s, m_dp(s, 0xC7)));
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:3DC5
/* $E5: queue the same tile copy for later: $45 = the low 3 bits of $44, and
 * bit 5 of $17 set. */
void event_cmd_queue_tile_copy(Snes *s)
{
    load_tile_copy(s);
    set_dp8(s, 0x45, m_lda8(s, dp8(s, 0x44) & 0x07));
    m_lda8(s, 0x20);
    m_tsb8(s, m_dp(s, 0x17));
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:3DF4
/* $E6: set layer scroll values: the 16-bit operand 1-2 goes to $1D24, $1D26
 * and/or $1D28 for bits 0, 1 and 2 of operand 3, and operand 4 to $1D4D.
 * Leaves Y holding the value when any bit is set. */
void event_cmd_set_layer_scroll(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    m_rep(s, CPU_FLAG_M);
    m_write16(s, m_dp(s, 0xDB), m_lda16(s, m_read16(s, SCRIPT + s->cpu.x)));
    m_inx(s);
    m_inx(s);
    m_sep(s, CPU_FLAG_M);
    uint8_t layers = m_lda8(s, script_byte(s, s->cpu.x));
    set_dp8(s, 0xD9, layers);
    m_inx(s);
    m_write8(s, m_abs(s, 0x1D4D), m_lda8(s, script_byte(s, s->cpu.x)));
    m_lda8(s, dp8(s, 0xD9));
    static const uint16_t targets[3] = { 0x1D24, 0x1D26, 0x1D28 };
    for (int i = 0; i < 3; i++) {
        m_bit_imm8(s, (uint8_t)(1 << i));
        if (layers & (1 << i)) {
            m_ldy(s, m_read16(s, m_dp(s, 0xDB)));
            m_write16(s, m_abs(s, targets[i]), s->cpu.y);
        }
    }
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C0:3E30
/* $E7: scroll the screen to operands 1-2 (doubled into $1D02/$1D03), a
 * state machine in $1D00: 0 starts (and clears $1F), 1 waits, anything else
 * finishes and continues. */
void event_cmd_scroll_screen(Snes *s)
{
    uint8_t state = m_lda8(s, m_read8(s, m_abs(s, 0x1D00)));
    if (state != 0) {
        if (m_lda8(s, (uint8_t)(state - 1)) != 0) {
            m_write8(s, m_abs(s, 0x1D00), 0x00);
            tyx_inx_sec_rts(s, 3);
            return;
        }
    } else {
        m_tyx(s);
        m_inx(s);
        uint8_t v = m_asl8(s, m_lda8(s, script_byte(s, s->cpu.x)));
        m_set_a8(s, v);
        m_write8(s, m_abs(s, 0x1D02), v);
        m_inx(s);
        v = m_asl8(s, m_lda8(s, script_byte(s, s->cpu.x)));
        m_set_a8(s, v);
        m_write8(s, m_abs(s, 0x1D03), v);
        uint32_t addr = m_abs(s, 0x1D00);
        uint8_t next = (uint8_t)(m_read8(s, addr) + 1);
        m_write8(s, addr, next);
        m_set_nz8(s, next);
        set_dp8(s, 0x1F, 0x00);
    }
    m_tyx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* INY : STY $C7 : JSL $C2:8004 with A = each value : LDX $C7 : RTS */
static void party_routine(Snes *s, uint32_t first_site, uint8_t first, bool both)
{
    m_iny(s);
    m_write16(s, m_dp(s, 0xC7), s->cpu.y);
    uint32_t site = first_site;
    m_lda8(s, first);
    if (both) {
        m_jsl(s, site, 0xC28004);
        if (s->cpu.e || (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
            return;
        m_lda8(s, 0x07);
        site = 0xC03E67;
    }
    m_jsl(s, site, 0xC28004);
    if (s->cpu.e || (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
        return;
    m_ldx(s, m_read16(s, m_dp(s, 0xC7)));
    m_rts(s);
}

// $C0:3E5C
/* $F8: call $C2:8004 with A = 6, then with A = 7. */
void event_cmd_party_routine_6_7(Snes *s) { party_routine(s, 0xC03E61, 0x06, true); }

// $C0:3E6E
/* $F9: call $C2:8004 with A = 6. */
void event_cmd_party_routine_6(Snes *s) { party_routine(s, 0xC03E67, 0x06, false); }

// $C0:3E75
/* $FA: call $C2:8004 with A = 7. */
void event_cmd_party_routine_7(Snes *s) { party_routine(s, 0xC03E67, 0x07, false); }
