/* Field: a quadrilateral's outline rasterized into a per-line table of left and
 * right X positions (the field copies the corners from $7F:1CEA-$7F:1CF8 and
 * points the table at $7F:17C8 or $7F:1AC8), presumably for a window HDMA
 * shape. The code lives in bank C3. */
#include "field/field.h"

#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }

/* The 24-bit pointer in dp $50-$52 plus Y, as [dp],Y addresses it. */
static uint32_t table_y(Snes *s)
{
    uint32_t base = (uint32_t)dp8(s, 0x52) << 16 | dp16(s, 0x50);
    return base + s->cpu.y;
}

/* PEA #$0300 : PLD */
static void set_dp_0300(Snes *s)
{
    m_push16(s, 0x0300);
    s->cpu.d = m_pull16(s);
    m_set_nz16(s, s->cpu.d);
}

/* TRB on a direct-page word with 16-bit A. */
static void trb_dp16(Snes *s, uint8_t cell)
{
    uint16_t v = dp16(s, cell);
    m_set_flag(s, CPU_FLAG_Z, (v & s->cpu.a) == 0);
    set_dp16(s, cell, (uint16_t)(v & ~s->cpu.a));
}

/* One edge's X positions for each line (the edge-stepping loops of $C3:1025),
 * written one byte per line from Y upward by 2; `step` is +1 or -1. */
static void step_edge(Snes *s, int step)
{
    Cpu *c = &s->cpu;
    do {
        if (!(m_lda16(s, dp16(s, 0xFC)) & 0x8000)) {
            do {
                m_ldx16(s, (uint16_t)(c->x + step));
                m_lda16(s, m_sbc16(s, c->a, dp16(s, 0xFA)));
            } while (!(c->a & 0x8000));
        }
        m_lda16(s, m_adc16(s, c->a, dp16(s, 0xF8)));
        set_dp16(s, 0xFC, c->a);
        m_txa(s);
        m_sep(s, CPU_FLAG_M);
        m_write8(s, m_abs(s, 0x0000) + c->y, (uint8_t)c->a);
        m_iny(s);
        m_iny(s);
        uint8_t left = (uint8_t)(dp8(s, 0xF2) - 1);
        set_dp8(s, 0xF2, left);
        m_set_nz8(s, left);
        m_rep(s, CPU_FLAG_M);
    } while (!m_flag(s, CPU_FLAG_Z));
}

/* $C3:1025, entered with its JSL's return address pushed and 16-bit A: one
 * edge from (dp $53, $55) to (dp $57, $59). */
static void draw_edge(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push16(s, c->d);
    m_push8(s, c->db);
    m_push8(s, c->p);
    m_sep(s, CPU_FLAG_M);
    set_dp_0300(s);
    m_lda8(s, dp8(s, 0x52));
    m_push8(s, (uint8_t)c->a);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    set_dp8(s, 0x54, 0x00);
    set_dp8(s, 0x58, 0x00);
    set_dp8(s, 0x56, 0x00);
    set_dp8(s, 0x5A, 0x00);
    set_dp8(s, 0xFB, 0x00);
    m_lda8(s, dp8(s, 0x59));
    m_set_flag(s, CPU_FLAG_C, true);
    m_set_a8(s, m_sbc8(s, (uint8_t)c->a, dp8(s, 0x55)));

    if (m_flag(s, CPU_FLAG_Z)) {
        /* $C3:1001: a horizontal edge sets both ends of its line. */
        m_rep(s, CPU_FLAG_M);
        m_lda16(s, dp16(s, 0x55));
        m_lda16(s, c->a & 0x00FF);
        c->a = m_asl16(s, c->a);
        m_set_index(s, &c->y, c->a);
        m_sep(s, CPU_FLAG_M);
        m_lda8(s, dp8(s, 0x53));
        m_cmp8(s, (uint8_t)c->a, dp8(s, 0x57));
        if (!m_flag(s, CPU_FLAG_N)) {
            m_lda8(s, dp8(s, 0x57));
            m_write8(s, table_y(s), (uint8_t)c->a);
            m_lda8(s, dp8(s, 0x53));
        } else {
            m_write8(s, table_y(s), (uint8_t)c->a);
            m_lda8(s, dp8(s, 0x57));
        }
        m_iny(s);
        m_iny(s);
        m_write8(s, table_y(s), (uint8_t)c->a);
    } else {
        if (!m_flag(s, CPU_FLAG_C)) {
            /* Going up: swap the ends. */
            uint16_t y = dp16(s, 0x57), x = dp16(s, 0x53);
            m_ldy(s, y);
            m_ldx(s, x);
            set_dp16(s, 0x53, y);
            set_dp16(s, 0x57, x);
            y = m_ldy(s, dp16(s, 0x59));
            x = m_ldx(s, dp16(s, 0x55));
            set_dp16(s, 0x55, y);
            set_dp16(s, 0x59, x);
            m_lda8(s, (uint8_t)c->x);
            m_set_flag(s, CPU_FLAG_C, true);
            m_set_a8(s, m_sbc8(s, (uint8_t)c->a, dp8(s, 0x55)));
        }
        set_dp8(s, 0xFA, (uint8_t)c->a);
        set_dp8(s, 0xF2, (uint8_t)c->a);
        m_rep(s, CPU_FLAG_M);
        m_lda16(s, dp16(s, 0x55));
        c->a = m_asl16(s, c->a);
        m_set_flag(s, CPU_FLAG_C, false);
        m_lda16(s, m_adc16(s, c->a, dp16(s, 0x50)));
        m_set_index(s, &c->y, c->a);
        m_ldx(s, dp16(s, 0x53));
        m_lda16(s, dp16(s, 0x57));
        m_set_flag(s, CPU_FLAG_C, true);
        m_lda16(s, m_sbc16(s, c->a, dp16(s, 0x53)));
        if (!(c->a & 0x8000)) {
            set_dp16(s, 0xF8, c->a);
            m_set_flag(s, CPU_FLAG_C, true);
            m_lda16(s, m_sbc16(s, c->a, dp16(s, 0xFA)));
            set_dp16(s, 0xFC, c->a);
            step_edge(s, +1);
        } else {
            m_lda16(s, (uint16_t)((c->a ^ 0xFFFF) + 1));
            set_dp16(s, 0xF8, c->a);
            m_lda16(s, dp16(s, 0xFA));
            m_lda16(s, m_sbc16(s, c->a, dp16(s, 0xF8)));
            set_dp16(s, 0xFC, c->a);
            step_edge(s, -1);
        }
    }

    /* $C3:10BA: TDC : XBA : PLP : PLB : PLD : RTL */
    m_tdc(s);
    m_xba(s);
    c->p = m_pull8(s);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
}

// $C3:0011
/* Rasterizes the quadrilateral with corners (X, Y) at dp $0360/$0361, $0362/
 * $0363, $0364/$0365 and $0366/$0367 into the table at the 24-bit pointer in
 * $0350: two bytes per line, left X then right X. Starting from the topmost
 * corner (its line in $0368, its entry point in $03F0) it draws the four edges
 * in order through $C3:1025; the first edge going up moves the table pointer
 * to the right-hand bytes and records its line in $036A. Then it fills the
 * lines below $036A with 1, 0 and those above $0368 with the first entry
 * (MVN in bank $7E or $7F, by the table's bank). Entered with 16-bit X
 * through the JMP at $C3:0011; returns with RTL. */
void field_rasterize_quad(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push16(s, c->d);
    m_push8(s, c->db);
    m_push8(s, c->p);
    m_sep(s, CPU_FLAG_M);
    set_dp_0300(s);
    set_dp8(s, 0x69, 0x00);
    m_ldy(s, 0x0F35);
    set_dp16(s, 0xF6, c->y);
    int first = 0;
    m_lda8(s, dp8(s, 0x61));
    for (int corner = 1; corner < 4; corner++) {
        m_cmp8(s, (uint8_t)c->a, dp8(s, (uint8_t)(0x61 + 2 * corner)));
        if (m_flag(s, CPU_FLAG_C)) {
            first = corner;
            m_ldy(s, (uint16_t)(0x0F35 + 0x13 * corner));
            m_lda8(s, dp8(s, (uint8_t)(0x61 + 2 * corner)));
        }
    }
    set_dp8(s, 0x68, (uint8_t)c->a);
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, 0x0004);
    set_dp16(s, 0xF4, c->a);
    set_dp16(s, 0xF0, c->y);
    /* JMP ($03F0) to the corner's entry in the edge sequence at $C3:0F35. */

    for (int edge = first;; edge = (edge + 1) & 3) {
        uint8_t from = (uint8_t)(0x60 + 2 * edge), to = (uint8_t)(0x60 + 2 * ((edge + 1) & 3));
        set_dp16(s, 0x53, m_lda16(s, dp16(s, from)));
        set_dp16(s, 0x55, m_lda16(s, dp16(s, (uint8_t)(from + 1))));
        set_dp16(s, 0x57, m_lda16(s, dp16(s, to)));
        set_dp16(s, 0x59, m_lda16(s, dp16(s, (uint8_t)(to + 1))));
        m_push16(s, (uint16_t)(0x0F47 + 0x13 * edge));   /* JSR $0F83 */

        /* $C3:0F83 */
        m_lda16(s, 0xFF00);
        trb_dp16(s, 0x55);
        trb_dp16(s, 0x59);
        m_lda16(s, dp16(s, 0x59));
        m_cmp16(s, c->a, dp16(s, 0x55));
        if (m_flag(s, CPU_FLAG_N) && m_lda16(s, dp16(s, 0xF6)) != 0) {
            uint16_t ptr = (uint16_t)(dp16(s, 0x50) + 1);
            set_dp16(s, 0x50, ptr);
            m_set_nz16(s, ptr);
            set_dp16(s, 0x6A, m_lda16(s, dp16(s, 0x55)));
            set_dp16(s, 0xF6, 0x0000);
        }
        m_push8(s, 0xC3);                                /* JSL $C31025 */
        m_push16(s, 0x0F9F);
        draw_edge(s);
        m_pull16(s);                                     /* RTL */
        m_pull8(s);
        uint16_t left = (uint16_t)(dp16(s, 0xF4) - 1);
        set_dp16(s, 0xF4, left);
        m_set_nz16(s, left);
        if (left != 0) {
            m_pull16(s);                                 /* RTS */
            continue;
        }
        break;
    }

    /* $C3:0FA5 */
    m_lda16(s, m_pull16(s));
    m_lda16(s, dp16(s, 0x52));
    c->a = m_lsr16(s, c->a);
    m_push8(s, c->p);
    uint16_t ptr = (uint16_t)(dp16(s, 0x50) - 1);
    set_dp16(s, 0x50, ptr);
    m_set_nz16(s, ptr);
    m_lda16(s, 0x0100);
    m_set_flag(s, CPU_FLAG_C, false);
    m_lda16(s, m_sbc16(s, c->a, dp16(s, 0x6A)));
    c->a = m_asl16(s, c->a);
    m_lda16(s, (uint16_t)(c->a - 1));
    set_dp16(s, 0xF0, c->a);
    m_lda16(s, dp16(s, 0x6A));
    c->a = m_asl16(s, c->a);
    m_set_flag(s, CPU_FLAG_C, false);
    m_lda16(s, m_adc16(s, c->a, dp16(s, 0x50)));
    m_set_index(s, &c->y, c->a);
    m_tax(s);
    m_lda16(s, 0x0001);
    uint32_t base = (uint32_t)dp8(s, 0x52) << 16 | dp16(s, 0x50);
    m_write16(s, base, c->a);
    c->p = m_pull8(s);
    uint8_t bank = m_flag(s, CPU_FLAG_C) ? 0x7F : 0x7E;
    m_write16(s, ((uint32_t)bank << 16) + c->x, c->a);
    m_iny(s);
    m_iny(s);
    if (m_lda16(s, dp16(s, 0xF0)) != 0) {
        m_mvn(s, bank, bank);
        m_ldx(s, dp16(s, 0x50));
        m_txy(s);
        m_iny(s);
        m_iny(s);
        if (m_lda16(s, dp16(s, 0x68)) != 0) {
            c->a = m_asl16(s, c->a);
            m_lda16(s, (uint16_t)(c->a - 1));
            m_mvn(s, bank, bank);
        }
    }

    /* $C3:10BA */
    m_tdc(s);
    m_xba(s);
    c->p = m_pull8(s);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    m_rtl(s);
}
