/* Battle: the menu window.
 *
 * The window's tilemap is built in a buffer of (tile, attribute) pairs at
 * $A6E1 (rows of $40 bytes); $95D5 is the party member whose menu is open
 * (negative: none), $95DB the menu page (0 and 1 are drawn here, presumably
 * the command list and the tech list). The cursor sprites are four 4-byte
 * entries at $0700 (x, y, tile, attributes) with their size and visibility
 * bits in $0900. */
#include "battle/menu_window.h"

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

/* INC abs / INC dp, 8-bit. */
static void inc_abs8(Snes *s, uint16_t addr)
{
    uint8_t v = (uint8_t)(abs8(s, addr) + 1);
    set_abs8(s, addr, v);
    m_set_nz8(s, v);
}

static void inc_dp8(Snes *s, uint8_t cell)
{
    uint8_t v = (uint8_t)(dp8(s, cell) + 1);
    set_dp8(s, cell, v);
    m_set_nz8(s, v);
}

/* ADC / SBC with the 8-bit accumulator. */
static void adc_a(Snes *s, uint8_t v) { m_set_a8(s, m_adc8(s, (uint8_t)s->cpu.a, v)); }
static void sbc_a(Snes *s, uint8_t v) { m_set_a8(s, m_sbc8(s, (uint8_t)s->cpu.a, v)); }

/* ASL A, 8-bit. */
static void asl_a(Snes *s) { m_set_a8(s, m_asl8(s, (uint8_t)s->cpu.a)); }

/* TAY at the current X width. */
static void tay(Snes *s) { m_set_index(s, &s->cpu.y, s->cpu.a); }

static bool m8_x16(Snes *s) { return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M; }

/* JSR / JSL out; false if the callee didn't come back with 8-bit A and 16-bit X. */
static bool call(Snes *s, uint32_t site, uint32_t target)
{
    m_jsr(s, site, target);
    return m8_x16(s);
}

static bool call_long(Snes *s, uint32_t site, uint32_t target)
{
    m_jsl(s, site, target);
    return m8_x16(s);
}

/* A local subroutine the JSR at `site` calls: its return address is pushed,
 * and pulled again by the RTS that ends `body`. */
static void local_call(Snes *s, uint16_t site, void (*body)(Snes *))
{
    m_push16(s, (uint16_t)(site + 2));
    body(s);
    m_pull16(s);
}

// $C1:0EBA
/* Put tile $FF in the twelve cells $A6E3/$A6E5 + $40 * row, rows 0-5. */
static void clear_cursor_column(Snes *s)
{
    m_lda8(s, 0xFF);
    for (uint16_t row = 0; row < 6; row++) {
        set_abs8(s, (uint16_t)(0xA6E3 + 0x40 * row), 0xFF);
        set_abs8(s, (uint16_t)(0xA6E5 + 0x40 * row), 0xFF);
    }
}

// $C1:011A
/* LSR A four times. */
static void shift_right_4(Snes *s)
{
    for (int i = 0; i < 4; i++)
        m_set_a8(s, m_lsr8(s, (uint8_t)s->cpu.a));
}

// $C1:103E
/* Z clear if party slot $80 ($A6D9 + $80) is empty (negative) or its member's
 * $A0D1 is set. */
static void member_unavailable(Snes *s)
{
    m_lda8(s, dp8(s, 0x80));
    m_tax(s);
    uint8_t member = m_lda8(s, absx8(s, 0xA6D9));
    if (member & 0x80)
        return;
    m_tax(s);
    if (m_lda8(s, absx8(s, 0xA0D1)) != 0)
        return;
    m_tdc(s);
}

// $C1:0EE1
/* Tech list attributes: for the three entries of member $95D5's list on the
 * page ($CC:F38C + member, plus $95EB + member), whose tech data are 7-byte
 * records at $1A80 ($CC:F395 + 2 * member plus $CC:F3A1 + tech), dim the row
 * (attribute $2D, and bit 7 of the record's $1A82) when any cost byte
 * ($1A83, $1A85, $1A86, unless negative) exceeds the matching party member's
 * $5E34 (presumably MP), when a partner named by $1A84's nibbles is missing
 * or has $A0D1 set, when $A0D1 + $95D5 is set, or when the record's $1A80 is
 * $74 and $1C48 + $A0FF isn't $42. Entries $FC and $FD (list markers,
 * $94D0 >= $F0) are dimmed depending on $A6DE. The attribute goes into the
 * 18 cells of both lines of the row at $CC:F39B. Sets $A099 at the end. */
static void mark_tech_list(Snes *s)
{
    Cpu *c = &s->cpu;
    m_lda8(s, abs8(s, 0x95D5));
    asl_a(s);
    m_tax(s);
    set_dp8(s, 0xAF, m_lda8(s, m_read8(s, 0xCCF395 + c->x)));
    set_dp8(s, 0xB0, m_lda8(s, m_read8(s, 0xCCF396 + c->x)));
    m_lda8(s, abs8(s, 0x95D5));
    m_tax(s);
    tay(s);
    set_dp8(s, 0x86, m_lda8(s, m_read8(s, 0xCCF38C + c->x)));
    m_lda8(s, m_read8(s, m_abs(s, 0x95EB) + c->y));
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_a8(s, m_adc8(s, (uint8_t)c->a, dp8(s, 0x86)));
    m_tax(s);
    set_dp16(s, 0x86, c->x);
    for (uint16_t i = 0; i < 3; i++) {
        set_dp8(s, (uint8_t)(0x96 + 2 * i), m_lda8(s, abs8(s, (uint16_t)(0x5E34 + 0x80 * i))));
        set_dp8(s, (uint8_t)(0x97 + 2 * i), m_lda8(s, abs8(s, (uint16_t)(0x5E35 + 0x80 * i))));
    }
    m_tdc(s);
    m_tax(s);
    set_dp16(s, 0x82, c->x);

    static const struct { uint16_t cost; uint8_t mp; } costs[3] = { { 0x1A83, 0x96 }, { 0x1A85, 0x98 }, { 0x1A86, 0x9A } };
    uint8_t row;
    do {
        /* $C1:0F28 */
        m_lda8(s, dp8(s, 0x82));
        asl_a(s);
        m_tax(s);
        set_dp8(s, 0x84, m_lda8(s, m_read8(s, 0xCCF39B + c->x)));
        set_dp8(s, 0x85, m_lda8(s, m_read8(s, 0xCCF39C + c->x)));
        set_dp8(s, 0x94, m_lda8(s, 0x29));
        m_ldx16(s, dp16(s, 0x86));
        uint8_t kind = m_lda8(s, (uint8_t)(m_lda8(s, absx8(s, 0x94D0)) & 0xF0));
        m_cmp8(s, kind, 0xF0);
        if (kind == 0xF0) {
            uint8_t entry = m_lda8(s, absx8(s, 0x94D0));
            bool dim = false;
            m_cmp8(s, entry, 0xFC);
            if (entry == 0xFC) {
                uint8_t n = m_lda8(s, abs8(s, 0xA6DE));
                m_cmp8(s, n, 0x02);
                dim = n < 0x02;
            } else {
                m_cmp8(s, entry, 0xFD);
                if (entry == 0xFD) {
                    uint8_t n = m_lda8(s, abs8(s, 0xA6DE));
                    m_cmp8(s, n, 0x03);
                    dim = n < 0x03;
                }
            }
            if (dim)
                set_dp8(s, 0x94, m_lda8(s, 0x2D));
        } else {
            /* $C1:0F69 */
            m_lda8(s, absx8(s, 0x9551));
            m_tax(s);
            m_lda8(s, m_read8(s, 0xCCF3A1 + c->x));
            m_set_flag(s, CPU_FLAG_C, false);
            m_set_a8(s, m_adc8(s, (uint8_t)c->a, dp8(s, 0xAF)));
            set_dp8(s, 0x88, (uint8_t)c->a);
            m_lda8(s, dp8(s, 0xB0));
            m_set_a8(s, m_adc8(s, (uint8_t)c->a, 0x00));
            set_dp8(s, 0x89, (uint8_t)c->a);
            m_ldx16(s, dp16(s, 0x88));
            bool dim = false;
            for (int i = 0; i < 3 && !dim; i++) {
                if (m_lda8(s, absx8(s, costs[i].cost)) & 0x80)
                    continue;
                m_set_flag(s, CPU_FLAG_C, true);
                m_lda8(s, dp8(s, costs[i].mp));
                sbc_a(s, absx8(s, costs[i].cost));
                m_lda8(s, dp8(s, (uint8_t)(costs[i].mp + 1)));
                sbc_a(s, 0x00);
                dim = !m_flag(s, CPU_FLAG_C);
            }
            if (!dim) {
                /* $C1:0FB1 */
                uint8_t partners = m_lda8(s, absx8(s, 0x1A84));
                set_dp8(s, 0x81, partners);
                m_cmp8(s, partners, 0xFF);
                if (partners != 0xFF) {
                    set_dp8(s, 0x80, m_lda8(s, partners & 0x0F));
                    local_call(s, 0x0FBE, member_unavailable);
                    if (!m_flag(s, CPU_FLAG_Z)) {
                        dim = true;
                    } else {
                        uint8_t high = m_lda8(s, (uint8_t)(m_lda8(s, dp8(s, 0x81)) & 0xF0));
                        m_cmp8(s, high, 0xF0);
                        if (high != 0xF0) {
                            local_call(s, 0x0FCB, shift_right_4);
                            set_dp8(s, 0x80, (uint8_t)c->a);
                            local_call(s, 0x0FD0, member_unavailable);
                            dim = !m_flag(s, CPU_FLAG_Z);
                        }
                    }
                }
            }
            if (!dim) {
                /* $C1:0FD5 */
                m_lda8(s, abs8(s, 0x95D5));
                m_tax(s);
                if (m_lda8(s, absx8(s, 0xA0D1)) != 0) {
                    dim = true;
                } else {
                    m_ldx16(s, dp16(s, 0x88));
                    uint8_t id = m_lda8(s, absx8(s, 0x1A80));
                    m_cmp8(s, id, 0x74);
                    if (id == 0x74) {
                        m_ldx16(s, m_read16(s, m_abs(s, 0xA0FF)));
                        uint8_t v = m_lda8(s, absx8(s, 0x1C48));
                        m_cmp8(s, v, 0x42);
                        dim = v != 0x42;
                    }
                }
            }
            m_ldx16(s, dp16(s, 0x88));
            uint8_t flags = m_lda8(s, absx8(s, 0x1A82));
            if (dim) {
                set_absx8(s, 0x1A82, m_lda8(s, flags | 0x80));
                set_dp8(s, 0x94, m_lda8(s, 0x2D));
            } else {
                set_absx8(s, 0x1A82, m_lda8(s, flags & 0x7F));
                set_dp8(s, 0x94, m_lda8(s, 0x29));
            }
        }

        /* $C1:100F */
        static const uint16_t lines[2] = { 0xA6E2, 0xA722 };
        for (int l = 0; l < 2; l++) {
            m_ldx16(s, dp16(s, 0x84));
            m_ldy(s, 0x0012);
            if (l == 0)
                m_lda8(s, dp8(s, 0x94));
            do {
                set_absx8(s, lines[l], (uint8_t)c->a);
                m_inx(s);
                m_inx(s);
                m_set_index(s, &c->y, (uint16_t)(c->y - 1));
            } while (c->y != 0);
        }
        inc_dp8(s, 0x86);
        inc_dp8(s, 0x82);
        row = m_lda8(s, dp8(s, 0x82));
        m_cmp8(s, row, 0x03);
    } while (row != 0x03);
    set_abs8(s, 0xA099, m_lda8(s, 0x01));
}

// $C1:0872
/* Clear party member $95D5's cells in the six lines of the tilemap at $0CC0
 * ($0CC0/$0CC2 + $40 * line + 12 * member); then, when $A43F is set and
 * $9609 clear, put tiles $60-$63 at the position for the member in menu slot
 * $A6DD ($CC:FADD by its $95DC, plus 12 * slot). Clears $A43F. */
static void clear_member_cells(Snes *s)
{
    Cpu *c = &s->cpu;
    m_lda8(s, abs8(s, 0x95D5));
    asl_a(s);
    asl_a(s);
    set_dp8(s, 0x80, (uint8_t)c->a);
    asl_a(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_a8(s, m_adc8(s, (uint8_t)c->a, dp8(s, 0x80)));
    m_tax(s);
    for (uint16_t k = 0; k < 4; k += 2)
        for (uint16_t line = 0; line < 6; line++)
            set_absx8(s, (uint16_t)(0x0CC0 + k + 0x40 * line), 0x00);
    if (m_lda8(s, abs8(s, 0xA43F)) != 0) {
        m_lda8(s, abs8(s, 0xA6DD));
        m_tax(s);
        asl_a(s);
        asl_a(s);
        set_dp8(s, 0x80, (uint8_t)c->a);
        asl_a(s);
        m_set_flag(s, CPU_FLAG_C, false);
        m_set_a8(s, m_adc8(s, (uint8_t)c->a, dp8(s, 0x80)));
        tay(s);
        set_dp16(s, 0x80, c->y);
        m_lda8(s, absx8(s, 0xA6D9));
        m_tax(s);
        m_lda8(s, absx8(s, 0x95DC));
        asl_a(s);
        m_tax(s);
        m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
        c->a = m_adc16(s, m_lda16(s, m_read16(s, 0xCCFADD + c->x)), dp16(s, 0x80));
        m_tax(s);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        if (m_lda8(s, abs8(s, 0x9609)) == 0) {
            static const uint16_t cells[4] = { 0x0CC0, 0x0CC2, 0x0D00, 0x0D02 };
            for (int i = 0; i < 4; i++)
                set_absx8(s, cells[i], m_lda8(s, (uint8_t)(0x60 + i)));
        }
    }
    set_abs8(s, 0xA43F, 0x00);
}

/* $C1:0C49: page 0. */
static void draw_command_page(Snes *s)
{
    if (m_lda8(s, abs8(s, 0x95DB)) != 0) {
        m_rts(s);
        return;
    }
    uint8_t slot = m_lda8(s, abs8(s, 0xA6DD));
    uint8_t shown = abs8(s, 0xA6DF);
    m_cmp8(s, slot, shown);
    if (slot != shown) {
        set_abs8(s, 0xA6DF, slot);
        if (!call(s, 0xC10C59, 0xC11C3A) || !call(s, 0xC10C5C, 0xC10299))
            return;
    } else {
        if (!call(s, 0xC10C61, 0xC105A7))
            return;
        if (m_lda8(s, abs8(s, 0xA43F)) != 0 && !(m_lda8(s, abs8(s, 0xA6DD)) & 0x80))
            local_call(s, 0x0C6E, clear_member_cells);
    }
    if ((m_lda8(s, abs8(s, 0x95D5)) & 0x80) && !call(s, 0xC10C76, 0xC11C3A))
        return;
    if (!call_long(s, 0xC10C79, 0xCFFD02))
        return;
    inc_abs8(s, 0x99E2);
    m_rts(s);
}

/* $C1:0C83: page 1. */
static void draw_tech_page(Snes *s)
{
    Cpu *c = &s->cpu;
    if (!call(s, 0xC10C83, 0xC10A88))
        return;
    m_tdc(s);
    tay(s);
    m_tax(s);
    do {
        set_absy8(s, 0xA6E1, m_lda8(s, absx8(s, 0x99E3)));
        m_inx(s);
        m_inx(s);
        m_iny(s);
        m_iny(s);
        m_cmp16(s, c->y, 0x0180);
    } while (c->y != 0x0180);

    /* $C1:0C98: nine attribute bytes per row from $CC:FA95 */
    m_tdc(s);
    m_tax(s);
    tay(s);
    set_dp16(s, 0x80, c->x);
    uint8_t n;
    do {
        set_dp8(s, 0x81, 0x00);
        do {
            set_absy8(s, 0xA70B, m_lda8(s, m_read8(s, 0xCCFA95 + c->x)));
            m_inx(s);
            m_iny(s);
            m_iny(s);
            inc_dp8(s, 0x81);
            n = m_lda8(s, dp8(s, 0x81));
            m_cmp8(s, n, 0x09);
        } while (n != 0x09);
        m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
        m_lda16(s, c->y);
        c->a = m_adc16(s, c->a, 0x002E);
        tay(s);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        inc_dp8(s, 0x80);
        n = m_lda8(s, dp8(s, 0x80));
        m_cmp8(s, n, 0x06);
    } while (n != 0x06);

    /* $C1:0CC3: each party member's $5E34 as two digits, or a blank */
    static const struct {
        uint16_t present, value, tens, ones, blank1, blank2, absent;
        uint32_t digits_site, blank_site;
    } members[3] = {
        { 0x96F5, 0x5E34, 0xA751, 0xA753, 0xA75B, 0xA75D, 0xA757, 0xC10CD0, 0xC10CD3 },
        { 0x96F6, 0x5EB4, 0xA7D1, 0xA7D3, 0xA7DB, 0xA7DD, 0xA7D7, 0xC10CFE, 0xC10D01 },
        { 0x96F7, 0x5F34, 0xA851, 0xA853, 0xA85B, 0xA85D, 0xA857, 0xC10D2C, 0xC10D2F },
    };
    for (int i = 0; i < 3; i++) {
        if (m_lda8(s, abs8(s, members[i].present)) != 0) {
            m_rep(s, CPU_FLAG_M);
            m_write16(s, m_abs(s, 0x9499), m_lda16(s, m_read16(s, m_abs(s, members[i].value))));
            if (!call(s, members[i].digits_site, 0xC10174) || !call(s, members[i].blank_site, 0xC1104E))
                return;
            set_abs8(s, members[i].tens, m_lda8(s, abs8(s, 0x949E)));
            set_abs8(s, members[i].ones, m_lda8(s, abs8(s, 0x949F)));
            m_lda8(s, 0xFF);
            set_abs8(s, members[i].blank1, 0xFF);
            set_abs8(s, members[i].blank2, 0xFF);
        } else {
            m_lda8(s, 0xFF);
            set_abs8(s, members[i].absent, 0xFF);
        }
    }

    /* $C1:0D4D: the costs of the tech at the list position */
    m_lda8(s, abs8(s, 0x95D5));
    m_tax(s);
    set_dp8(s, 0xAF, m_lda8(s, m_read8(s, 0xCCF38C + c->x)));
    m_set_flag(s, CPU_FLAG_C, false);
    m_lda8(s, absx8(s, 0x95EB));
    adc_a(s, absx8(s, 0x95DF));
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_a8(s, m_adc8(s, (uint8_t)c->a, dp8(s, 0xAF)));
    m_tax(s);
    uint8_t tech = m_lda8(s, absx8(s, 0x9551));
    m_cmp8(s, tech, 0xFF);
    if (tech == 0xFF) {
        set_abs8(s, 0xA7DB, 0xFF);
        set_abs8(s, 0xA7DD, 0xFF);
        for (uint16_t k = 0; k < 7; k++)
            set_abs8(s, (uint16_t)(0x9EE3 + k), 0xFF);
    } else {
        /* $C1:0D87 */
        set_dp8(s, 0x80, tech);
        m_lda8(s, abs8(s, 0x95D5));
        asl_a(s);
        m_tax(s);
        set_dp8(s, 0xAF, m_lda8(s, m_read8(s, 0xCCF395 + c->x)));
        set_dp8(s, 0xB0, m_lda8(s, m_read8(s, 0xCCF396 + c->x)));
        m_lda8(s, dp8(s, 0x80));
        m_tax(s);
        set_dp8(s, 0x80, m_lda8(s, m_read8(s, 0xCCF3A1 + c->x)));
        m_set_flag(s, CPU_FLAG_C, false);
        m_lda8(s, dp8(s, 0xAF));
        adc_a(s, dp8(s, 0x80));
        set_dp8(s, 0xAF, (uint8_t)c->a);
        m_lda8(s, dp8(s, 0xB0));
        adc_a(s, 0x00);
        set_dp8(s, 0xB0, (uint8_t)c->a);
        m_ldx16(s, dp16(s, 0xAF));
        for (uint16_t k = 0; k < 7; k++)
            set_abs8(s, (uint16_t)(0x9EE3 + k), m_lda8(s, absx8(s, (uint16_t)(0x1A80 + k))));
        static const struct { uint16_t cost; uint32_t site; uint16_t right, left; } costs[3] = {
            { 0x9EE6, 0xC10DE6, 0xA75D, 0xA75B },
            { 0x9EE8, 0xC10E10, 0xA7DD, 0xA7DB },
            { 0x9EE9, 0xC10E3A, 0xA85D, 0xA85B },
        };
        for (int i = 0; i < 3; i++) {
            if (m_lda8(s, abs8(s, costs[i].cost)) & 0x80)
                continue;
            m_rep(s, CPU_FLAG_M);
            m_write16(s, m_abs(s, 0x9499), c->a);
            if (!call(s, costs[i].site, 0xC10174))
                return;
            uint8_t tens = m_lda8(s, abs8(s, 0x949E));
            m_cmp8(s, tens, 0x73);
            if (tens == 0x73) {
                set_abs8(s, costs[i].right, m_lda8(s, 0xFF));
                set_abs8(s, costs[i].left, m_lda8(s, abs8(s, 0x949F)));
            } else {
                set_abs8(s, costs[i].left, tens);
                set_abs8(s, costs[i].right, m_lda8(s, abs8(s, 0x949F)));
            }
        }
    }

    /* $C1:0E5A */
    m_lda8(s, abs8(s, 0x95D5));
    m_tax(s);
    set_dp8(s, 0x80, m_lda8(s, absx8(s, 0x95DF)));
    local_call(s, 0x0E63, clear_cursor_column);
    if (m_lda8(s, abs8(s, 0x9609)) == 0) {
        m_lda8(s, dp8(s, 0x80));
        asl_a(s);
        m_tax(s);
        m_rep(s, CPU_FLAG_M);
        m_lda16(s, m_read16(s, 0xCCFAE3 + c->x));
        tay(s);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        static const uint16_t cells[4] = { 0xA6E1, 0xA6E3, 0xA721, 0xA723 };
        for (int i = 0; i < 4; i++)
            set_absy8(s, cells[i], m_lda8(s, (uint8_t)(0x60 + i)));
    }
    if (m_lda8(s, abs8(s, 0xA0DB)) & 0x80) {
        m_lda8(s, abs8(s, 0x9EE3));
        if (!call_long(s, 0xC10E95, 0xCD0027))
            return;
    }
    if (!call_long(s, 0xC10E99, 0xCFFD36))
        return;
    if (m_lda8(s, abs8(s, 0xA86A)) != 0) {
        m_tdc(s);
        m_tax(s);
        do {
            set_absx8(s, 0x0B40, m_lda8(s, m_read8(s, 0xD15A50 + c->x)));
            m_inx(s);
            m_cmp16(s, c->x, 0x0180);
        } while (c->x != 0x0180);
        inc_abs8(s, 0x99E2);
        set_abs8(s, 0xA86A, 0x00);
    }
    m_rts(s);
}

// $C1:0C2D
/* Draw the menu window for party member $95D5 after $CF:FD9E. Page 0 ($95DB):
 * when the menu slot $A6DD differs from $A6DF, remember it and rebuild the
 * window ($C1:1C3A, $C1:0299); otherwise redraw the party status ($C1:05A7)
 * and, with $A43F set, the member's cells ($C1:0872); then $CF:FD02. Page 1:
 * the tech list ($C1:0A88 and $C1:0EE1 lay it out and set its attributes),
 * the three members' $5E34 in two digits, the costs of the tech under the
 * cursor ($9EE3-$9EE9 copied from its record), the cursor tiles, then
 * $CD:0027 (when $A0DB is negative) and $CF:FD36, and, when $A86A is set, a
 * reload of $0B40-$0CBF from $D1:5A50. Other pages draw nothing. */
void battle_draw_menu_window(Snes *s)
{
    m_lda8(s, abs8(s, 0x95D5));
    if (!call_long(s, 0xC10C30, 0xCFFD9E))
        return;
    uint8_t page = m_lda8(s, abs8(s, 0x95DB));
    if (page == 0) {
        draw_command_page(s);
        return;
    }
    m_cmp8(s, page, 0x01);
    if (page != 0x01) {
        m_rts(s);
        return;
    }
    local_call(s, 0x0C40, mark_tech_list);
    draw_tech_page(s);
}

/* CLC : LDA $1D0C,X : ADC $9708,X : SEC : SBC #$10 : STA x_cell : CLC :
 * LDA $1D23,X : ADC $9713,X : STA x_cell+1 -- a cursor at object X. */
static void place_at_object(Snes *s, uint32_t x_cell)
{
    Cpu *c = &s->cpu;
    m_set_flag(s, CPU_FLAG_C, false);
    m_lda8(s, absx8(s, 0x1D0C));
    adc_a(s, absx8(s, 0x9708));
    m_set_flag(s, CPU_FLAG_C, true);
    m_set_a8(s, m_sbc8(s, (uint8_t)c->a, 0x10));
    m_write8(s, x_cell, (uint8_t)c->a);
    m_set_flag(s, CPU_FLAG_C, false);
    m_lda8(s, absx8(s, 0x1D23));
    adc_a(s, absx8(s, 0x9713));
    m_write8(s, x_cell + 1, (uint8_t)c->a);
}

/* LDA $0900 : AND #mask : STA $0900 */
static void and_0900(Snes *s, uint8_t mask)
{
    set_abs8(s, 0x0900, m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0x0900)) & mask)));
}

/* LDA $0900 : AND long : STA $0900 */
static void and_0900_long(Snes *s, uint32_t addr)
{
    uint8_t v = m_lda8(s, abs8(s, 0x0900));
    set_abs8(s, 0x0900, m_lda8(s, (uint8_t)(v & m_read8(s, addr))));
}

/* LDA $0900 : ORA #$55 : STA $0900 */
static void hide_cursors(Snes *s)
{
    set_abs8(s, 0x0900, m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0x0900)) | 0x55)));
}

/* $C1:17FE: page 1, a combination tech's cursors on its members. */
static bool place_tech_cursors(Snes *s)
{
    uint8_t v = m_lda8(s, abs8(s, 0x9EE3));
    m_cmp8(s, v, 0x39);
    bool single = v < 0x39;
    if (!single) {
        uint8_t partners = m_lda8(s, abs8(s, 0x9EE7));
        m_cmp8(s, partners, 0xFF);
        single = partners == 0xFF;
    }
    if (single)
        return call(s, 0xC118AA, 0xC11115);
    hide_cursors(s);
    m_lda8(s, 0xF0);
    for (uint16_t i = 0; i < 4; i++)
        set_abs8(s, (uint16_t)(0x0701 + 4 * i), 0xF0);
    m_lda8(s, 0x32);
    for (uint16_t i = 0; i < 3; i++)
        set_abs8(s, (uint16_t)(0x0703 + 4 * i), 0x32);
    for (uint16_t i = 0; i < 3; i++)
        set_abs8(s, (uint16_t)(0x0702 + 4 * i), 0x00);
    m_lda8(s, abs8(s, 0x95D5));
    m_tax(s);
    place_at_object(s, m_abs(s, 0x0700));
    m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0x9EE7)) & 0x0F));
    m_tax(s);
    place_at_object(s, m_abs(s, 0x0704));
    set_abs8(s, 0x0900, m_lda8(s, (uint8_t)(m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0x0900)) & 0xFE)) & 0xFB)));
    uint8_t high = m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0x9EE7)) & 0xF0));
    m_cmp8(s, high, 0xF0);
    if (high != 0xF0) {
        local_call(s, 0x1884, shift_right_4);
        m_tax(s);
        place_at_object(s, m_abs(s, 0x0708));
        and_0900(s, 0xEF);
    }
    return true;
}

/* $C1:18B0: other pages, the tiles at $0EFC/$0EFD and $0FFC/$0FFD by $95E6
 * and the cursor tiles at the window position for $95E5 ($CC:FAE9), moved
 * when either changed; then $CF:FD6A when anything was drawn. */
static bool place_list_cursor(Snes *s)
{
    Cpu *c = &s->cpu;
    set_dp8(s, 0x80, 0x00);
    uint8_t arrows = m_lda8(s, abs8(s, 0x95E6));
    uint8_t shown = abs8(s, 0x9920);
    m_cmp8(s, arrows, shown);
    if (arrows != shown) {
        arrows = m_lda8(s, abs8(s, 0x95E6));
        if (arrows == 0) {
            set_abs8(s, 0x0EFC, m_lda8(s, 0xFF));
            set_abs8(s, 0x0FFC, m_lda8(s, 0x7F));
            m_lda8(s, 0xA9);
            set_abs8(s, 0x0EFD, 0xA9);
            set_abs8(s, 0x0FFD, 0xA9);
        } else {
            arrows = m_lda8(s, abs8(s, 0x95E6));
            m_cmp8(s, arrows, 0xFA);
            if (arrows != 0xFA) {
                m_lda8(s, 0x7F);
                set_abs8(s, 0x0EFC, 0x7F);
                set_abs8(s, 0x0FFC, 0x7F);
                set_abs8(s, 0x0EFD, m_lda8(s, 0x29));
                set_abs8(s, 0x0FFD, m_lda8(s, 0xA9));
            } else {
                set_abs8(s, 0x0EFC, m_lda8(s, 0x7F));
                set_abs8(s, 0x0FFC, m_lda8(s, 0xFF));
                m_lda8(s, 0x29);
                set_abs8(s, 0x0EFD, 0x29);
                set_abs8(s, 0x0FFD, 0x29);
            }
        }
        inc_dp8(s, 0x80);
    }

    /* $C1:1905 */
    uint8_t item = m_lda8(s, abs8(s, 0x95E5));
    uint8_t item_shown = abs8(s, 0x991F);
    m_cmp8(s, item, item_shown);
    bool move = item != item_shown;
    if (!move) {
        arrows = m_lda8(s, abs8(s, 0x95E6));
        shown = abs8(s, 0x9920);
        m_cmp8(s, arrows, shown);
        if (arrows != shown) {
            set_abs8(s, 0x9920, arrows);
            move = true;
        }
    }
    if (move) {
        /* $C1:1918 */
        if (m_lda8(s, abs8(s, 0x991F)) & 0x80)
            m_tdc(s);
        asl_a(s);
        m_tax(s);
        m_rep(s, CPU_FLAG_M);
        m_lda16(s, m_read16(s, 0xCCFAE9 + c->x));
        m_tax(s);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        m_lda8(s, 0xFF);
        static const uint16_t cells[4] = { 0x0E80, 0x0E82, 0x0EC0, 0x0EC2 };
        for (int i = 0; i < 4; i++)
            set_absx8(s, cells[i], 0xFF);
        set_abs8(s, 0x991F, m_lda8(s, abs8(s, 0x95E5)));
        asl_a(s);
        m_tax(s);
        m_rep(s, CPU_FLAG_M);
        m_lda16(s, m_read16(s, 0xCCFAE9 + c->x));
        m_tax(s);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        for (int i = 0; i < 4; i++)
            set_absx8(s, cells[i], m_lda8(s, (uint8_t)(0x60 + i)));
        m_lda8(s, 0x29);
        for (int i = 0; i < 4; i++)
            set_absx8(s, (uint16_t)(cells[i] + 1), 0x29);
        inc_dp8(s, 0x80);
    }
    if (m_lda8(s, dp8(s, 0x80)) != 0)
        return call_long(s, 0xC11972, 0xCFFD6A);
    return true;
}

/* $C1:1979: while $9609 is set (presumably choosing a target), cursors on
 * the targets in $A62D-$A632. */
static bool place_target_cursors(Snes *s)
{
    Cpu *c = &s->cpu;
    if (m_lda8(s, abs8(s, 0x9613)) & 0x80)
        return true;
    m_lda8(s, abs8(s, 0x95DB));
    uint8_t page = (uint8_t)(c->a - 1);
    m_set_a8(s, page);
    m_set_nz8(s, page);
    if (page == 0)
        local_call(s, 0x1987, clear_cursor_column);
    hide_cursors(s);
    m_lda8(s, 0x32);
    for (uint16_t i = 0; i < 4; i++)
        set_abs8(s, (uint16_t)(0x0703 + 4 * i), 0x32);
    for (uint16_t i = 0; i < 4; i++)
        set_abs8(s, (uint16_t)(0x0702 + 4 * i), 0x00);

    if (!(m_lda8(s, abs8(s, 0xA62E)) & 0x80)) {
        /* $C1:1A7B: several targets */
        uint8_t first = m_lda8(s, abs8(s, 0xA62D));
        if (!(first & 0x80)) {
            m_tax(s);
            place_at_object(s, m_abs(s, 0x0700));
            and_0900_long(s, 0xCCFB8D);
        }
        m_ldy(s, 0x0001);
        set_dp16(s, 0x82, c->y);
        m_lda8(s, abs8(s, 0x960B));
        tay(s);
        set_dp16(s, 0x80, c->y);
        uint8_t n;
        for (;;) {
            m_set_index(s, &c->y, dp16(s, 0x80));
            if (!(m_lda8(s, m_read8(s, m_abs(s, 0xA62D) + c->y)) & 0x80)) {
                m_tax(s);
                m_lda8(s, dp8(s, 0x82));
                asl_a(s);
                asl_a(s);
                tay(s);
                place_at_object(s, m_abs(s, 0x0700) + c->y);
                m_ldx16(s, dp16(s, 0x82));
                and_0900_long(s, 0xCCFB8D + c->x);
            }
            inc_dp8(s, 0x80);
            n = m_lda8(s, dp8(s, 0x80));
            m_cmp8(s, n, 0x03);
            if (n == 0x03)
                break;
            m_cmp8(s, n, 0x06);
            if (n == 0x06)
                break;
            inc_dp8(s, 0x82);
            uint8_t k = m_lda8(s, dp8(s, 0x82));
            m_cmp8(s, k, 0x04);
            if (k == 0x04)
                break;
        }
        n = m_lda8(s, dp8(s, 0x80));
        m_cmp8(s, n, 0x06);
        if (n == 0x06)
            m_tdc(s);
        set_abs8(s, 0x960B, (uint8_t)c->a);
        return true;
    }

    /* $C1:19B4: one target */
    uint8_t target = m_lda8(s, abs8(s, 0xA62D));
    if (target & 0x80)
        return true;
    set_abs8(s, 0xA64E, target);
    m_tax(s);
    place_at_object(s, m_abs(s, 0x0700));
    if (m_lda8(s, abs8(s, 0x95DB)) == 0) {
        target = m_lda8(s, abs8(s, 0xA64E));
        m_cmp8(s, target, 0x03);
        if (target >= 0x03) {
            asl_a(s);
            m_tax(s);
            set_abs8(s, 0x0200, m_lda8(s, absx8(s, 0x984D)));
            set_dp8(s, 0x80, m_lda8(s, m_read8(s, 0xCCF8ED + c->x)));
            set_dp8(s, 0x81, m_lda8(s, m_read8(s, 0xCCF8EE + c->x)));
            bool stats = false;
            if (m_lda8(s, abs8(s, 0x9F34)) != 0) {
                m_lda8(s, abs8(s, 0xA64E));
                tay(s);
                stats = m_lda8(s, m_read8(s, m_abs(s, 0x9F29) + c->y)) != 0;
            }
            if (stats) {
                /* $C1:1A1C */
                m_ldx16(s, dp16(s, 0x80));
                for (uint16_t k = 0; k < 4; k++)
                    set_abs8(s, (uint16_t)(0x0201 + k), m_lda8(s, absx8(s, (uint16_t)(0x5E30 + k))));
            }
            m_lda8(s, abs8(s, 0x0200));
            m_tax(s);
            if (m_lda8(s, m_read8(s, 0xE1DE80 + c->x)) != 0)
                m_lda8(s, 0xFF);
            else
                m_lda8(s, abs8(s, 0xA64E));
            if (!call_long(s, stats ? 0xC11A47 : 0xC11A16, stats ? 0xCD002D : 0xCD0030))
                return false;
        }
    }

    /* $C1:1A4B */
    and_0900(s, 0xFE);
    if (m_lda8(s, abs8(s, 0xA4EE)) != 0) {
        set_abs8(s, 0xA4EE, 0x00);
        if (m_lda8(s, abs8(s, 0x960A)) != 0) {
            if (m_lda8(s, dp8(s, 0xEC)) == 0) {
                uint8_t top = m_lda8(s, abs8(s, 0x0701));
                m_cmp8(s, top, 0x2D);
                if (top < 0x2D)
                    inc_dp8(s, 0xEC);
            } else {
                uint8_t top = m_lda8(s, abs8(s, 0x0701));
                m_cmp8(s, top, 0x9C);
                if (top >= 0x9C)
                    set_dp8(s, 0xEC, 0x00);
            }
        }
    }
    return true;
}

// $C1:17DD
/* Place the menu's cursor sprites: with no menu open ($95D5 negative), hide
 * them; while $9609 is set, on the chosen targets ($C1:1979); on page 1 on a
 * combination tech's members (or through $C1:1115 for other techs); on pages
 * other than 0 and 1, the list cursor tiles. Clears $EE and $EF. */
void battle_place_menu_cursors(Snes *s)
{
    bool ok = true;
    if (m_lda8(s, abs8(s, 0x95D5)) & 0x80) {
        /* $C1:1AFE */
        hide_cursors(s);
        m_lda8(s, 0xF0);
        for (uint16_t i = 0; i < 4; i++)
            set_abs8(s, (uint16_t)(0x0701 + 4 * i), 0xF0);
    } else if (m_lda8(s, abs8(s, 0x9609)) != 0) {
        ok = place_target_cursors(s);
    } else {
        uint8_t page = m_lda8(s, abs8(s, 0x95DB));
        if (page != 0) {
            page = (uint8_t)(page - 1);
            m_set_a8(s, page);
            m_set_nz8(s, page);
            ok = page == 0 ? place_tech_cursors(s) : place_list_cursor(s);
        }
    }
    if (!ok)
        return;
    /* $C1:1B14 */
    set_dp8(s, 0xEE, 0x00);
    set_dp8(s, 0xEF, 0x00);
    m_rts(s);
}
