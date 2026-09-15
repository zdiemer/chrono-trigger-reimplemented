/* Scenes: arithmetic helpers. */
#include "scene/scene.h"

#include "port/machine.h"

// $C3:094C
/* Signed multiply through the hardware multiplier: the 16-bit dp $F0 times
 * the 8-bit dp $F2, both taken as magnitudes and the product negated when the
 * signs of $F1 and $F2 differ, into the 32-bit dp $F4-$F7. The middle byte
 * adds the second partial product to dp $F5-$F6 as the original does, reading
 * $F6 as it was. Restores P. Entered with 16-bit X. */
void scene_multiply_signed(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, m_read8(s, m_dp(s, 0xF1)));
    m_lda8(s, (uint8_t)(c->a ^ m_read8(s, m_dp(s, 0xF2))));
    m_push8(s, c->p);
    uint8_t f2 = m_lda8(s, m_read8(s, m_dp(s, 0xF2)));
    if (f2 & 0x80)
        m_lda8(s, (uint8_t)((f2 ^ 0xFF) + 1));
    m_write8(s, m_abs(s, 0x4202), (uint8_t)c->a);
    m_rep(s, CPU_FLAG_M);
    uint16_t f0 = m_lda16(s, m_read16(s, m_dp(s, 0xF0)));
    if (f0 & 0x8000)
        m_lda16(s, (uint16_t)((f0 ^ 0xFFFF) + 1));
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_abs(s, 0x4203), (uint8_t)c->a);
    m_write8(s, m_dp(s, 0xF7), 0x00);
    m_xba(s);
    m_ldx16(s, m_read16(s, m_abs(s, 0x4216)));
    m_write8(s, m_abs(s, 0x4203), (uint8_t)c->a);
    m_write16(s, m_dp(s, 0xF4), c->x);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
    m_lda16(s, m_read16(s, m_abs(s, 0x4216)));
    m_lda16(s, m_adc16(s, c->a, m_read16(s, m_dp(s, 0xF5))));
    m_write16(s, m_dp(s, 0xF5), c->a);
    uint8_t sign = m_pull8(s);
    if (sign & CPU_FLAG_N) {
        m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
        m_lda16(s, m_read16(s, m_dp(s, 0xF4)));
        m_lda16(s, m_adc16(s, c->a ^ 0xFFFF, 0x0001));
        m_write16(s, m_dp(s, 0xF4), c->a);
        m_lda16(s, m_read16(s, m_dp(s, 0xF6)));
        m_lda16(s, m_adc16(s, c->a ^ 0xFFFF, 0x0000));
        m_write16(s, m_dp(s, 0xF6), c->a);
    }
    c->p = m_pull8(s);
    m_rtl(s);
}
