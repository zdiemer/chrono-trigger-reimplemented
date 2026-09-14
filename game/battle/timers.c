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
                m_jsr(s, 0xC18333, 0xC10000 | m_jump_table(s, 0xC1, 0xB92D, c->x));
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

/* One of the two loops of $CF:EFAF: for slot $C6 from `last` down to 0, when
 * the slot is active (`active` + slot), count down its delay (`delay`) and,
 * when it runs out, reload it (`reload`), set $C8 = slot * 2 and $CA = slot * 4,
 * and call the handler for its type (`type`) from the table at `table`. */
static bool run_effect_slots(Snes *s, uint16_t last, uint16_t active, uint16_t delay, uint16_t reload,
                             uint16_t type, uint16_t table, uint32_t site)
{
    Cpu *c = &s->cpu;
    m_ldx16(s, last);
    m_write16(s, m_dp(s, 0xC6), c->x);
    uint8_t slot;
    do {
        m_ldx16(s, m_read16(s, m_dp(s, 0xC6)));
        if (m_lda8(s, absx8(s, active)) != 0) {
            uint8_t d = (uint8_t)(absx8(s, delay) - 1);
            set_absx8(s, delay, d);
            m_set_nz8(s, d);
            if (d == 0) {
                set_absx8(s, delay, m_lda8(s, absx8(s, reload)));
                m_rep(s, CPU_FLAG_M);
                m_lda16(s, m_read16(s, m_dp(s, 0xC6)));
                c->a = m_asl16(s, c->a);
                m_write16(s, m_dp(s, 0xC8), c->a);
                c->a = m_asl16(s, c->a);
                m_write16(s, m_dp(s, 0xCA), c->a);
                m_tdc(s);
                m_sep(s, CPU_FLAG_M);
                uint8_t t = m_asl8(s, m_lda8(s, absx8(s, type)));
                m_set_a8(s, t);
                m_tax(s);
                if (!call(s, site, 0xCF0000 | m_jump_table(s, 0xCF, table, c->x)))
                    return false;
            }
        }
        slot = (uint8_t)(m_read8(s, m_dp(s, 0xC6)) - 1);
        m_write8(s, m_dp(s, 0xC6), slot);
        m_set_nz8(s, slot);
    } while (!(slot & 0x80));
    return true;
}

// $CF:EFAF
/* Step the battle's timed effects: with $A013 set, the 11 object effect slots
 * ($A337 active, $A2EB delay, $A2FE reload, $A86D type, handlers at $CF:F01E)
 * and then 8 more ($A342, $A2F6, $A309, $A878, $CF:F02E). With $A013 clear,
 * returns if $A0FD is set and otherwise continues at $CF:F978. */
void battle_step_effect_slots(Snes *s)
{
    Cpu *c = &s->cpu;
    if (m_lda8(s, m_read8(s, m_abs(s, 0xA013))) == 0) {
        if (m_lda8(s, m_read8(s, m_abs(s, 0xA0FD))) != 0)
            m_rts(s);
        else
            c->pc = 0xF978;
        return;
    }
    if (!run_effect_slots(s, 0x000A, 0xA337, 0xA2EB, 0xA2FE, 0xA86D, 0xF01E, 0xCFEFE6))
        return;
    if (!run_effect_slots(s, 0x0007, 0xA342, 0xA2F6, 0xA309, 0xA878, 0xF02E, 0xCFF016))
        return;
    m_rts(s);
}
