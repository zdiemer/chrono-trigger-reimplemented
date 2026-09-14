/* Battle: per-frame display tables. */
#include "battle/battle.h"

#include "port/machine.h"

/* LDA abs : CLC : ADC abs (16-bit) */
static uint16_t add_abs16(Snes *s, uint16_t a, uint16_t b)
{
    m_lda16(s, m_read16(s, m_abs(s, a)));
    m_set_flag(s, CPU_FLAG_C, false);
    return s->cpu.a = m_adc16(s, s->cpu.a, m_read16(s, m_abs(s, b)));
}

static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }

/* SEP #$10 : REP #$20, then the BG1 and BG2 offsets both entries share:
 * $45/$47 = $2C70/$2C72 + $CCA4/$CCA6 + $BB01/$BB03, $49/$4B = $2C74/$2C76 +
 * $CCA8/$CCAA. */
static void scroll_bg12(Snes *s)
{
    m_sep(s, CPU_FLAG_X);
    m_rep(s, CPU_FLAG_M);
    add_abs16(s, 0x2C70, 0xCCA4);
    m_set_flag(s, CPU_FLAG_C, false);
    set_dp16(s, 0x45, s->cpu.a = m_adc16(s, s->cpu.a, m_read16(s, m_abs(s, 0xBB01))));
    add_abs16(s, 0x2C72, 0xCCA6);
    m_set_flag(s, CPU_FLAG_C, false);
    set_dp16(s, 0x47, s->cpu.a = m_adc16(s, s->cpu.a, m_read16(s, m_abs(s, 0xBB03))));
    set_dp16(s, 0x49, add_abs16(s, 0x2C74, 0xCCA8));
    set_dp16(s, 0x4B, add_abs16(s, 0x2C76, 0xCCAA));
}

/* $CD:0706: write the offsets in $45-$4F to all 32 entries of the three HDMA
 * scroll tables ($BC89, $BD09, $BD89 for BG1-BG3: in each 16-byte block, four
 * horizontal/vertical word pairs), then return with 8-bit A and 16-bit X. */
static void fill_scroll_tables(Snes *s)
{
    Cpu *c = &s->cpu;
    static const uint16_t tables[3] = { 0xBC89, 0xBD09, 0xBD89 };
    m_set_index(s, &c->x, 0x00);
    do {
        for (int t = 0; t < 3; t++) {
            uint16_t h = m_lda16(s, dp16(s, (uint8_t)(0x45 + 4 * t)));
            for (uint16_t k = 0; k < 16; k += 4)
                m_write16(s, m_abs(s, (uint16_t)(tables[t] + k)) + c->x, h);
            uint16_t v = m_lda16(s, dp16(s, (uint8_t)(0x47 + 4 * t)));
            for (uint16_t k = 2; k < 16; k += 4)
                m_write16(s, m_abs(s, (uint16_t)(tables[t] + k)) + c->x, v);
        }
        m_txa(s);
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, 0x0010);
        m_tax(s);
        m_cmp8(s, (uint8_t)c->x, 0x80);
    } while (c->x != 0x80);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_rep(s, CPU_FLAG_X);
    m_rts(s);
}

// $CD:06C4
/* Fill the battle's BG1-BG3 scroll HDMA tables from the base scroll ($2C70),
 * the shake/offset values ($CCA4) and, for BG1 and BG2, $BB01. */
void battle_update_scroll_tables(Snes *s)
{
    scroll_bg12(s);
    set_dp16(s, 0x4D, add_abs16(s, 0x2C78, 0xCCAC));
    set_dp16(s, 0x4F, add_abs16(s, 0x2C7A, 0xCCAE));
    fill_scroll_tables(s);
}

// $CD:076C
/* The same, with BG3 scrolled by $CA5A + $CA5E and $CA5C + $CA60 instead of
 * its base scroll. */
void battle_update_scroll_tables_bg3_moving(Snes *s)
{
    scroll_bg12(s);
    add_abs16(s, 0xCA5A, 0xCA5E);
    m_set_flag(s, CPU_FLAG_C, false);
    set_dp16(s, 0x4D, s->cpu.a = m_adc16(s, s->cpu.a, m_read16(s, m_abs(s, 0xCCAC))));
    add_abs16(s, 0xCA5C, 0xCA60);
    m_set_flag(s, CPU_FLAG_C, false);
    set_dp16(s, 0x4F, s->cpu.a = m_adc16(s, s->cpu.a, m_read16(s, m_abs(s, 0xCCAE))));
    fill_scroll_tables(s);
}
