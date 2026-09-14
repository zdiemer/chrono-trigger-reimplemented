/* Battle: the window's gauges, drawn into the window tilemap at $0CC0. */
#include "battle/battle.h"

#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }
static void set_absy8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.y, v); }

// $C1:06F0
/* Draw party member $80's gauge: 4 tiles at the member's window position
 * ($CC:FA35 + $1A) filled in proportion to $99DD / $9F22 (32 pixels, 8 per
 * tile), full tiles $6F and a partial tile $67 + remaining pixels; the tile
 * attributes are $2D while $99DD is nonzero, $29 otherwise. The ratio is
 * computed with the hardware divider ($C1:00D7, flagged in $A029 for the
 * interrupt handlers). */
void battle_draw_gauge(Snes *s)
{
    Cpu *c = &s->cpu;
    m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
    m_lda16(s, dp16(s, 0x80));
    c->a = m_asl16(s, c->a);
    m_tax(s);
    m_lda16(s, m_read16(s, 0xCCFA35 + c->x));
    c->a = m_adc16(s, c->a, 0x001A);
    m_set_index(s, &c->y, c->a);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_ldx16(s, dp16(s, 0x80));
    set_dp8(s, 0xAD, m_lda8(s, m_read8(s, m_abs(s, 0x99DD) + c->x)));
    set_dp8(s, 0xAE, 0x00);
    set_dp8(s, 0xB3, m_lda8(s, m_read8(s, m_abs(s, 0x9F22) + c->x)));
    set_dp8(s, 0xB4, 0x00);

    /* JSR $010D: ASL x8 (16-bit) */
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, dp16(s, 0xAD));
    m_push16(s, 0x0717);
    for (int i = 0; i < 8; i++)
        c->a = m_asl16(s, c->a);
    m_pull16(s);
    set_dp16(s, 0xB1, c->a);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);

    /* JSR $00D7: $B5 = $B1-$B2 / $B3, $B7 = remainder */
    m_push16(s, 0x071F);
    uint32_t busy = m_abs(s, 0xA029);
    uint8_t flag = (uint8_t)(m_read8(s, busy) + 1);
    m_write8(s, busy, flag);
    m_set_nz8(s, flag);
    static const uint16_t div_regs[3] = { 0x4204, 0x4205, 0x4206 };
    for (int i = 0; i < 3; i++) {
        uint8_t v = m_lda8(s, dp8(s, (uint8_t)(0xB1 + i)));
        set_dp8(s, (uint8_t)(0x79 + i), v);
        m_write8(s, div_regs[i], v);
    }
    m_rep(s, CPU_FLAG_M);
    set_dp16(s, 0xB5, m_lda16(s, m_read16(s, 0x004214)));
    set_dp16(s, 0xB7, m_lda16(s, m_read16(s, 0x004216)));
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, busy, 0x00);
    m_pull16(s);

    /* JSR $011B: LSR x3 (16-bit) */
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, dp16(s, 0xB5));
    m_push16(s, 0x0726);
    for (int i = 0; i < 3; i++)
        c->a = m_lsr16(s, c->a);
    m_pull16(s);
    set_dp16(s, 0xB5, c->a);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);

    /* $C1:072C: 32 - filled pixels, split into full tiles and the rest */
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t empty = m_sbc8(s, m_lda8(s, 0x20), dp8(s, 0xB5));
    m_set_a8(s, empty);
    set_dp8(s, 0x82, empty);
    uint8_t tiles = empty;
    for (int i = 0; i < 3; i++)
        tiles = m_lsr8(s, tiles);
    set_dp8(s, 0x83, tiles);
    uint8_t pixels = tiles;
    for (int i = 0; i < 3; i++)
        pixels = m_asl8(s, pixels);
    set_dp8(s, 0x86, pixels);
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t rest = m_sbc8(s, m_lda8(s, dp8(s, 0x82)), dp8(s, 0x86));
    m_set_a8(s, rest);
    set_dp8(s, 0x82, rest);

    m_lda8(s, 0x67);
    for (uint16_t k = 0; k < 8; k += 2)
        set_absy8(s, (uint16_t)(0x0CC0 + k), 0x67);
    m_ldx16(s, dp16(s, 0x80));
    uint8_t attr = m_lda8(s, m_read8(s, m_abs(s, 0x99DD) + c->x)) ? 0x2D : 0x29;
    m_lda8(s, attr);
    for (uint16_t k = 1; k < 8; k += 2)
        set_absy8(s, (uint16_t)(0x0CC0 + k), attr);
    while (m_lda8(s, dp8(s, 0x83)) != 0) {
        set_absy8(s, 0x0CC0, m_lda8(s, 0x6F));
        m_iny(s);
        m_iny(s);
        uint8_t n = (uint8_t)(dp8(s, 0x83) - 1);
        set_dp8(s, 0x83, n);
        m_set_nz8(s, n);
    }
    m_set_flag(s, CPU_FLAG_C, false);
    uint8_t part = m_lda8(s, dp8(s, 0x82));
    if (part != 0) {
        part = m_adc8(s, part, 0x67);
        m_set_a8(s, part);
        set_absy8(s, 0x0CC0, part);
    }
    m_rts(s);
}
