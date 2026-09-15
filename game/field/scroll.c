/* Field: automatic layer scrolling. */
#include "field/field.h"

#include "port/machine.h"

static uint8_t abs8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr)); }
static void set_abs8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr), v); }

/* One axis: move the 16-bit scroll position `pos` by the whole pixels in the
 * signed 4.4 accumulator `acc` (mirroring it to `mirror` in $7E), keeping the
 * fraction, sign-extended. With `skip_zero`, a zero accumulator does nothing. */
static void scroll_axis(Snes *s, uint16_t acc, uint16_t pos, uint32_t mirror, bool skip_zero)
{
    Cpu *c = &s->cpu;
    uint8_t v = m_lda8(s, abs8(s, acc));
    if (skip_zero && v == 0)
        return;
    if (!(v & 0x80)) {
        for (int i = 0; i < 4; i++)
            v = m_lsr8(s, v);
        m_set_a8(s, v);
        m_rep(s, CPU_FLAG_M);
        m_lda16(s, c->a & 0x00FF);
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, m_read16(s, m_abs(s, pos)));
    } else {
        v = m_lda8(s, (uint8_t)((v ^ 0xFF) + 1));
        for (int i = 0; i < 4; i++)
            v = m_lsr8(s, v);
        m_set_a8(s, v);
        m_write8(s, m_dp(s, 0xD9), v);
        m_write8(s, m_dp(s, 0xDA), 0x00);
        m_rep(s, CPU_FLAG_M);
        m_lda16(s, m_read16(s, m_abs(s, pos)));
        m_set_flag(s, CPU_FLAG_C, true);
        c->a = m_sbc16(s, c->a, m_read16(s, m_dp(s, 0xD9)));
    }
    m_write16(s, m_abs(s, pos), c->a);
    m_write16(s, mirror, c->a);
    m_sep(s, CPU_FLAG_M);
    uint8_t frac = m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, acc)) & 0x0F));
    if (frac != 0) {
        uint8_t a = m_lda8(s, abs8(s, acc));
        frac = (a & 0x80) ? m_lda8(s, (uint8_t)(a | 0xF0)) : m_lda8(s, (uint8_t)(a & 0x0F));
    }
    set_abs8(s, acc, frac);
}

/* Add a layer group's speeds to its accumulators (8-bit, CLC each). */
static void accumulate(Snes *s, uint16_t speed, uint16_t acc)
{
    for (uint16_t i = 0; i < 2; i++) {
        m_set_flag(s, CPU_FLAG_C, false);
        uint8_t v = m_adc8(s, m_lda8(s, abs8(s, (uint16_t)(speed + i))), abs8(s, (uint16_t)(acc + i)));
        m_set_a8(s, v);
        set_abs8(s, (uint16_t)(acc + i), v);
    }
}

// $C0:2C41
/* Scroll the field layers that scroll on their own ($7F:3415 bit 1: BG1 at
 * speed $3417/$3418, bit 2: BG2 at $3419/$341A): the 4.4 accumulators at
 * $341D-$3420 move the positions at $346B-$3471, which are copied to the
 * scroll values at $7E:2C74-$2C7A. Uses direct page $0100 and data bank $7F,
 * restoring both; returns with RTL. */
void field_scroll_layers(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push16(s, c->d);
    m_push8(s, c->db);
    m_rep(s, CPU_FLAG_M);
    c->d = m_lda16(s, 0x0100);
    m_sep(s, CPU_FLAG_M);
    m_push8(s, m_lda8(s, 0x7F));
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_lda8(s, abs8(s, 0x3415));
    m_bit_imm8(s, 0x02);
    if (c->a & 0x02) {
        accumulate(s, 0x3417, 0x341D);
        scroll_axis(s, 0x341D, 0x346B, 0x7E2C74, true);
        scroll_axis(s, 0x341E, 0x346D, 0x7E2C76, false);
    }
    m_lda8(s, abs8(s, 0x3415));
    m_bit_imm8(s, 0x04);
    if (c->a & 0x04) {
        accumulate(s, 0x3419, 0x341F);
        scroll_axis(s, 0x341F, 0x346F, 0x7E2C78, false);
        scroll_axis(s, 0x3420, 0x3471, 0x7E2C7A, false);
    }
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    m_rtl(s);
}
