/* Battle: per-combatant status flags derived from the stat blocks.
 *
 * Each combatant (slot Y: 0-2 the party, 3-10 enemies) has a stat block at
 * $5E00 + X (X = slot * $80) with status bytes at $5E4A-$5E4E and $5E52-$5E53;
 * the battle keeps one flag table per status ($AFB6-$B03A + slot). */
#include "battle/battle.h"

#include "port/machine.h"

static uint8_t absx8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.x); }
static void set_absx8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.x, v); }
static uint8_t absy8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.y); }
static void set_absy8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.y, v); }

/* LDA abs,Y : AND #$FE : STA abs,Y */
static void clear_bit0_y(Snes *s, uint16_t table)
{
    set_absy8(s, table, m_lda8(s, (uint8_t)(m_lda8(s, absy8(s, table)) & 0xFE)));
}

/* LDA abs,Y : ORA #$01 : STA abs,Y */
static void set_bit0_y(Snes *s, uint16_t table)
{
    set_absy8(s, table, m_lda8(s, (uint8_t)(m_lda8(s, absy8(s, table)) | 0x01)));
}

/* LDA abs,X : BIT #mask -- true if the bit is set. */
static bool status_bit(Snes *s, uint16_t addr, uint8_t mask)
{
    m_lda8(s, absx8(s, addr));
    m_bit_imm8(s, mask);
    return !m_flag(s, CPU_FLAG_Z);
}

/* LDA a,X : ORA b,X : BIT #mask */
static bool either_status_bit(Snes *s, uint16_t a, uint16_t b, uint8_t mask)
{
    uint8_t v = m_lda8(s, absx8(s, a));
    m_lda8(s, (uint8_t)(v | absx8(s, b)));
    m_bit_imm8(s, mask);
    return !m_flag(s, CPU_FLAG_Z);
}

// $C1:B093
/* Set bit 0 of $5E2F,X when the 16-bit value at $5E30 is at most an eighth of
 * the one at $5E32 (presumably HP against maximum HP), clear it otherwise. */
void battle_update_low_hp_flag(Snes *s)
{
    set_absx8(s, 0x5E2F, m_lda8(s, (uint8_t)(m_lda8(s, absx8(s, 0x5E2F)) & 0xFE)));
    m_rep(s, CPU_FLAG_M);
    uint16_t max = m_lda16(s, m_read16(s, m_abs(s, 0x5E32) + s->cpu.x));
    for (int i = 0; i < 3; i++)
        max = m_lsr16(s, max);
    s->cpu.a = max;
    uint16_t hp = m_read16(s, m_abs(s, 0x5E30) + s->cpu.x);
    m_cmp16(s, max, hp);
    if (max >= hp) {
        m_sep(s, CPU_FLAG_M);
        set_absx8(s, 0x5E2F, m_lda8(s, (uint8_t)(m_lda8(s, absx8(s, 0x5E2F)) | 0x01)));
    }
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C1:B0B6
/* Rebuild combatant Y's status flag tables from its stat block (X): unless
 * $5E4A bit 7 is set, set or clear bit 0 of each table from its status bits,
 * and clear $B03A (and for enemies $5E0A) when $5E4B bit 1 is set. For the
 * party, $A0D1 reflects $5E4B bit 3 (when $5E4A bit 7 is clear) and ends the
 * routine when set; otherwise $AE7A follows $5E4B bit 2, setting $B268 to $FF
 * when it changes, and $5E4B bit 7 sets $5E0A to 4. */
void battle_update_status_flags(Snes *s)
{
    Cpu *c = &s->cpu;
    if (!status_bit(s, 0x5E4A, 0x80)) {
        if (status_bit(s, 0x5E4B, 0x02)) {
            set_absy8(s, 0xB03A, m_lda8(s, 0x00));
            m_cmp16(s, c->y, 0x0003);
            if (c->y >= 0x0003) {
                m_tdc(s);
                set_absy8(s, 0x5E0A, (uint8_t)c->a);
            }
        }
        static const struct { uint16_t table, a, b; uint8_t mask; } flags[10] = {
            { 0xB024, 0x5E4E, 0x5E53, 0x04 }, { 0xAFC1, 0x5E4B, 0, 0x40 }, { 0xB00E, 0x5E4B, 0, 0x10 },
            { 0xB019, 0x5E4E, 0x5E53, 0x20 }, { 0xB02F, 0x5E4D, 0x5E52, 0x02 }, { 0xAFCC, 0x5E4B, 0, 0x20 },
            { 0xAFED, 0x5E4E, 0x5E53, 0x40 }, { 0xAFD7, 0x5E4D, 0x5E52, 0x80 }, { 0xAFE2, 0x5E4D, 0x5E52, 0x40 },
            { 0xAFB6, 0x5E4B, 0, 0x80 },
        };
        for (int i = 0; i < 10; i++) {
            clear_bit0_y(s, flags[i].table);
            bool on = flags[i].b ? either_status_bit(s, flags[i].a, flags[i].b, flags[i].mask)
                                 : status_bit(s, flags[i].a, flags[i].mask);
            if (on)
                set_bit0_y(s, flags[i].table);
        }
    }

    /* $C1:B1CD */
    m_cmp16(s, c->y, 0x0003);
    if (c->y < 0x0003) {
        set_absy8(s, 0xA0D1, m_lda8(s, 0x00));
        if (!status_bit(s, 0x5E4A, 0x80) && status_bit(s, 0x5E4B, 0x08)) {
            set_absy8(s, 0xA0D1, m_lda8(s, 0x01));
            m_rts(s);
            return;
        }
    }
    uint8_t bit2 = m_lda8(s, (uint8_t)(m_lda8(s, absx8(s, 0x5E4B)) & 0x04));
    if (bit2 != 0) {
        uint8_t tracked = absy8(s, 0xAE7A);
        m_cmp8(s, bit2, tracked);
        if (bit2 != tracked) {
            set_absy8(s, 0xAE7A, bit2);
            set_absy8(s, 0xB268, m_lda8(s, 0xFF));
        }
    } else {
        uint8_t v = m_lda8(s, (uint8_t)(m_lda8(s, absx8(s, 0x5E4B)) ^ absy8(s, 0xAE7A)));
        if (m_lda8(s, v & 0x04) != 0) {
            set_absy8(s, 0xB268, m_lda8(s, 0xFF));
            set_absy8(s, 0xAE7A, m_lda8(s, 0x00));
        }
    }
    if (m_lda8(s, (uint8_t)(m_lda8(s, absx8(s, 0x5E4B)) & 0x80)) != 0)
        set_absy8(s, 0x5E0A, m_lda8(s, 0x04));
    m_rts(s);
}

// $C1:B223
/* For each of the 11 combatants ($B2D3), run $C1:B279 with X = its stat
 * block; an enemy whose $AEFF is $FF, and whose linked combatant's ($5E12)
 * $AEFF is too, gets $B23F = $FF and $B2B3 = 0. Then, when all three party
 * members' $AEFF are negative, set $24. */
void battle_update_all_combatants(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_write8(s, m_abs(s, 0xB2D3), (uint8_t)c->a);
    uint8_t slot;
    do {
        m_tdc(s);
        m_lda8(s, m_read8(s, m_abs(s, 0xB2D3)));
        m_set_index(s, &c->y, c->a);
        m_rep(s, CPU_FLAG_M);
        m_xba(s);
        c->a = m_lsr16(s, c->a);
        m_tax(s);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        m_jsr(s, 0xC1B234, 0xC1B279);
        if (c->e || (c->p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
            return;
        m_tdc(s);
        m_lda8(s, m_read8(s, m_abs(s, 0xB2D3)));
        m_set_index(s, &c->y, c->a);
        uint8_t value = m_lda8(s, absy8(s, 0xAEFF));
        m_cmp8(s, value, 0xFF);
        if (value == 0xFF) {
            m_cmp16(s, c->y, 0x0003);
            if (c->y >= 0x0003) {
                m_lda8(s, absy8(s, 0x5E12));
                m_tax(s);
                uint8_t linked = m_lda8(s, absx8(s, 0xAEFF));
                m_cmp8(s, linked, 0xFF);
                if (linked == 0xFF) {
                    set_absy8(s, 0xB23F, m_lda8(s, 0xFF));
                    set_absy8(s, 0xB2B3, m_lda8(s, 0x00));
                }
            }
        }
        uint32_t cell = m_abs(s, 0xB2D3);
        slot = (uint8_t)(m_read8(s, cell) + 1);
        m_write8(s, cell, slot);
        m_set_nz8(s, slot);
        slot = m_lda8(s, m_read8(s, cell));
        m_cmp8(s, slot, 0x0B);
    } while (slot < 0x0B);
    m_tdc(s);
    m_tax(s);
    do {
        if (!(m_lda8(s, absx8(s, 0xAEFF)) & 0x80)) {
            m_rts(s);
            return;
        }
        m_inx(s);
        m_cmp16(s, c->x, 0x0003);
    } while (c->x < 0x0003);
    m_write8(s, m_dp(s, 0x24), m_lda8(s, 0x01));
    m_rts(s);
}
