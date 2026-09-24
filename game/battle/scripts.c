/* Battle: the animation script interpreter.
 *
 * Sixteen script channels: channel Y's program pointer lives at $5DBD + 4 * Y
 * (24-bit), its wait counter at $5DFD + Y, and whether it runs at $5DAC + Y.
 * While a channel runs, its pointer is in $E6-$E8, the channel in $A010 and
 * its pointer offset in $A00E; each opcode's handler ($C1:7A6B table) sets
 * $A00F to keep going within the frame. */
#include "battle/entries.h"

#include "port/machine.h"

static uint8_t abs8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr)); }
static void set_abs8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr), v); }

// $C1:4C9E
/* Run script channel Y (pointer offset A = 4 * Y) for this frame: count down
 * its wait, or execute opcodes until a handler leaves $A00F clear or an opcode
 * of $DB or above is reached, then store the pointer back. */
void battle_run_script_channel(Snes *s)
{
    Cpu *c = &s->cpu;
    set_abs8(s, 0xA00E, (uint8_t)c->a);
    m_tax(s);
    for (uint16_t i = 0; i < 3; i++)
        m_write8(s, m_dp(s, (uint8_t)(0xE6 + i)), m_lda8(s, m_read8(s, m_abs(s, (uint16_t)(0x5DBD + i)) + c->x)));
    m_write16(s, m_abs(s, 0xA010), c->y);
    m_tyx(s);
    uint32_t wait = m_abs(s, 0x5DFD) + c->x;
    if (m_lda8(s, m_read8(s, wait)) != 0) {
        uint8_t v = (uint8_t)(m_read8(s, wait) - 1);
        m_write8(s, wait, v);
        m_set_nz8(s, v);
        m_rts(s);
        return;
    }
    do {
        set_abs8(s, 0xA00F, 0x00);
        m_tdc(s);
        m_set_index(s, &c->y, c->a);
        uint32_t pc = m_read16(s, m_dp(s, 0xE6)) | (uint32_t)m_read8(s, m_dp(s, 0xE8)) << 16;
        uint8_t op = m_lda8(s, m_read8(s, pc + c->y));
        set_abs8(s, 0x9895, op);
        m_cmp8(s, op, 0xDB);
        if (op >= 0xDB)
            break;
        m_rep(s, CPU_FLAG_M);
        c->a = m_asl16(s, c->a);
        m_tax(s);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        m_jsr(s, 0xC14CD4, 0xC10000 | m_jump_table(s, 0xC1, 0x7A6B, c->x));
        if (c->e || (c->p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
            return;
    } while (m_lda8(s, abs8(s, 0xA00F)) != 0);
    m_lda8(s, abs8(s, 0xA00E));
    m_tax(s);
    for (uint16_t i = 0; i < 3; i++)
        m_write8(s, m_abs(s, (uint16_t)(0x5DBD + i)) + c->x, m_lda8(s, m_read8(s, m_dp(s, (uint8_t)(0xE6 + i)))));
    m_rts(s);
}

/* LDY #channel : LDA #offset : JSR $4C9E */
static bool run_channel(Snes *s, uint32_t site, uint16_t channel)
{
    m_set_index(s, &s->cpu.y, channel);
    m_lda8(s, (uint8_t)(channel * 4));
    m_jsr(s, site, 0xC14C9E);
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M;
}

// $C1:4BBE
/* Run the battle's animation script channels for this frame: channels 0-3
 * when enabled ($5DAC + channel), channel 4 when enabled and $AE99/$AE9A is
 * nonzero, and, unless $A3D1 is set, channels 8-15 with their number less 8
 * in $A2B6. */
void battle_run_scripts(Snes *s)
{
    static const uint16_t sites[16] = {
        0x4BC8, 0x4BD5, 0x4BE2, 0x4BEF, 0x4C04, 0, 0, 0,
        0x4C1C, 0x4C2E, 0x4C40, 0x4C52, 0x4C64, 0x4C76, 0x4C88, 0x4C9A,
    };
    for (uint16_t ch = 0; ch < 4; ch++)
        if (m_lda8(s, abs8(s, (uint16_t)(0x5DAC + ch))) != 0 && !run_channel(s, 0xC10000 | sites[ch], ch))
            return;
    if (m_lda8(s, abs8(s, 0x5DB0)) != 0) {
        uint8_t v = m_lda8(s, abs8(s, 0xAE99));
        if (m_lda8(s, (uint8_t)(v | abs8(s, 0xAE9A))) != 0 && !run_channel(s, 0xC10000 | sites[4], 4))
            return;
    }
    if (m_lda8(s, abs8(s, 0xA3D1)) != 0) {
        m_rts(s);
        return;
    }
    for (uint16_t ch = 8; ch < 16; ch++) {
        if (m_lda8(s, abs8(s, (uint16_t)(0x5DAC + ch))) == 0)
            continue;
        if (ch == 8)
            set_abs8(s, 0xA2B6, 0x00);
        else
            set_abs8(s, 0xA2B6, m_lda8(s, (uint8_t)(ch - 8)));
        if (!run_channel(s, 0xC10000 | sites[ch], ch))
            return;
    }
    m_rts(s);
}
