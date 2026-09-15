/* Menu: drawing font glyphs into tile graphics.
 *
 * A glyph's two bitplanes come from the long pointers at $76 and $79 (pixel
 * $60 of each); pixels go to the tile buffer through the long pointer at $73,
 * at byte offsets from the table at $C2:5FE6 for layout $6E, shifted by the
 * plane mode $6A (and $62 bit 0 picking how the second plane is read). */
#include "menu/menu.h"

#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }

/* The 24-bit address a long pointer on the direct page holds. */
static uint32_t long_ptr(Snes *s, uint8_t cell)
{
    return m_read16(s, m_dp(s, cell)) | (uint32_t)m_read8(s, m_dp(s, (uint8_t)(cell + 2))) << 16;
}

/* LDY abs,X : STA [$73],Y (the 16-bit offset table at `table` in bank DB) */
static uint32_t target(Snes *s, uint16_t table)
{
    Cpu *c = &s->cpu;
    m_set_index(s, &c->y, m_read16(s, m_abs(s, table) + c->x));
    return long_ptr(s, 0x73) + c->y;
}

/* ROL on the 16-bit direct-page word $63. */
static void rol_63(Snes *s)
{
    uint32_t addr = m_dp(s, 0x63);
    uint16_t v = m_read16(s, addr);
    bool carry = v & 0x8000;
    v = (uint16_t)(v << 1 | (m_flag(s, CPU_FLAG_C) ? 1 : 0));
    m_write16(s, addr, v);
    m_set_flag(s, CPU_FLAG_C, carry);
    m_set_nz16(s, v);
}

/* Guard: the plane mode ($6A) is one of the eight the handler table at
 * $C2:5EAF covers; other values jump through bytes past its end. */
bool menu_glyph_mode_is_valid(Snes *s)
{
    return snes_bus_peek(&s->bus, m_dp(s, 0x6A)) < 8;
}

// $C2:5E89
/* Draw pixel $60 of the glyph: the byte from the first plane in the high
 * half of A, the second plane's (shifted left 4, or masked to its high nibble
 * when $62 bit 0 is clear) in the low half, then placed by mode $6A: 0 stores
 * both bytes at offsets 1 and 0; 1-4 shift right by 1-4 and OR the high byte
 * in; 5-7 shift left by 3-1 into a third byte at offset 0. */
void menu_draw_glyph_pixel(Snes *s)
{
    Cpu *c = &s->cpu;
    m_set_index(s, &c->y, dp16(s, 0x60));
    m_lda8(s, m_read8(s, long_ptr(s, 0x76) + c->y));
    m_xba(s);
    uint8_t mode_bit = m_lsr8(s, m_lda8(s, dp8(s, 0x62)));
    m_set_a8(s, mode_bit);
    uint8_t second = m_lda8(s, m_read8(s, long_ptr(s, 0x79) + c->y));
    if (m_flag(s, CPU_FLAG_C)) {
        for (int i = 0; i < 4; i++)
            second = m_asl8(s, second);
    } else {
        second = m_lda8(s, second & 0xF0);
    }
    m_set_a8(s, second);
    m_rep(s, CPU_FLAG_M);
    m_set_index(s, &c->y, c->a);
    m_write16(s, m_dp(s, 0x63), 0x0000);
    m_lda16(s, (uint16_t)(dp16(s, 0x6A) & 0x00FF));
    c->a = m_asl16(s, c->a);
    m_tax(s);
    uint16_t mode = (uint16_t)(c->x >> 1);
    m_lda16(s, c->y);   /* TYA */
    if (mode == 0) {
        m_sep(s, CPU_FLAG_M);
        m_ldx16(s, dp16(s, 0x6E));
        m_write8(s, target(s, 0x5FE8), (uint8_t)c->a);
        uint32_t t = target(s, 0x5FE6);
        m_xba(s);
        m_write8(s, t, (uint8_t)c->a);
    } else if (mode <= 4) {
        for (uint16_t i = 0; i < mode; i++)
            c->a = m_lsr16(s, c->a);
        m_sep(s, CPU_FLAG_M);
        m_ldx16(s, dp16(s, 0x6E));
        m_write8(s, target(s, 0x5FE8), (uint8_t)c->a);
        uint32_t t = target(s, 0x5FE6);
        m_xba(s);
        uint8_t v = m_lda8(s, (uint8_t)(c->a | m_read8(s, t)));
        m_write8(s, t, v);
    } else {
        for (uint16_t i = mode; i < 8; i++) {
            c->a = m_asl16(s, c->a);
            rol_63(s);
        }
        m_sep(s, CPU_FLAG_M);
        m_ldx16(s, dp16(s, 0x6E));
        m_write8(s, target(s, 0x5FEA), (uint8_t)c->a);
        uint32_t t = target(s, 0x5FE8);
        m_xba(s);
        m_write8(s, t, (uint8_t)c->a);
        t = target(s, 0x5FE6);
        uint8_t v = m_lda8(s, (uint8_t)(m_lda8(s, dp8(s, 0x63)) | m_read8(s, t)));
        m_write8(s, t, v);
    }
    m_rts(s);
}

/* JSR $5E89 : REP #$20 : INC $60 : INC $73 : SEP #$20 */
static void pixel_and_advance(Snes *s, uint16_t site)
{
    m_push16(s, (uint16_t)(site + 2));
    menu_draw_glyph_pixel(s);
    m_rep(s, CPU_FLAG_M);
    for (int i = 0; i < 2; i++) {
        static const uint8_t cells[2] = { 0x60, 0x73 };
        uint32_t addr = m_dp(s, cells[i]);
        uint16_t v = (uint16_t)(m_read16(s, addr) + 1);
        m_write16(s, addr, v);
        m_set_nz16(s, v);
    }
    m_sep(s, CPU_FLAG_M);
}

// $C2:5E36
/* Draw a glyph: two rows of pixels, 8 then 16, with $73 moved to the next
 * row ($F0 on from the end of the first) in between. Data bank $C2 while
 * drawing. */
void menu_draw_glyph(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_push8(s, m_lda8(s, 0xC2));
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_write8(s, m_dp(s, 0x68), m_lda8(s, 0x04));
    m_write8(s, m_dp(s, 0x60), 0x00);
    m_write8(s, m_dp(s, 0x61), 0x00);
    uint8_t n;
    do {
        pixel_and_advance(s, 0x5E43);
        pixel_and_advance(s, 0x5E4E);
        n = (uint8_t)(dp8(s, 0x68) - 1);
        m_write8(s, m_dp(s, 0x68), n);
        m_set_nz8(s, n);
    } while (n != 0);
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, dp16(s, 0x73));
    m_set_flag(s, CPU_FLAG_C, false);
    c->a = m_adc16(s, c->a, 0x00F0);
    m_write16(s, m_dp(s, 0x73), c->a);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_dp(s, 0x68), m_lda8(s, 0x08));
    do {
        pixel_and_advance(s, 0x5E6D);
        pixel_and_advance(s, 0x5E78);
        n = (uint8_t)(dp8(s, 0x68) - 1);
        m_write8(s, m_dp(s, 0x68), n);
        m_set_nz8(s, n);
    } while (n != 0);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rts(s);
}
