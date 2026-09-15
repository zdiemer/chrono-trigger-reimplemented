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

// $C1:2F97
/* Choose combatant $94's status pose: walk the 4-byte entries of the table at
 * $CC:F77F (stat block offset, mask, pose, animation; a negative offset ends
 * it) for the first status bit set in its stat block ($CC:F8ED + slot * 2);
 * the first entry also applies when $A119 is set. A matching entry sets the
 * pose in $A441 (and $968C = $FF to restart the animation when it differs
 * from $A44C) and its animation in $A440 ($FF: 3 unless $A0A7 is set). With no
 * match, bit 0 of $5E2F picks pose $80 and animation $12 (unless $A09B is set);
 * otherwise the animation is 0, or 3 when $A09B is clear. */
void battle_select_status_pose(Snes *s)
{
    Cpu *c = &s->cpu;
    m_ldx16(s, m_read16(s, m_dp(s, 0x94)));
    uint8_t base = m_lda8(s, absx8(s, 0xA09B)) ? 0x00 : 0x03;
    m_write8(s, m_dp(s, 0x84), m_lda8(s, base));
    set_absx8(s, 0xA441, m_lda8(s, 0xFF));
    m_set_a8(s, m_asl8(s, m_lda8(s, m_read8(s, m_dp(s, 0x94)))));
    m_tax(s);
    m_rep(s, CPU_FLAG_M);
    m_set_index(s, &c->y, m_lda16(s, m_read16(s, 0xCCF8ED + c->x)));
    m_write16(s, m_dp(s, 0xA2), c->y);
    m_tdc(s);
    m_write16(s, m_dp(s, 0x80), c->a);
    m_sep(s, CPU_FLAG_M);
    m_ldx16(s, m_read16(s, m_dp(s, 0x94)));
    bool restart = m_lda8(s, absx8(s, 0xA119)) != 0;
    if (!restart)
        restart = m_lda8(s, m_read8(s, m_abs(s, 0x5E4A) + c->y)) & 0x80;
    if (restart) {
        set_absx8(s, 0xA0A8, m_lda8(s, 0x01));
        set_absx8(s, 0x968C, m_lda8(s, 0xFF));
    }
    m_tdc(s);
    m_tax(s);
    for (;;) {
        /* $C1:2FD5 */
        uint8_t offset = m_lda8(s, m_read8(s, 0xCCF77F + c->x));
        if (offset & 0x80) {
            /* $C1:302C: no status pose */
            m_set_index(s, &c->y, m_read16(s, m_dp(s, 0xA2)));
            if (m_lda8(s, (uint8_t)(m_lda8(s, m_read8(s, m_abs(s, 0x5E2F) + c->y)) & 0x01)) != 0) {
                m_set_index(s, &c->y, m_read16(s, m_dp(s, 0x94)));
                if (m_lda8(s, m_read8(s, m_abs(s, 0xA09B) + c->y)) == 0) {
                    m_write8(s, m_abs(s, 0xA441) + c->y, m_lda8(s, 0x80));
                    uint8_t last = m_read8(s, m_abs(s, 0xA44C) + c->y);
                    m_lda8(s, 0x12);
                    m_cmp8(s, 0x12, last);
                    if (last != 0x12) {
                        m_cmp16(s, c->y, 0x0003);
                        if (c->y < 0x0003)
                            m_write8(s, m_abs(s, 0x968C) + c->y, m_lda8(s, 0xFF));
                    }
                    m_write8(s, m_abs(s, 0xA440), m_lda8(s, 0x12));
                    m_rts(s);
                    return;
                }
            }
            break;
        }
        m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
        c->a = m_adc16(s, c->a, m_read16(s, m_dp(s, 0xA2)));
        m_set_index(s, &c->y, c->a);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        uint8_t bits = m_lda8(s, m_read8(s, m_abs(s, 0x5E4A) + c->y));
        bool match = m_lda8(s, (uint8_t)(bits & m_read8(s, 0xCCF780 + c->x))) != 0;
        if (!match) {
            m_txa(s);
            if ((uint8_t)c->a == 0) {
                m_ldx16(s, m_read16(s, m_dp(s, 0x94)));
                match = m_lda8(s, absx8(s, 0xA119)) != 0;
                m_tdc(s);
                m_tax(s);
            }
        }
        if (match) {
            /* $C1:2FFE */
            uint8_t anim = m_lda8(s, m_read8(s, 0xCCF782 + c->x));
            m_cmp8(s, anim, 0xFF);
            if (anim == 0xFF) {
                if (m_lda8(s, m_read8(s, m_abs(s, 0xA0A7))) != 0)
                    goto next;
                anim = m_lda8(s, 0x03);
            }
            m_write8(s, m_dp(s, 0x84), anim);
            m_set_index(s, &c->y, m_read16(s, m_dp(s, 0x94)));
            uint8_t pose = m_lda8(s, m_read8(s, 0xCCF781 + c->x));
            if (!(pose & 0x80)) {
                m_write8(s, m_abs(s, 0xA441) + c->y, pose);
                uint8_t last = m_read8(s, m_abs(s, 0xA44C) + c->y);
                m_cmp8(s, pose, last);
                if (pose != last)
                    m_write8(s, m_abs(s, 0x968C) + c->y, m_lda8(s, 0xFF));
            }
            break;
        }
    next:
        for (int i = 0; i < 4; i++)
            m_inx(s);
    }
    /* $C1:3056 */
    m_write8(s, m_abs(s, 0xA440), m_lda8(s, m_read8(s, m_dp(s, 0x84))));
    m_rts(s);
}

/* LDA $abs : AND #mask : CMP #mask : BEQ -- then the nibble (shifted down for
 * the high one) selects which of two table cells gets it. */
static void pair_nibble(Snes *s, uint16_t source, bool high, uint8_t first, uint16_t first_cell, uint8_t second,
                        uint16_t second_cell)
{
    uint8_t mask = high ? 0xF0 : 0x0F;
    uint8_t v = m_lda8(s, (uint8_t)(m_lda8(s, m_read8(s, m_abs(s, source))) & mask));
    m_cmp8(s, v, mask);
    if (v == mask)
        return;
    if (high) {
        for (int i = 0; i < 4; i++)
            v = m_lsr8(s, v);
        m_set_a8(s, v);
    }
    m_cmp8(s, v, first);
    if (v == first) {
        m_write8(s, m_abs(s, first_cell), v);
        return;
    }
    m_cmp8(s, v, second);
    if (v == second)
        m_write8(s, m_abs(s, second_cell), v);
}

// $C1:B575
/* Work out which party members can join a combination tech: fill the 3x3
 * table at $B3EB from the members' pairing nibbles ($93F3, $93FA, $9401: low
 * then high nibble naming a partner), then for each member check its row
 * through $FD:A8A5 (result in $B3EA) and, on success, flag it and its
 * partners in $B3E7-$B3E9. Finishes with $FD:A95F. */
void battle_update_combo_members(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    m_lda8(s, 0xFF);
    do {
        m_write8(s, m_abs(s, 0xB3EB) + c->x, 0xFF);
        m_inx(s);
        m_cmp16(s, c->x, 0x0009);
    } while (c->x < 0x0009);
    m_tdc(s);
    m_write8(s, m_abs(s, 0xB3EB), (uint8_t)c->a);
    m_write8(s, m_abs(s, 0xB3EE), m_lda8(s, (uint8_t)((uint8_t)c->a + 1)));
    m_write8(s, m_abs(s, 0xB3F1), m_lda8(s, (uint8_t)((uint8_t)c->a + 1)));
    for (uint16_t i = 0; i < 3; i++)
        m_write8(s, m_abs(s, (uint16_t)(0xB3E7 + i)), 0x00);
    for (int pass = 0; pass < 2; pass++) {
        bool high = pass == 1;
        pair_nibble(s, 0x93F3, high, 0x01, 0xB3EC, 0x02, 0xB3ED);
        pair_nibble(s, 0x93FA, high, 0x00, 0xB3F0, 0x02, 0xB3EF);
        pair_nibble(s, 0x9401, high, 0x00, 0xB3F2, 0x01, 0xB3F3);
    }

    /* $C1:B639: each member's row */
    static const struct {
        uint16_t row;
        uint32_t site;
        uint16_t self, partner1_cell, partner1_flag, partner2_cell, partner2_flag;
    } rows[3] = {
        { 0xB3EB, 0xC1B640, 0xB3E7, 0xB3EC, 0xB3E8, 0xB3ED, 0xB3E9 },
        { 0xB3EE, 0xC1B671, 0xB3E8, 0xB3EF, 0xB3E9, 0xB3F0, 0xB3E7 },
        { 0xB3F1, 0xC1B6A2, 0xB3E9, 0xB3F2, 0xB3E7, 0xB3F3, 0xB3E8 },
    };
    for (int r = 0; r < 3; r++) {
        m_tdc(s);
        m_tax(s);
        do {
            if (!(m_lda8(s, m_read8(s, m_abs(s, rows[r].row) + c->x)) & 0x80)) {
                m_jsl(s, rows[r].site, 0xFDA8A5);
                if (c->e || (c->p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
                    return;
                if (m_lda8(s, m_read8(s, m_abs(s, 0xB3EA))) != 0) {
                    m_write8(s, m_abs(s, rows[r].self), m_lda8(s, 0x01));
                    if (!(m_lda8(s, m_read8(s, m_abs(s, rows[r].partner1_cell))) & 0x80))
                        m_write8(s, m_abs(s, rows[r].partner1_flag), m_lda8(s, 0x01));
                    if (!(m_lda8(s, m_read8(s, m_abs(s, rows[r].partner2_cell))) & 0x80))
                        m_write8(s, m_abs(s, rows[r].partner2_flag), m_lda8(s, 0x01));
                    break;
                }
            }
            m_inx(s);
            m_cmp16(s, c->x, 0x0003);
        } while (c->x < 0x0003);
    }
    m_jsl(s, 0xC1B6CC, 0xFDA95F);
    if (c->e || (c->p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
        return;
    m_rts(s);
}
