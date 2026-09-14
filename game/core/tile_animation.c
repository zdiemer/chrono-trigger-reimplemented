/* Animated background tiles: uploads from the animation tables in $7F:0400.
 *
 * Twelve slots of 5 bytes on the direct page $0500, from $05B0: a 16-bit
 * frame accumulator (bits 9-11 pick the frame), the VRAM address (negative:
 * slot unused) and the source bank. Slot k's table at $7F:0400 + $20 * k holds
 * the accumulator step (per frame) and, $10 bytes later, the source address. */
#include "core/tile_animation.h"

#include "port/machine.h"

static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }

/* One slot: when active, step its accumulator and send 128 bytes of the
 * current frame to VRAM over DMA channel 7 (16-bit A, 8-bit X and Y = $80). */
static void upload_slot(Snes *s, uint8_t base, uint32_t table)
{
    Cpu *c = &s->cpu;
    uint16_t vram = m_lda16(s, dp16(s, (uint8_t)(base + 2)));
    if (vram & 0x8000)
        return;
    m_write16(s, 0x2116, vram);
    m_lda16(s, (uint16_t)(dp16(s, base) & 0x0E00));
    m_xba(s);
    m_tax(s);
    m_lda16(s, dp16(s, base));
    m_set_flag(s, CPU_FLAG_C, false);
    c->a = m_adc16(s, c->a, m_read16(s, table + c->x));
    m_write16(s, m_dp(s, base), c->a);
    m_write16(s, 0x4372, m_lda16(s, m_read16(s, table + 0x10 + c->x)));
    m_set_index(s, &c->x, m_read8(s, m_dp(s, (uint8_t)(base + 4))));
    m_write8(s, 0x4374, (uint8_t)c->x);
    m_write16(s, 0x4375, 0x0000);
    m_write8(s, 0x4375, (uint8_t)c->y);
    m_write8(s, 0x420B, (uint8_t)c->y);
}

// $FD:E022
/* Upload animated tiles: six slots on even frames of the counter at $051D
 * (slots 0-5, from $05B0) and the other six on odd ones (from $05CE). Returns
 * with RTL, the register widths, direct page and data bank as they were. */
void tile_animation_upload(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_push16(s, c->d);
    m_push8(s, c->db);
    m_lda8(s, 0x00);
    m_push8(s, (uint8_t)c->a);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_write8(s, 0x2115, m_lda8(s, 0x80));
    m_write8(s, 0x4370, m_lda8(s, 0x41));
    m_write8(s, 0x4371, m_lda8(s, 0x18));
    m_ldx16(s, 0x0500);
    m_push16(s, c->x);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    uint8_t count = (uint8_t)(m_read8(s, m_dp(s, 0x1D)) + 1);
    m_write8(s, m_dp(s, 0x1D), count);
    m_set_nz8(s, count);
    m_sep(s, CPU_FLAG_X);
    m_set_index(s, &c->y, 0x80);
    uint8_t v = m_lsr8(s, m_lda8(s, m_read8(s, m_dp(s, 0x1D))));
    m_set_a8(s, v);
    bool odd = m_flag(s, CPU_FLAG_C);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
    for (uint8_t k = 0; k < 6; k++) {
        uint8_t slot = (uint8_t)(k + (odd ? 6 : 0));
        upload_slot(s, (uint8_t)(0xB0 + 5 * slot), 0x7F0400 + 0x20 * (uint32_t)slot);
    }
    m_lda16(s, 0x0000);
    m_sep(s, CPU_FLAG_M);
    m_rep(s, CPU_FLAG_X);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    c->p = m_pull8(s);
    if (c->p & CPU_FLAG_X) {
        c->x &= 0xFF;
        c->y &= 0xFF;
    }
    m_rtl(s);
}
