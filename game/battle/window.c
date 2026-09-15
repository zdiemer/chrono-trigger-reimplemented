/* Battle: drawing the battle window's tilemap.
 *
 * The window tilemap at $0CC0 holds 2 bytes per tile (tile, attribute) and $40
 * bytes per row; $0B40 is a second tilemap of the same layout. $9F20 selects
 * one of three window layouts. */
#include "battle/window.h"

#include "port/machine.h"

static uint8_t abs8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr)); }
static void set_abs8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr), v); }
static uint8_t absx8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.x); }
static void set_absx8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.x, v); }
static void set_absy8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.y, v); }
static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }

/* INC/DEC on a direct-page byte, setting N and Z. */
static uint8_t step_dp8(Snes *s, uint8_t cell, int delta)
{
    uint8_t v = (uint8_t)(dp8(s, cell) + delta);
    set_dp8(s, cell, v);
    m_set_nz8(s, v);
    return v;
}

/* DEC A at 8 and 16 bits. */
static uint8_t dec_a8(Snes *s)
{
    uint8_t v = (uint8_t)(s->cpu.a - 1);
    m_set_a8(s, v);
    m_set_nz8(s, v);
    return v;
}

static uint16_t dec_a16(Snes *s) { return m_lda16(s, (uint16_t)(s->cpu.a - 1)); }

/* ASL A (8-bit). */
static uint8_t asl_a8(Snes *s)
{
    uint8_t v = m_asl8(s, (uint8_t)s->cpu.a);
    m_set_a8(s, v);
    return v;
}

/* LDA dp : ASL : ASL : STA dp : ASL : CLC : ADC dp : TAX -- X = value * 12. */
static void times_12_to_x(Snes *s, uint8_t value, uint8_t cell)
{
    m_lda8(s, value);
    asl_a8(s);
    set_dp8(s, cell, asl_a8(s));
    asl_a8(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_a8(s, m_adc8(s, (uint8_t)s->cpu.a, dp8(s, cell)));
    m_tax(s);
}

/* REP #$21 : LDA dp : ADC #n : STA dp : TDC : SEP #$20 */
static void add_dp16(Snes *s, uint8_t cell, uint16_t n)
{
    m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
    m_lda16(s, dp16(s, cell));
    set_dp16(s, cell, s->cpu.a = m_adc16(s, s->cpu.a, n));
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
}

static bool widths_ok(const Snes *s) { return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M; }

/* JSR out; false if the callee didn't come back with 8-bit A and 16-bit X. */
static bool jsr(Snes *s, uint32_t site, uint32_t target)
{
    m_jsr(s, site, target);
    return widths_ok(s);
}

/* LDA #count : STA $8E (: LDY $84 when `load_y`), then `count` tiles from
 * table,X to the tilemap at Y with attribute `attr`. */
static void copy_tile_row(Snes *s, uint16_t table, uint8_t attr, uint8_t count, bool load_y)
{
    set_dp8(s, 0x8E, m_lda8(s, count));
    if (load_y)
        m_set_index(s, &s->cpu.y, dp16(s, 0x84));
    do {
        set_absy8(s, 0x0CC0, m_lda8(s, absx8(s, table)));
        set_absy8(s, 0x0CC1, m_lda8(s, attr));
        m_iny(s);
        m_iny(s);
        m_inx(s);
    } while (step_dp8(s, 0x8E, -1) != 0);
}

/* REP #$20 : SEC : LDA $84 : SBC #$0040 : TAY : TDC : SEP #$20 -- the row above. */
static void row_above(Snes *s)
{
    m_rep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, true);
    m_lda16(s, dp16(s, 0x84));
    s->cpu.a = m_sbc16(s, s->cpu.a, 0x0040);
    m_set_index(s, &s->cpu.y, s->cpu.a);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
}

/* CLC or SEC : LDA $84 : ADC or SBC #n : TAY (16-bit A) */
static void y_from_row(Snes *s, int16_t offset)
{
    Cpu *c = &s->cpu;
    m_set_flag(s, CPU_FLAG_C, offset < 0);
    m_lda16(s, dp16(s, 0x84));
    c->a = offset < 0 ? m_sbc16(s, c->a, (uint16_t)-offset) : m_adc16(s, c->a, (uint16_t)offset);
    m_set_index(s, &c->y, c->a);
}

/* LDA $9F20 (16-bit) : BNE : ... : DEC A : BNE -- the row offset for layouts
 * 0, 1 and 2, into Y. */
static void y_for_layout(Snes *s, int16_t layout0, int16_t layout1, int16_t layout2)
{
    if (m_lda16(s, m_read16(s, m_abs(s, 0x9F20))) == 0)
        y_from_row(s, layout0);
    else
        y_from_row(s, dec_a16(s) == 0 ? layout1 : layout2);
}

/* LDX #$29 : LDA $A10F : BEQ : LDX #$2D : TXA -- the digit attribute: $2D
 * while $A10F is set, $29 otherwise. */
static uint8_t digit_attribute(Snes *s)
{
    m_ldx16(s, 0x0029);
    if (m_lda8(s, abs8(s, 0xA10F)) != 0)
        m_ldx16(s, 0x002D);
    m_txa(s);
    return (uint8_t)s->cpu.a;
}

/* The three digits at $949D-$949F into the tilemap at Y, with `attrs`
 * attribute cells (from $0CC1) set to the digit attribute. */
static void put_3_digits(Snes *s, int attrs)
{
    for (uint16_t i = 0; i < 3; i++)
        set_absy8(s, (uint16_t)(0x0CC0 + 2 * i), m_lda8(s, abs8(s, (uint16_t)(0x949D + i))));
    uint8_t attr = digit_attribute(s);
    for (int k = 0; k < attrs; k++)
        set_absy8(s, (uint16_t)(0x0CC1 + 2 * k), attr);
}

// $C1:0311
/* Party member $80's part of the window (X = $80, Y = its row $84): its
 * name from $9412 (5 tiles on its row, 5 more on the row above, attribute
 * $2D), HP ($5E30, highlighted as in battle_draw_party_status), in layout 0
 * the maximum HP ($5E32) after a $E0 tile ($5F in other layouts), MP ($5E34),
 * and in layouts 1 and 2 the gauge's end tiles $5D/$5E and the gauge
 * ($C1:06F0). False if a callee came back with other register widths. */
static bool draw_member(Snes *s)
{
    Cpu *c = &s->cpu;
    m_lda8(s, m_read8(s, 0xCCF837 + c->x));
    m_tax(s);
    copy_tile_row(s, 0x9412, 0x2D, 5, false);
    row_above(s);
    copy_tile_row(s, 0x9412, 0x2D, 5, false);
    set_abs8(s, 0xA10F, 0x00);

    /* $C1:0351: HP */
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, dp16(s, 0x80));
    c->a = m_asl16(s, c->a);
    m_tax(s);
    set_dp16(s, 0xA2, m_lda16(s, m_read16(s, 0xCCF8ED + c->x)));
    m_tax(s);
    uint16_t hp = m_lda16(s, m_read16(s, m_abs(s, 0x5E30) + c->x));
    m_write16(s, m_abs(s, 0x9499), hp);
    bool low = hp == 0;
    if (!low) {
        m_lda16(s, m_read16(s, m_abs(s, 0x5E32) + c->x));
        m_push16(s, 0x036B);   /* JSR $011B: LSR x3 */
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
    y_for_layout(s, 0x0E, -0x0E, 0x0C);
    if (!jsr(s, 0xC1039A, 0xC1011F) || !jsr(s, 0xC1039D, 0xC1104E))
        return false;
    put_3_digits(s, 4);

    /* $C1:03CA: maximum HP */
    bool wide = m_lda8(s, abs8(s, 0x9F20)) == 0;
    set_absy8(s, 0x0CC6, m_lda8(s, wide ? 0xE0 : 0x5F));
    if (wide) {
        m_rep(s, CPU_FLAG_M);
        m_ldx16(s, dp16(s, 0xA2));
        m_write16(s, m_abs(s, 0x9499), m_lda16(s, m_read16(s, m_abs(s, 0x5E32) + c->x)));
        y_from_row(s, 0x16);
        if (!jsr(s, 0xC103EE, 0xC1011F) || !jsr(s, 0xC103F1, 0xC1104E))
            return false;
        put_3_digits(s, 3);
    }

    /* $C1:041B: MP */
    m_rep(s, CPU_FLAG_M);
    m_ldx16(s, dp16(s, 0xA2));
    m_write16(s, m_abs(s, 0x9499), m_lda16(s, m_read16(s, m_abs(s, 0x5E34) + c->x)));
    y_for_layout(s, 0x1E, -0x06, 0x14);
    if (!jsr(s, 0xC10444, 0xC10174))
        return false;
    wide = m_lda8(s, abs8(s, 0x9F20)) == 0;
    if (!jsr(s, wide ? 0xC1044C : 0xC10451, 0xC1104E))
        return false;
    uint16_t first = wide ? 0x0CC2 : 0x0CC0;
    set_absy8(s, first, m_lda8(s, abs8(s, 0x949E)));
    set_absy8(s, (uint16_t)(first + 2), m_lda8(s, abs8(s, 0x949F)));
    uint8_t attr = digit_attribute(s);
    set_absy8(s, (uint16_t)(first + 1), attr);
    set_absy8(s, (uint16_t)(first + 3), attr);

    /* $C1:0492: gauge */
    if (m_lda8(s, abs8(s, 0x9F20)) != 0) {
        m_rep(s, CPU_FLAG_M);
        y_from_row(s, dec_a16(s) == 0 ? 0x0A : 0x18);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        set_absy8(s, 0x0CC0, m_lda8(s, 0x5D));
        set_absy8(s, 0x0CCA, m_lda8(s, 0x5E));
        set_absy8(s, 0x0CC1, m_lda8(s, 0x29));
        set_absy8(s, 0x0CCB, 0x29);
        if (!jsr(s, 0xC104C0, 0xC106F0))
            return false;
    }
    return true;
}

// $C1:0785
/* The 7x6-tile window for party member A ($80) at row $82 = A * 12 (in the
 * tilemap's first row), from $CC:FA41; with $95F1 set, the member's $9F25 or
 * $9F28 set and either $A117 clear, $A6DE = 3, or member $A115 (another
 * member) negative in $A6D9, from $CC:FA6B instead. Attribute $29. */
static void draw_member_window(Snes *s)
{
    Cpu *c = &s->cpu;
    set_dp8(s, 0x80, (uint8_t)c->a);
    asl_a8(s);
    set_dp8(s, 0x82, asl_a8(s));
    asl_a8(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_a8(s, m_adc8(s, (uint8_t)c->a, dp8(s, 0x82)));
    m_tax(s);
    set_dp16(s, 0x82, c->x);
    uint32_t table = 0xCCFA41;
    if (m_lda8(s, abs8(s, 0x95F1)) != 0) {
        m_lda8(s, dp8(s, 0x80));
        m_tax(s);
        uint8_t v = m_lda8(s, absx8(s, 0x9F25));
        if (m_lda8(s, (uint8_t)(v | absx8(s, 0x9F28))) != 0) {
            if (m_lda8(s, abs8(s, 0xA117)) == 0) {
                table = 0xCCFA6B;
            } else {
                uint8_t mode = m_lda8(s, abs8(s, 0xA6DE));
                m_cmp8(s, mode, 0x03);
                if (mode == 0x03) {
                    table = 0xCCFA6B;
                } else {
                    uint8_t member = m_lda8(s, dp8(s, 0x80));
                    uint8_t other = abs8(s, 0xA115);
                    m_cmp8(s, member, other);
                    if (member != other) {
                        m_ldx16(s, m_read16(s, m_abs(s, 0xA115)));
                        if (m_lda8(s, absx8(s, 0xA6D9)) & 0x80)
                            table = 0xCCFA6B;
                    }
                }
            }
        }
    }
    m_tdc(s);
    m_tax(s);
    set_dp8(s, 0x80, m_lda8(s, 0x06));
    do {
        set_dp8(s, 0x81, m_lda8(s, 0x07));
        m_set_index(s, &c->y, dp16(s, 0x82));
        do {
            set_absy8(s, 0x0CC0, m_lda8(s, m_read8(s, table + c->x)));
            set_absy8(s, 0x0CC1, m_lda8(s, 0x29));
            m_iny(s);
            m_iny(s);
            m_inx(s);
        } while (step_dp8(s, 0x81, -1) != 0);
        add_dp16(s, 0x82, 0x0040);
    } while (step_dp8(s, 0x80, -1) != 0);
}

// $C1:0929
/* Copy 6 rows of 14 bytes (or, with $84 set, 12 bytes skipping 2 per row)
 * from $D1:59FC into the tilemap at $0B40 + $82. */
static void copy_window_frame(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    set_dp8(s, 0x81, m_lda8(s, 0x06));
    do {
        uint8_t count;
        if (m_lda8(s, dp8(s, 0x84)) == 0) {
            count = m_lda8(s, 0x0E);
        } else {
            m_inx(s);
            m_inx(s);
            count = m_lda8(s, 0x0C);
        }
        set_dp8(s, 0x80, count);
        m_set_index(s, &c->y, dp16(s, 0x82));
        do {
            set_absy8(s, 0x0B40, m_lda8(s, m_read8(s, 0xD159FC + c->x)));
            m_inx(s);
            m_iny(s);
        } while (step_dp8(s, 0x80, -1) != 0);
        add_dp16(s, 0x82, 0x0040);
    } while (step_dp8(s, 0x81, -1) != 0);
}

// $C1:08E8
/* Set attribute $29 on 5 tiles in each of two rows at the position for member
 * A ($CC:FA23 in layout 1, $CC:FA29 otherwise). */
static void mark_member_attributes(Snes *s)
{
    Cpu *c = &s->cpu;
    asl_a8(s);
    m_tax(s);
    m_rep(s, CPU_FLAG_M);
    uint32_t table = 0xCCFA29;
    if (m_lda16(s, m_read16(s, m_abs(s, 0x9F20))) != 0 && dec_a16(s) == 0)
        table = 0xCCFA23;
    m_lda16(s, m_read16(s, table + c->x));
    m_tax(s);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, 0x29);
    static const uint16_t cells[10] = { 0x0CC0, 0x0CC2, 0x0CC4, 0x0CC6, 0x0CC8, 0x0D00, 0x0D02, 0x0D04, 0x0D06, 0x0D08 };
    for (int i = 0; i < 10; i++)
        set_absx8(s, cells[i], 0x29);
}

// $C1:081E
/* The window frames of the party members present in $A6D9-$A6DB (not
 * negative) into $0B40, each next to the previous one; member $A6DD's
 * attributes; then ($C1:0872) clear 2 tiles in each of 6 rows at member
 * $95D5's window and, with $A43F set and $9609 clear, put tiles $60-$63 at
 * the position $CC:FADD gives for member $A6DD's entry in $95DC. Clears
 * $A43F. */
static void draw_window_frames(Snes *s)
{
    Cpu *c = &s->cpu;
    set_dp8(s, 0x84, 0x00);
    if (!(m_lda8(s, abs8(s, 0xA6D9)) & 0x80)) {
        m_ldx16(s, 0x0000);
        set_dp16(s, 0x82, c->x);
        m_push16(s, 0x082C);
        copy_window_frame(s);
        m_pull16(s);
    }
    if (!(m_lda8(s, abs8(s, 0xA6DA)) & 0x80)) {
        if (!(m_lda8(s, abs8(s, 0xA6D9)) & 0x80)) {
            set_dp8(s, 0x84, m_lda8(s, 0x01));
            m_ldx16(s, 0x000E);
        } else {
            m_ldx16(s, 0x000C);
        }
        set_dp16(s, 0x82, c->x);
        m_push16(s, 0x0847);
        copy_window_frame(s);
        m_pull16(s);
        set_dp8(s, 0x84, 0x00);
    }
    if (!(m_lda8(s, abs8(s, 0xA6DB)) & 0x80)) {
        if (!(m_lda8(s, abs8(s, 0xA6DA)) & 0x80)) {
            set_dp8(s, 0x84, m_lda8(s, 0x01));
            m_ldx16(s, 0x001A);
        } else {
            m_ldx16(s, 0x0018);
        }
        set_dp16(s, 0x82, c->x);
        m_push16(s, 0x0864);
        copy_window_frame(s);
        m_pull16(s);
    }
    m_lda8(s, abs8(s, 0xA6DD));
    m_tax(s);
    m_lda8(s, absx8(s, 0xA6D9));
    m_push16(s, 0x086E);
    mark_member_attributes(s);
    m_pull16(s);
    uint8_t flag = (uint8_t)(abs8(s, 0xA43F) + 1);
    set_abs8(s, 0xA43F, flag);
    m_set_nz8(s, flag);

    /* $C1:0872 */
    times_12_to_x(s, abs8(s, 0x95D5), 0x80);
    static const uint16_t cells[12] = { 0x0CC0, 0x0D00, 0x0D40, 0x0D80, 0x0DC0, 0x0E00,
                                        0x0CC2, 0x0D02, 0x0D42, 0x0D82, 0x0DC2, 0x0E02 };
    for (int i = 0; i < 12; i++)
        set_absx8(s, cells[i], 0x00);
    if (m_lda8(s, abs8(s, 0xA43F)) != 0) {
        m_lda8(s, abs8(s, 0xA6DD));
        m_tax(s);
        asl_a8(s);
        set_dp8(s, 0x80, asl_a8(s));
        asl_a8(s);
        m_set_flag(s, CPU_FLAG_C, false);
        m_set_a8(s, m_adc8(s, (uint8_t)c->a, dp8(s, 0x80)));
        m_set_index(s, &c->y, c->a);
        set_dp16(s, 0x80, c->y);
        m_lda8(s, absx8(s, 0xA6D9));
        m_tax(s);
        m_lda8(s, absx8(s, 0x95DC));
        asl_a8(s);
        m_tax(s);
        m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
        m_lda16(s, m_read16(s, 0xCCFADD + c->x));
        c->a = m_adc16(s, c->a, dp16(s, 0x80));
        m_tax(s);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        if (m_lda8(s, abs8(s, 0x9609)) == 0) {
            set_absx8(s, 0x0CC0, m_lda8(s, 0x60));
            set_absx8(s, 0x0CC2, m_lda8(s, 0x61));
            set_absx8(s, 0x0D00, m_lda8(s, 0x62));
            set_absx8(s, 0x0D02, m_lda8(s, 0x63));
        }
    }
    set_abs8(s, 0xA43F, 0x00);
}

// $C1:0299
/* Redraw the battle window's tilemap ($0CC0): clear 192 tiles (attribute
 * $29), put the corner tiles $64-$66 for layout $9F20, then for each party
 * member present ($96F5) its name, HP, MP and gauge on rows from $5A ($68 in
 * layout 1), $80 apart; for each of three entries with $98CC set, 11 tiles
 * from $ACBC on its row and 11 on the row above, rows from $42. Then, when
 * $A6DE is set: with $A86B set and $A62D differing from $A0D7, set $A6DF to
 * $FE and mark member $A62D's attributes; otherwise, unless $95DB is set,
 * set $95F1 = $A6DE - 1, make $A6DD point at a present member ($A6D9 not
 * negative) and store that entry in $95D5, and draw each present member's
 * window and the window frames. */
void battle_draw_window(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    m_set_index(s, &c->y, c->a);
    do {
        m_tdc(s);
        set_absy8(s, 0x0CC0, (uint8_t)c->a);
        set_absy8(s, 0x0CC1, m_lda8(s, 0x29));
        m_iny(s);
        m_iny(s);
        m_inx(s);
        m_cmp16(s, c->x, 0x00C0);
    } while (c->x != 0x00C0);

    static const uint16_t corners[3][4] = {
        { 0x0CE8, 0x0CEA, 0x0CFC, 0x0CFA },
        { 0x0CDA, 0x0CDC, 0x0CE4, 0x0CE2 },
        { 0x0CE6, 0x0CE8, 0x0CF0, 0x0CEE },
    };
    int layout = 0;
    if (m_lda8(s, abs8(s, 0x9F20)) != 0)
        layout = dec_a8(s) == 0 ? 1 : 2;
    set_abs8(s, corners[layout][0], m_lda8(s, 0x64));
    set_abs8(s, corners[layout][1], m_lda8(s, 0x66));
    set_abs8(s, corners[layout][2], 0x66);
    set_abs8(s, corners[layout][3], m_lda8(s, 0x65));

    /* $C1:02EF: party members */
    uint16_t row = 0x005A;
    if (m_lda8(s, abs8(s, 0x9F20)) != 0 && dec_a8(s) == 0)
        row = 0x0068;
    set_dp16(s, 0x84, m_ldx16(s, row));
    m_tdc(s);
    m_tax(s);
    set_dp16(s, 0x80, c->x);
    uint8_t n;
    do {
        m_set_index(s, &c->y, dp16(s, 0x84));
        m_ldx16(s, dp16(s, 0x80));
        if (m_lda8(s, absx8(s, 0x96F5)) != 0 && !draw_member(s))
            return;
        add_dp16(s, 0x84, 0x0080);
        step_dp8(s, 0x80, +1);
        n = m_lda8(s, dp8(s, 0x80));
        m_cmp8(s, n, 0x03);
    } while (n != 0x03);

    /* $C1:04DC: three entries of 11+11 tiles from $ACBC */
    set_dp16(s, 0x84, m_ldx16(s, 0x0042));
    m_tdc(s);
    m_tax(s);
    set_dp16(s, 0x80, c->x);
    do {
        m_ldx16(s, dp16(s, 0x80));
        if (m_lda8(s, absx8(s, 0x98CC)) != 0) {
            m_lda8(s, dp8(s, 0x80));
            asl_a8(s);
            asl_a8(s);
            set_dp8(s, 0x8E, asl_a8(s));
            asl_a8(s);
            m_set_flag(s, CPU_FLAG_C, false);
            m_set_a8(s, m_adc8(s, (uint8_t)c->a, dp8(s, 0x8E)));
            m_tax(s);
            copy_tile_row(s, 0xACBC, 0x29, 0x0B, true);
            row_above(s);
            m_inx(s);
            copy_tile_row(s, 0xACBC, 0x29, 0x0B, false);
            add_dp16(s, 0x84, 0x0080);
        }
        step_dp8(s, 0x80, +1);
        n = m_lda8(s, dp8(s, 0x80));
        m_cmp8(s, n, 0x03);
    } while (n != 0x03);

    /* $C1:0547 */
    if (m_lda8(s, abs8(s, 0xA6DE)) == 0) {
        m_rts(s);
        return;
    }
    if (m_lda8(s, abs8(s, 0xA86B)) != 0) {
        uint8_t member = m_lda8(s, abs8(s, 0xA62D));
        uint8_t shown = abs8(s, 0xA0D7);
        m_cmp8(s, member, shown);
        if (member != shown) {
            set_abs8(s, 0xA6DF, m_lda8(s, 0xFE));
            m_lda8(s, abs8(s, 0xA62D));
            mark_member_attributes(s);   /* JMP $08E8 */
        }
        m_rts(s);
        return;
    }
    if (m_lda8(s, abs8(s, 0x95DB)) != 0) {
        m_rts(s);
        return;
    }
    m_lda8(s, abs8(s, 0xA6DE));
    set_abs8(s, 0x95F1, dec_a8(s));
    m_lda8(s, abs8(s, 0xA6DD));
    m_tax(s);
    if (m_lda8(s, absx8(s, 0xA6D9)) & 0x80) {
        m_tdc(s);
        m_tax(s);
        while (m_lda8(s, absx8(s, 0xA6D9)) & 0x80)
            m_inx(s);
        m_push8(s, (uint8_t)c->a);
        m_txa(s);
        set_abs8(s, 0xA6DD, (uint8_t)c->a);
        m_lda8(s, m_pull8(s));
    }
    set_abs8(s, 0x95D5, (uint8_t)c->a);
    m_tdc(s);
    m_tax(s);
    set_dp16(s, 0x86, c->x);
    do {
        m_ldx16(s, dp16(s, 0x86));
        if (!(m_lda8(s, absx8(s, 0xA6D9)) & 0x80)) {
            m_push16(s, 0x059A);
            draw_member_window(s);
            m_pull16(s);
        }
        step_dp8(s, 0x86, +1);
        n = m_lda8(s, dp8(s, 0x86));
        m_cmp8(s, n, 0x03);
    } while (n != 0x03);
    m_push16(s, 0x05A5);
    draw_window_frames(s);
    m_pull16(s);
    m_rts(s);
}
