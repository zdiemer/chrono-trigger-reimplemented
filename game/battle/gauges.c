/* Battle: the window's gauges, drawn into the window tilemap at $0CC0. */
#include "battle/entries.h"

#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }
static void set_absy8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.y, v); }

// $C1:06F0
/* Draw party member $80's gauge: 4 tiles at the member's window position
 * ($CC:FA35 + $1A) filled in proportion to $99DD / $9F22 (32 pixels, 8 per
 * tile), full tiles $6F and a partial tile $67 + remaining pixels; the tile
 * attributes are $2D while $99DD is nonzero, $29 otherwise. The ratio is
 * computed with the hardware divider ($C1:00D7, flagged in $A029 for the
 * interrupt handlers). */
void battle_draw_gauge(Snes *s)
{
    Cpu *c = &s->cpu;
    m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
    m_lda16(s, dp16(s, 0x80));
    c->a = m_asl16(s, c->a);
    m_tax(s);
    m_lda16(s, m_read16(s, 0xCCFA35 + c->x));
    c->a = m_adc16(s, c->a, 0x001A);
    m_set_index(s, &c->y, c->a);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_ldx16(s, dp16(s, 0x80));
    set_dp8(s, 0xAD, m_lda8(s, m_read8(s, m_abs(s, 0x99DD) + c->x)));
    set_dp8(s, 0xAE, 0x00);
    set_dp8(s, 0xB3, m_lda8(s, m_read8(s, m_abs(s, 0x9F22) + c->x)));
    set_dp8(s, 0xB4, 0x00);

    /* JSR $010D: ASL x8 (16-bit) */
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, dp16(s, 0xAD));
    m_push16(s, 0x0717);
    for (int i = 0; i < 8; i++)
        c->a = m_asl16(s, c->a);
    m_pull16(s);
    set_dp16(s, 0xB1, c->a);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);

    /* JSR $00D7: $B5 = $B1-$B2 / $B3, $B7 = remainder */
    m_push16(s, 0x071F);
    uint32_t busy = m_abs(s, 0xA029);
    uint8_t flag = (uint8_t)(m_read8(s, busy) + 1);
    m_write8(s, busy, flag);
    m_set_nz8(s, flag);
    static const uint16_t div_regs[3] = { 0x4204, 0x4205, 0x4206 };
    for (int i = 0; i < 3; i++) {
        uint8_t v = m_lda8(s, dp8(s, (uint8_t)(0xB1 + i)));
        set_dp8(s, (uint8_t)(0x79 + i), v);
        m_write8(s, div_regs[i], v);
    }
    m_rep(s, CPU_FLAG_M);
    set_dp16(s, 0xB5, m_lda16(s, m_read16(s, 0x004214)));
    set_dp16(s, 0xB7, m_lda16(s, m_read16(s, 0x004216)));
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, busy, 0x00);
    m_pull16(s);

    /* JSR $011B: LSR x3 (16-bit) */
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, dp16(s, 0xB5));
    m_push16(s, 0x0726);
    for (int i = 0; i < 3; i++)
        c->a = m_lsr16(s, c->a);
    m_pull16(s);
    set_dp16(s, 0xB5, c->a);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);

    /* $C1:072C: 32 - filled pixels, split into full tiles and the rest */
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t empty = m_sbc8(s, m_lda8(s, 0x20), dp8(s, 0xB5));
    m_set_a8(s, empty);
    set_dp8(s, 0x82, empty);
    uint8_t tiles = empty;
    for (int i = 0; i < 3; i++)
        tiles = m_lsr8(s, tiles);
    set_dp8(s, 0x83, tiles);
    uint8_t pixels = tiles;
    for (int i = 0; i < 3; i++)
        pixels = m_asl8(s, pixels);
    set_dp8(s, 0x86, pixels);
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t rest = m_sbc8(s, m_lda8(s, dp8(s, 0x82)), dp8(s, 0x86));
    m_set_a8(s, rest);
    set_dp8(s, 0x82, rest);

    m_lda8(s, 0x67);
    for (uint16_t k = 0; k < 8; k += 2)
        set_absy8(s, (uint16_t)(0x0CC0 + k), 0x67);
    m_ldx16(s, dp16(s, 0x80));
    uint8_t attr = m_lda8(s, m_read8(s, m_abs(s, 0x99DD) + c->x)) ? 0x2D : 0x29;
    m_lda8(s, attr);
    for (uint16_t k = 1; k < 8; k += 2)
        set_absy8(s, (uint16_t)(0x0CC0 + k), attr);
    while (m_lda8(s, dp8(s, 0x83)) != 0) {
        set_absy8(s, 0x0CC0, m_lda8(s, 0x6F));
        m_iny(s);
        m_iny(s);
        uint8_t n = (uint8_t)(dp8(s, 0x83) - 1);
        set_dp8(s, 0x83, n);
        m_set_nz8(s, n);
    }
    m_set_flag(s, CPU_FLAG_C, false);
    uint8_t part = m_lda8(s, dp8(s, 0x82));
    if (part != 0) {
        part = m_adc8(s, part, 0x67);
        m_set_a8(s, part);
        set_absy8(s, 0x0CC0, part);
    }
    m_rts(s);
}

/* SEC : LDA $9499 : SBC #step : STA $9499 : INX : BCS, then add the last step
 * back (16-bit), DEX : PHX -- how many times the step goes into $9499. */
static void count_digit(Snes *s, uint16_t step, bool store_rest)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    do {
        m_set_flag(s, CPU_FLAG_C, true);
        uint16_t v = m_sbc16(s, m_lda16(s, m_read16(s, m_abs(s, 0x9499))), step);
        m_write16(s, m_abs(s, 0x9499), v);
        m_inx(s);
    } while (m_flag(s, CPU_FLAG_C));
    m_set_flag(s, CPU_FLAG_C, false);
    c->a = m_adc16(s, m_lda16(s, m_read16(s, m_abs(s, 0x9499))), step);
    if (store_rest)
        m_write16(s, m_abs(s, 0x9499), c->a);
    m_ldx16(s, (uint16_t)(c->x - 1));
    m_push16(s, c->x);
}

// $C1:011F
/* Write the 16-bit number at $9499 (below 1000) as three digit tiles from
 * $CC:F903 into $949D-$949F, followed by $FF at $949C; $9499 is left holding
 * the number modulo 100. Entered with 16-bit A. */
void battle_number_to_digits(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push16(s, c->x);
    count_digit(s, 100, true);
    count_digit(s, 10, false);
    m_sep(s, CPU_FLAG_M);
    m_tax(s);
    static const uint16_t cells[3] = { 0x949F, 0x949E, 0x949D };
    for (int i = 0; i < 3; i++) {
        if (i > 0) {
            c->x = m_pull16(s);
            m_set_nz16(s, c->x);
        }
        m_write8(s, m_abs(s, cells[i]), m_lda8(s, m_read8(s, 0xCCF903 + c->x)));
    }
    m_write8(s, m_abs(s, 0x949C), m_lda8(s, 0xFF));
    c->x = m_pull16(s);
    m_set_nz16(s, c->x);
    m_rts(s);
}

// $C1:0174
/* Write the 16-bit number at $9499 (below 100) as two digit tiles from
 * $CC:F903 into $949E-$949F, with $FF at $949C and $949D. Entered with 16-bit
 * A; returns with 8-bit A. */
void battle_number_to_2_digits(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push16(s, c->x);
    count_digit(s, 10, false);
    m_sep(s, CPU_FLAG_M);
    m_tax(s);
    m_write8(s, m_abs(s, 0x949F), m_lda8(s, m_read8(s, 0xCCF903 + c->x)));
    c->x = m_pull16(s);
    m_set_nz16(s, c->x);
    m_write8(s, m_abs(s, 0x949E), m_lda8(s, m_read8(s, 0xCCF903 + c->x)));
    m_lda8(s, 0xFF);
    m_write8(s, m_abs(s, 0x949D), 0xFF);
    m_write8(s, m_abs(s, 0x949C), 0xFF);
    c->x = m_pull16(s);
    m_set_nz16(s, c->x);
    m_rts(s);
}

// $C1:104E
/* Blank the leading zero digit tiles ($73) of the number at $949D-$949F:
 * the hundreds, then the tens when the hundreds are blank. */
void battle_blank_leading_zeros(Snes *s)
{
    uint8_t hundreds = m_lda8(s, m_read8(s, m_abs(s, 0x949D)));
    m_cmp8(s, hundreds, 0x73);
    if (hundreds == 0x73)
        m_write8(s, m_abs(s, 0x949D), m_lda8(s, 0xFF));
    uint8_t tens = m_lda8(s, m_read8(s, m_abs(s, 0x949E)));
    m_cmp8(s, tens, 0x73);
    if (tens == 0x73) {
        hundreds = m_lda8(s, m_read8(s, m_abs(s, 0x949D)));
        m_cmp8(s, hundreds, 0xFF);
        if (hundreds == 0xFF)
            m_write8(s, m_abs(s, 0x949E), m_lda8(s, 0xFF));
    }
    m_rts(s);
}

/* JSR out; false if the callee didn't come back with the widths `widths`. */
static bool call_with(Snes *s, uint32_t site, uint32_t target, uint8_t widths)
{
    m_jsr(s, site, target);
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == widths;
}

/* LDX #$29 : LDA $A10F : BEQ : LDX #$2D : TXA -- the digit attribute: $2D
 * while $A10F is set (low HP), $29 otherwise. */
static uint8_t digit_attribute(Snes *s)
{
    m_ldx16(s, 0x0029);
    if (m_lda8(s, m_read8(s, m_abs(s, 0xA10F))) != 0)
        m_ldx16(s, 0x002D);
    m_txa(s);
    return (uint8_t)s->cpu.a;
}

// $C1:05A7
/* Draw the next party member's status in the battle window (cycling $A6E0
 * through 0-2): its HP as three digits ($5E30, highlighted with $A10F when 0
 * or at most an eighth of $5E32 on blink frames $A110) and MP as two digits
 * ($5E34), at a position from $CC:FA2F/$FA35/$FA3B by window layout $9F20;
 * then, in layouts other than 0, all party gauges. With $A6DB positive, $A09A
 * clear and layout 1, only the gauges are drawn. */
void battle_draw_party_status(Snes *s)
{
    Cpu *c = &s->cpu;
    bool gauges_only = false;
    if (!(m_lda8(s, m_read8(s, m_abs(s, 0xA6DB))) & 0x80) && m_lda8(s, m_read8(s, m_abs(s, 0xA09A))) == 0) {
        uint8_t layout = m_lda8(s, m_read8(s, m_abs(s, 0x9F20)));
        if (layout != 0 && m_lda8(s, (uint8_t)(layout - 1)) == 0)
            gauges_only = true;
    }
    if (!gauges_only) {
        uint32_t turn = m_abs(s, 0xA6E0);
        uint8_t member = (uint8_t)(m_read8(s, turn) + 1);
        m_write8(s, turn, member);
        m_set_nz8(s, member);
        member = m_lda8(s, m_read8(s, turn));
        m_cmp8(s, member, 0x03);
        if (member >= 0x03)
            m_write8(s, turn, 0x00);
        m_lda8(s, m_read8(s, turn));
        m_tax(s);
        m_write16(s, m_dp(s, 0x80), c->x);
        if (m_lda8(s, m_read8(s, m_abs(s, 0x96F5) + c->x)) == 0) {
            m_rts(s);
            return;
        }
        m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
        m_lda16(s, m_read16(s, m_dp(s, 0x80)));
        c->a = m_asl16(s, c->a);
        m_tax(s);
        uint16_t layout = m_lda16(s, m_read16(s, m_abs(s, 0x9F20)));
        uint32_t positions = 0xCCFA2F;
        if (layout != 0)
            positions = m_lda16(s, (uint16_t)(layout - 1)) == 0 ? 0xCCFA35 : 0xCCFA3B;
        m_set_index(s, &c->y, m_lda16(s, m_read16(s, positions + c->x)));
        m_write16(s, m_dp(s, 0x86), c->y);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        m_write8(s, m_abs(s, 0xA10F), 0x00);

        /* $C1:05FE: HP */
        m_rep(s, CPU_FLAG_M);
        m_lda16(s, m_read16(s, m_dp(s, 0x80)));
        c->a = m_asl16(s, c->a);
        m_tax(s);
        m_write16(s, m_dp(s, 0xA2), m_lda16(s, m_read16(s, 0xCCF8ED + c->x)));
        m_tax(s);
        uint16_t hp = m_lda16(s, m_read16(s, m_abs(s, 0x5E30) + c->x));
        m_write16(s, m_abs(s, 0x9499), hp);
        bool low = hp == 0;
        if (!low) {
            m_lda16(s, m_read16(s, m_abs(s, 0x5E32) + c->x));
            m_push16(s, 0x0618);   /* JSR $011B: LSR x3 */
            for (int i = 0; i < 3; i++)
                c->a = m_lsr16(s, c->a);
            m_pull16(s);
            uint16_t eighth = c->a;
            uint16_t cur = m_read16(s, m_abs(s, 0x9499));
            m_cmp16(s, eighth, cur);
            if (eighth >= cur)
                low = m_lda16(s, m_read16(s, m_abs(s, 0xA110))) != 0;
        }
        if (low) {
            uint32_t flag = m_abs(s, 0xA10F);
            uint16_t v = (uint16_t)(m_read16(s, flag) + 1);
            m_write16(s, flag, v);
            m_set_nz16(s, v);
        }
        if (!call_with(s, 0xC10628, 0xC1011F, CPU_FLAG_M) || !call_with(s, 0xC1062B, 0xC1104E, CPU_FLAG_M))
            return;
        for (uint16_t i = 0; i < 3; i++)
            m_write8(s, m_abs(s, (uint16_t)(0x0CC0 + 2 * i)) + c->y, m_lda8(s, m_read8(s, m_abs(s, (uint16_t)(0x949D + i)))));
        uint8_t attr = digit_attribute(s);
        for (uint16_t k = 1; k < 8; k += 2)
            m_write8(s, m_abs(s, (uint16_t)(0x0CC0 + k)) + c->y, attr);
        if (m_lda8(s, m_read8(s, m_abs(s, 0x9F20))) == 0) {
            m_txa(s);
            for (uint16_t k = 9; k < 14; k += 2)
                m_write8(s, m_abs(s, (uint16_t)(0x0CC0 + k)) + c->y, (uint8_t)c->a);
        }

        /* $C1:0667: MP */
        m_rep(s, CPU_FLAG_M);
        m_ldx16(s, m_read16(s, m_dp(s, 0xA2)));
        m_write16(s, m_abs(s, 0x9499), m_lda16(s, m_read16(s, m_abs(s, 0x5E34) + c->x)));
        uint16_t wide = m_lda16(s, m_read16(s, m_abs(s, 0x9F20)));
        m_set_flag(s, CPU_FLAG_C, false);
        m_lda16(s, m_read16(s, m_dp(s, 0x86)));
        c->a = m_adc16(s, c->a, wide == 0 ? 0x0010 : 0x0008);
        m_set_index(s, &c->y, c->a);
        if (!call_with(s, 0xC10686, 0xC10174, CPU_FLAG_M))
            return;
        bool compact = m_lda8(s, m_read8(s, m_abs(s, 0x9F20))) != 0;
        if (!call_with(s, compact ? 0xC10693 : 0xC1068E, 0xC1104E, CPU_FLAG_M))
            return;
        uint16_t first = compact ? 0x0CC0 : 0x0CC2;
        m_write8(s, m_abs(s, first) + c->y, m_lda8(s, m_read8(s, m_abs(s, 0x949E))));
        m_write8(s, m_abs(s, (uint16_t)(first + 2)) + c->y, m_lda8(s, m_read8(s, m_abs(s, 0x949F))));
        attr = digit_attribute(s);
        m_write8(s, m_abs(s, (uint16_t)(first + 1)) + c->y, attr);
        m_write8(s, m_abs(s, (uint16_t)(first + 3)) + c->y, attr);
    }

    /* $C1:06D4: gauges */
    if (m_lda8(s, m_read8(s, m_abs(s, 0x9F20))) != 0) {
        m_tdc(s);
        m_tax(s);
        m_write16(s, m_dp(s, 0x80), c->x);
        uint8_t member;
        do {
            m_ldx16(s, m_read16(s, m_dp(s, 0x80)));
            if (m_lda8(s, m_read8(s, m_abs(s, 0x96F5) + c->x)) != 0 && !call_with(s, 0xC106E4, 0xC106F0, CPU_FLAG_M))
                return;
            member = (uint8_t)(m_read8(s, m_dp(s, 0x80)) + 1);
            m_write8(s, m_dp(s, 0x80), member);
            m_set_nz8(s, member);
            member = m_lda8(s, m_read8(s, m_dp(s, 0x80)));
            m_cmp8(s, member, 0x03);
        } while (member != 0x03);
    }
    m_rts(s);
}
