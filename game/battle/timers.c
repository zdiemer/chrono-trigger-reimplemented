/* Battle: timers ticked from the battle main loop. */
#include "battle/battle.h"

#include "port/machine.h"

static uint8_t absx8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.x); }
static void set_absx8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.x, v); }

// $C1:8231
/* A stretch of the battle main loop ($C1:8231-$C1:828F): walk the 143
 * timer entries ($AFB6 flags, $AF27 counters; $B3C4 the entry) alongside a
 * slot index cycling 0-10 ($B3C0). For each entry whose flags are positive:
 * if the slot's own flags ($AFB6 + slot) are set, count down the slot's
 * counter, otherwise count down the entry's counter unless it's already 0;
 * when the counted-down one reaches 0, set bit 7 of its flags. Continues at
 * $C1:8290. Entered with 16-bit X. */
void battle_tick_timers(Snes *s)
{
    Cpu *c = &s->cpu;
    m_sep(s, CPU_FLAG_M);
    m_tdc(s);
    m_tax(s);
    m_set_index(s, &c->y, c->a);
    m_write16(s, m_abs(s, 0xB3C0), c->y);
    m_write16(s, m_abs(s, 0xB3C4), c->y);
    do {
        m_ldx16(s, m_read16(s, m_abs(s, 0xB3C4)));
        uint8_t flags = m_lda8(s, absx8(s, 0xAFB6));
        if (flags != 0 && !(flags & 0x80)) {
            m_tdc(s);
            m_lda8(s, m_read8(s, m_abs(s, 0xB3C0)));
            m_tax(s);
            bool expired;
            if (m_lda8(s, absx8(s, 0xAFB6)) != 0) {
                uint8_t n = m_lda8(s, (uint8_t)(absx8(s, 0xAF27) - 1));
                set_absx8(s, 0xAF27, n);
                m_cmp8(s, n, 0x00);
                expired = n == 0;
            } else {
                m_ldx16(s, m_read16(s, m_abs(s, 0xB3C4)));
                uint8_t n = m_lda8(s, absx8(s, 0xAF27));
                m_cmp8(s, n, 0x00);
                expired = n == 0;
                if (!expired) {
                    n = m_lda8(s, (uint8_t)(n - 1));
                    set_absx8(s, 0xAF27, n);
                    m_cmp8(s, n, 0x00);
                    expired = n == 0;
                }
            }
            if (expired) {
                uint8_t f = m_lda8(s, absx8(s, 0xAFB6));
                set_absx8(s, 0xAFB6, m_lda8(s, f | 0x80));
            }
        }
        uint32_t slot_addr = m_abs(s, 0xB3C0);
        uint8_t slot = (uint8_t)(m_read8(s, slot_addr) + 1);
        m_write8(s, slot_addr, slot);
        m_set_nz8(s, slot);
        slot = m_lda8(s, m_read8(s, slot_addr));
        m_cmp8(s, slot, 0x0B);
        if (slot >= 0x0B)
            m_write8(s, slot_addr, 0x00);
        m_ldx16(s, m_read16(s, m_abs(s, 0xB3C4)));
        m_ldx16(s, (uint16_t)(c->x + 1));
        m_write16(s, m_abs(s, 0xB3C4), c->x);
        m_cmp16(s, c->x, 0x008F);
    } while (c->x < 0x008F);
    c->pc = 0x8290;
}
