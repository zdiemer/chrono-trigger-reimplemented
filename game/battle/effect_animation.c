/* Battle: stepping the effects' animations (being rewritten). */
#include "battle/entries.h"

#include "port/machine.h"

static uint8_t absx8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.x); }
static void set_absx8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.x, v); }

/* JSR out; false if the callee didn't come back with 8-bit A and 16-bit X. */
static bool call(Snes *s, uint32_t site, uint32_t target)
{
    m_jsr(s, site, target);
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M;
}

// $CC:F278
/* Count down the delays ($A1B0) of the 8 active ($A1A8) slots, then run
 * $CC:F2A9 for each active slot whose delay is 0 ($80 = the slot). */
void battle_step_slot_delays(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    do {
        if (m_lda8(s, absx8(s, 0xA1A8)) != 0) {
            uint8_t delay = m_lda8(s, absx8(s, 0xA1B0));
            if (delay != 0) {
                delay = (uint8_t)(delay - 1);
                set_absx8(s, 0xA1B0, delay);
                m_set_nz8(s, delay);
            }
        }
        m_inx(s);
        m_cmp16(s, c->x, 0x0008);
    } while (c->x != 0x0008);
    m_tdc(s);
    m_tax(s);
    m_write16(s, m_dp(s, 0x80), c->x);
    uint8_t slot;
    do {
        m_ldx16(s, m_read16(s, m_dp(s, 0x80)));
        if (m_lda8(s, absx8(s, 0xA1A8)) != 0 && m_lda8(s, absx8(s, 0xA1B0)) == 0 && !call(s, 0xCCF29D, 0xCCF2A9))
            return;
        slot = (uint8_t)(m_read8(s, m_dp(s, 0x80)) + 1);
        m_write8(s, m_dp(s, 0x80), slot);
        m_set_nz8(s, slot);
        slot = m_lda8(s, m_read8(s, m_dp(s, 0x80)));
        m_cmp8(s, slot, 0x08);
    } while (slot != 0x08);
    m_rtl(s);
}

