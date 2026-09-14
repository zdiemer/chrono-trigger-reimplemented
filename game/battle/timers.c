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

static uint8_t absy8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.y); }
static void set_absy8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.y, v); }
static uint16_t abs16(Snes *s, uint16_t addr) { return m_read16(s, m_abs(s, addr)); }

/* LSR on a 16-bit absolute word. */
static void lsr_abs16(Snes *s, uint16_t addr)
{
    uint32_t a = m_abs(s, addr);
    m_write16(s, a, m_lsr16(s, m_read16(s, a)));
}

/* INC A : STA abs,Y on an 8-bit counter; returns the new value. */
static uint8_t bump_absy8(Snes *s, uint16_t addr)
{
    uint8_t v = m_lda8(s, (uint8_t)(m_lda8(s, absy8(s, addr)) + 1));
    set_absy8(s, addr, v);
    return v;
}

// $C1:8290
/* The stretch of the battle main loop after battle_tick_timers ($C1:8290-
 * $C1:8344). Copies $AFAB-$AFAD to $99DD-$99DF. Then, when $0027 bit 0 is
 * set: for each of the 13 kinds ($00), look through its 11 timer entries
 * starting from the kind's cursor ($B179 + kind) for one whose flags (found
 * through the kind's table at $C1:B947 and the entry's index $B163) have bit 7
 * set; if one does, set the kind's bit in $B186 (bit 12 down to bit 0) and
 * move the cursor past it. And again when $0027 bit 0 is set: call the
 * handler ($C1:B92D table) for each kind whose bit is set. Continues at
 * $C1:8345. Entered with 8-bit A and 16-bit X. */
void battle_dispatch_expired_timers(Snes *s)
{
    Cpu *c = &s->cpu;
    for (uint16_t i = 0; i < 3; i++)
        m_write8(s, m_abs(s, (uint16_t)(0x99DD + i)), m_lda8(s, m_read8(s, m_abs(s, (uint16_t)(0xAFAB + i)))));
    m_lda8(s, m_read8(s, m_abs(s, 0x0027)));
    m_bit_imm8(s, 0x01);
    if (c->a & 0x01) {
        m_tdc(s);
        m_tax(s);
        m_write16(s, m_dp(s, 0x00), c->x);
        m_ldx16(s, 0x1000);
        m_write16(s, m_abs(s, 0xAEC3), c->x);
        uint8_t kind;
        do {
            /* $C1:82B3 */
            m_tdc(s);
            m_set_index(s, &c->y, m_read16(s, m_dp(s, 0x00)));
            m_lda8(s, absy8(s, 0xB179));
            m_tax(s);
            m_lda8(s, m_read8(s, m_abs(s, 0xB163) + c->x));
            m_rep(s, CPU_FLAG_M);
            m_write16(s, m_dp(s, 0x02), c->a);
            m_lda16(s, m_read16(s, m_dp(s, 0x00)));
            c->a = m_asl16(s, c->a);
            m_tax(s);
            m_lda16(s, m_read16(s, 0xC1B947 + c->x));
            m_set_flag(s, CPU_FLAG_C, false);
            c->a = m_adc16(s, c->a, m_read16(s, m_dp(s, 0x02)));
            m_tax(s);
            m_tdc(s);
            m_sep(s, CPU_FLAG_M);
            m_lda8(s, m_read8(s, m_abs(s, 0x0000) + c->x));
            m_bit_imm8(s, 0x80);
            if (c->a & 0x80) {
                m_rep(s, CPU_FLAG_M);
                m_lda16(s, abs16(s, 0xB186));
                m_write16(s, m_abs(s, 0xB186), m_lda16(s, (uint16_t)(c->a | abs16(s, 0xAEC3))));
                m_tdc(s);
                m_sep(s, CPU_FLAG_M);
                m_set_index(s, &c->y, m_read16(s, m_dp(s, 0x00)));
                bump_absy8(s, 0xB179);
            } else {
                m_set_index(s, &c->y, m_read16(s, m_dp(s, 0x00)));
                uint8_t cursor = bump_absy8(s, 0xB179);
                m_cmp8(s, cursor, 0x0B);
                if (cursor < 0x0B)
                    continue;   /* same kind, next entry */
                m_tdc(s);
                set_absy8(s, 0xB179, (uint8_t)c->a);
            }
            /* $C1:8301 */
            kind = (uint8_t)(m_read8(s, m_dp(s, 0x00)) + 1);
            m_write8(s, m_dp(s, 0x00), kind);
            m_set_nz8(s, kind);
            m_set_index(s, &c->y, m_read16(s, m_dp(s, 0x00)));
            m_rep(s, CPU_FLAG_M);
            lsr_abs16(s, 0xAEC3);
            m_tdc(s);
            m_sep(s, CPU_FLAG_M);
            kind = m_lda8(s, m_read8(s, m_dp(s, 0x00)));
            m_cmp8(s, kind, 0x0D);
        } while (m_read8(s, m_dp(s, 0x00)) < 0x0D);
    }

    /* $C1:8313 */
    m_lda8(s, m_read8(s, m_abs(s, 0x0027)));
    m_bit_imm8(s, 0x01);
    if (c->a & 0x01) {
        m_rep(s, CPU_FLAG_M);
        m_write16(s, m_abs(s, 0xAEC3), m_lda16(s, 0x1000));
        m_tdc(s);
        m_tax(s);
        do {
            uint16_t bit = m_lda16(s, abs16(s, 0xAEC3));
            uint16_t flags = abs16(s, 0xB186);
            m_set_flag(s, CPU_FLAG_Z, (bit & flags) == 0);
            m_set_flag(s, CPU_FLAG_N, flags & 0x8000);
            m_set_flag(s, CPU_FLAG_V, flags & 0x4000);
            if (bit & flags) {
                m_push16(s, c->x);
                m_txa(s);
                c->a = m_asl16(s, c->a);
                m_tax(s);
                m_tdc(s);
                m_sep(s, CPU_FLAG_M);
                m_jsr(s, 0xC18333, 0xC10000 | m_read16(s, 0xC1B92D + c->x));
                if (c->e || (c->p & CPU_FLAG_X))
                    return;   /* PLX below needs 16-bit X */
                m_rep(s, CPU_FLAG_M);
                c->x = m_pull16(s);
                m_set_nz16(s, c->x);
            }
            lsr_abs16(s, 0xAEC3);
            m_inx(s);
            m_cmp16(s, c->x, 0x000D);
        } while (c->x < 0x000D);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
    }
    c->pc = 0x8345;
}
