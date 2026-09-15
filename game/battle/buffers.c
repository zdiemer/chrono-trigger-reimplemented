/* Battle: resetting per-frame work buffers. */
#include "battle/battle.h"

#include "port/machine.h"

// $CE:F066
/* Hide sprites 4-127 in the OAM buffer (Y = $F0) and reset the high table
 * bytes at $0901-$091F to $AA (large sprites). Called with 8-bit A and 16-bit
 * X; returns with RTL, A = the direct page. */
void battle_clear_oam_buffer(Snes *s)
{
    Cpu *c = &s->cpu;
    m_sep(s, CPU_FLAG_X);
    for (uint8_t x = 0x10; x != 0; x = (uint8_t)(x - 4)) {
        m_set_index(s, &c->x, x);
        m_lda8(s, 0xF0);
        for (uint16_t row = 0; row < 0x200; row += 0x10)
            m_write8(s, m_abs(s, (uint16_t)(0x070D + row)) + c->x, 0xF0);
        for (int i = 0; i < 4; i++)
            m_set_index(s, &c->x, (uint16_t)(c->x - 1));
    }
    m_rep(s, CPU_FLAG_M);
    m_set_index(s, &c->x, 0x1E);
    m_lda16(s, 0xAAAA);
    do {
        m_write16(s, m_abs(s, 0x0900) + c->x, 0xAAAA);
        m_set_index(s, &c->x, (uint16_t)(c->x - 1));
        m_set_index(s, &c->x, (uint16_t)(c->x - 1));
    } while (c->x != 0);
    m_write16(s, m_abs(s, 0x0901), 0xAAAA);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_rep(s, CPU_FLAG_X);
    m_rtl(s);
}

// $CC:F110
/* Rebuild the battlefield occupancy grid: copy the 256-byte base grid at
 * $7E:7BFD to $7E:7CFD, then mark ($40) the cell under each present object
 * ($96F5 + slot) among the 11 from slot $80, which is 3 when $2989 bit 2 is
 * set (skipping the party) and 0 otherwise. A cell is the object's Y ($1D23)
 * high nibble and X ($1D0C) high nibble. Leaves the data bank at $7E. */
void battle_mark_object_grid(Snes *s)
{
    Cpu *c = &s->cpu;
    m_rep(s, CPU_FLAG_M);
    m_ldx16(s, 0x7BFD);
    m_set_index(s, &c->y, 0x7CFD);
    m_lda16(s, 0x00FF);
    m_mvn(s, 0x7E, 0x7E);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_dp(s, 0x80), 0x00);
    uint8_t flags = m_lda8(s, m_read8(s, m_abs(s, 0x2989)));
    if (m_lda8(s, flags & 0x04) != 0)
        m_write8(s, m_dp(s, 0x80), m_lda8(s, 0x03));
    m_write8(s, m_dp(s, 0x82), m_lda8(s, 0x0B));
    m_lda8(s, m_read8(s, m_dp(s, 0x80)));
    m_set_index(s, &c->y, c->a);
    uint8_t n;
    do {
        if (m_lda8(s, m_read8(s, m_abs(s, 0x96F5) + c->y)) != 0) {
            uint8_t y = m_lda8(s, m_read8(s, m_abs(s, 0x1D23) + c->y));
            m_write8(s, m_dp(s, 0x84), m_lda8(s, y & 0xF0));
            uint8_t x = m_lda8(s, m_read8(s, m_abs(s, 0x1D0C) + c->y));
            for (int i = 0; i < 4; i++)
                x = m_lsr8(s, x);
            m_lda8(s, (uint8_t)(x | m_read8(s, m_dp(s, 0x84))));
            m_set_index(s, &c->x, c->a);
            m_write8(s, m_abs(s, 0x7CFD) + c->x, m_lda8(s, 0x40));
        }
        m_set_index(s, &c->y, (uint16_t)(c->y + 1));
        n = (uint8_t)(m_read8(s, m_dp(s, 0x82)) - 1);
        m_write8(s, m_dp(s, 0x82), n);
        m_set_nz8(s, n);
    } while (n != 0);
    m_rtl(s);
}

// $C1:1C3A
/* Reset the 384-byte table at $0B40 from its initial contents at $D1:5800. */
void battle_reset_0b40_table(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_set_index(s, &c->x, c->a);
    do {
        uint8_t v = m_lda8(s, m_read8(s, 0xD15800 + c->x));
        m_write8(s, m_abs(s, 0x0B40) + c->x, v);
        m_set_index(s, &c->x, (uint16_t)(c->x + 1));
        m_cmp16(s, c->x, 0x0180);
    } while (c->x != 0x0180);
    m_rts(s);
}

// $FD:ACFD
/* Clear the 132-byte table at $B328. Returns with RTL. */
void battle_clear_b328_table(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_set_index(s, &c->x, c->a);
    do {
        m_write8(s, m_abs(s, 0xB328) + c->x, (uint8_t)c->a);
        m_set_index(s, &c->x, (uint16_t)(c->x + 1));
        m_cmp16(s, c->x, 0x0084);
    } while (c->x < 0x0084);
    m_rtl(s);
}

// $FD:ACEE
/* Clear $AD9B and the 176-byte table at $AD9C. Returns with RTL. */
void battle_clear_ad9c_table(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    m_write8(s, m_abs(s, 0xAD9B), (uint8_t)c->a);
    do {
        m_write8(s, m_abs(s, 0xAD9C) + c->x, (uint8_t)c->a);
        m_inx(s);
        m_cmp16(s, c->x, 0x00B0);
    } while (c->x < 0x00B0);
    m_rtl(s);
}
