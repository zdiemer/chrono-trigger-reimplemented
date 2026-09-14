/* Battle: arithmetic helpers on the hardware multiplier. */
#include "battle/battle.h"

#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }

// $C1:0089
/* $AF (16-bit) = $AD * $AE, with the operands kept in $77/$78 (for the
 * interrupt handlers, which restore the multiplier from them). */
void battle_multiply_8x8(Snes *s)
{
    uint8_t a = m_lda8(s, dp8(s, 0xAD));
    set_dp8(s, 0x77, a);
    m_write8(s, 0x004202, a);
    uint8_t b = m_lda8(s, dp8(s, 0xAE));
    set_dp8(s, 0x78, b);
    m_write8(s, 0x004203, b);
    m_rep(s, CPU_FLAG_M);
    m_write16(s, m_dp(s, 0xAF), m_lda16(s, m_read16(s, 0x004216)));
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C1:00A7
/* $A9-$AB (24-bit) = $A5 * $A7-$A8 (16-bit), in two hardware multiplications,
 * with the last operands kept in $77/$78. */
void battle_multiply_8x16(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_tdc(s);
    m_push8(s, (uint8_t)c->a);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    uint8_t a = m_lda8(s, dp8(s, 0xA5));
    set_dp8(s, 0x77, a);
    m_write8(s, m_abs(s, 0x4202), a);
    uint8_t low = m_lda8(s, dp8(s, 0xA7));
    set_dp8(s, 0x78, low);
    m_write8(s, m_abs(s, 0x4203), low);
    m_push8(s, c->p);
    uint8_t high = m_lda8(s, dp8(s, 0xA8));
    set_dp8(s, 0xAB, 0x00);
    uint16_t product = m_ldx16(s, m_read16(s, m_abs(s, 0x4216)));
    set_dp8(s, 0x78, high);
    m_write8(s, m_abs(s, 0x4203), high);
    m_write16(s, m_dp(s, 0xA9), product);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
    uint16_t sum = m_lda16(s, m_read16(s, m_dp(s, 0xAA)));
    sum = m_adc16(s, sum, m_read16(s, m_abs(s, 0x4216)));
    m_write16(s, m_dp(s, 0xAA), sum);
    m_tdc(s);
    c->p = m_pull8(s);
    if (c->p & CPU_FLAG_X) {
        c->x &= 0xFF;
        c->y &= 0xFF;
    }
    m_sep(s, CPU_FLAG_M);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rts(s);
}
