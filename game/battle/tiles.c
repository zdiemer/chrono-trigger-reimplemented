/* Battle: decoding tile graphics for upload.
 *
 * Tile descriptors (16-bit, read through a long pointer in $53) name 2-plane
 * graphics at $D0:0000 + number * 8 (bits 0-12) and how the tile is drawn
 * (bit 13: no third plane in the data; bits 13-14 also pick the flip
 * handler). Each decodes to 32 bytes of 4-plane tile at $2D00 + Y, the upper
 * planes taken from the lower ones masked by the palette mask in $5F. */
#include "battle/battle.h"

#include "port/machine.h"

static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }
static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }

/* The 24-bit address a long pointer on the direct page holds. */
static uint32_t long_ptr(Snes *s, uint8_t cell)
{
    return m_read16(s, m_dp(s, cell)) | (uint32_t)m_read8(s, m_dp(s, (uint8_t)(cell + 2))) << 16;
}

/* INC/DEC on a 16-bit direct-page word, setting N and Z. */
static uint16_t step_dp16(Snes *s, uint8_t cell, int delta)
{
    uint16_t v = (uint16_t)(dp16(s, cell) + delta);
    set_dp16(s, cell, v);
    m_set_nz16(s, v);
    return v;
}

/* TYA or TXA : CLC : ADC #n : TAY or TAX (16-bit) */
static void advance(Snes *s, uint16_t *reg, uint16_t n)
{
    m_lda16(s, *reg);
    m_set_flag(s, CPU_FLAG_C, false);
    s->cpu.a = m_adc16(s, s->cpu.a, n);
    m_set_index(s, reg, s->cpu.a);
}

static uint16_t tile_word(Snes *s, uint16_t k) { return m_read16(s, m_abs(s, (uint16_t)(0x2D00 + k)) + s->cpu.y); }
static void set_tile_word(Snes *s, uint16_t k, uint16_t v) { m_write16(s, m_abs(s, (uint16_t)(0x2D00 + k)) + s->cpu.y, v); }

/* Call a local subroutine as the JSR at `site` does: its return address is
 * pushed, and pulled again by the RTS that ends `body`. */
static void local_call(Snes *s, uint16_t site, void (*body)(Snes *))
{
    m_push16(s, (uint16_t)(site + 2));
    body(s);
    m_pull16(s);
}

/* $CD:1488: the two lower planes of eight rows from $D0:0000 + X, and the
 * upper planes: each row's two planes ORed, masked by $5F. */
static void decode_planes(Snes *s)
{
    Cpu *c = &s->cpu;
    for (uint16_t k = 0; k < 16; k += 2) {
        set_tile_word(s, k, m_lda16(s, m_read16(s, 0xD00000 + c->x + k)));
        m_xba(s);
        uint16_t v = m_lda16(s, (uint16_t)(c->a | tile_word(s, k)));
        set_tile_word(s, (uint16_t)(0x10 + k), m_lda16(s, (uint16_t)(v & dp16(s, 0x5F))));
    }
}

// $CD:1509
/* Decode the current tile unflipped. Without bit 13, the data has a third
 * plane (8 more bytes) whose bits go into the upper planes as well. */
static void decode_tile(Snes *s)
{
    Cpu *c = &s->cpu;
    uint16_t desc = m_lda16(s, m_read16(s, long_ptr(s, 0x53)));
    set_dp16(s, 0x65, desc);
    uint16_t number = m_lda16(s, desc & 0x1FFF);
    for (int i = 0; i < 3; i++)
        number = m_asl16(s, number);
    c->a = number;
    m_tax(s);
    uint16_t bit13 = m_lda16(s, (uint16_t)(m_read16(s, long_ptr(s, 0x53)) & 0x2000));
    if (bit13 != 0) {
        local_call(s, 0x151B, decode_planes);
        advance(s, &c->y, 0x0020);
        return;
    }
    local_call(s, 0x1525, decode_planes);
    advance(s, &c->x, 0x0010);
    for (uint16_t k = 0; k < 8; k++) {
        uint16_t bits = m_lda16(s, (uint16_t)(m_lda16(s, m_read16(s, 0xD00000 + c->x + k)) & 0x00FF));
        set_dp16(s, 0x57, bits);
        uint16_t v = m_lda16(s, (uint16_t)(bits | tile_word(s, (uint16_t)(0x10 + 2 * k))));
        m_lda16(s, (uint16_t)(v & dp16(s, 0x5F)));
        m_xba(s);
        set_tile_word(s, (uint16_t)(0x10 + 2 * k), m_lda16(s, (uint16_t)(c->a | dp16(s, 0x57))));
    }
    advance(s, &c->y, 0x0020);
}

// $CD:13FE
/* Swap the rows of one 16-byte half of the tile (Y) top to bottom, then
 * advance Y past it. */
static void flip_half_vertically(Snes *s)
{
    static const uint16_t pairs[4][2] = { { 0x0, 0xE }, { 0x2, 0xC }, { 0x4, 0xA }, { 0x6, 0x8 } };
    for (int i = 0; i < 4; i++) {
        set_dp16(s, 0x69, m_lda16(s, tile_word(s, pairs[i][0])));
        set_tile_word(s, pairs[i][0], m_lda16(s, tile_word(s, pairs[i][1])));
        set_tile_word(s, pairs[i][1], m_lda16(s, dp16(s, 0x69)));
    }
    advance(s, &s->cpu.y, 0x0010);
}

/* $CD:144A: mirror each of the tile's 32 bytes through the bit-reversal table
 * at $C0:FD00, advancing Y past it. */
static void flip_horizontally(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_dp(s, 0x61), m_lda8(s, 0x08));
    uint8_t n;
    do {
        for (uint16_t b = 0; b < 4; b++) {
            uint32_t cell = m_abs(s, (uint16_t)(0x2D00 + b)) + c->y;
            m_lda8(s, m_read8(s, cell));
            m_tax(s);
            m_write8(s, cell, m_lda8(s, m_read8(s, 0xC0FD00 + c->x)));
        }
        for (int i = 0; i < 4; i++)
            m_iny(s);
        n = (uint8_t)(dp8(s, 0x61) - 1);
        m_write8(s, m_dp(s, 0x61), n);
        m_set_nz8(s, n);
    } while (n != 0);
    m_rep(s, CPU_FLAG_M);
}

static void push_y(Snes *s) { m_push16(s, s->cpu.y); }
static void pull_y(Snes *s)
{
    s->cpu.y = m_pull16(s);
    m_set_nz16(s, s->cpu.y);
}

// $CD:13F6
/* Decode the tile, then flip both halves vertically. */
static void decode_flipped_vertically(Snes *s)
{
    push_y(s);
    local_call(s, 0x13F7, decode_tile);
    pull_y(s);
    local_call(s, 0x13FB, flip_half_vertically);
    flip_half_vertically(s);
}

// $CD:1445
static void decode_flipped_horizontally(Snes *s)
{
    push_y(s);
    local_call(s, 0x1446, decode_tile);
    pull_y(s);
    flip_horizontally(s);
}

// $CD:13EE
static void decode_flipped_both(Snes *s)
{
    push_y(s);
    local_call(s, 0x13EF, decode_flipped_vertically);
    pull_y(s);
    flip_horizontally(s);
}

// $CD:1368
/* Decode the 32 tiles of row set X ($67 of them, counted down) from the
 * descriptor table at $CF:A000 + X * 64 into $2D00, with the palette for each
 * tile taken two bits at a time from the row's palette word ($CF:E000 + X * 2,
 * copied to $CC9D and $CC9F) and turned into a plane mask through the table at
 * $CD:1300. */
void battle_decode_tile_row(Snes *s)
{
    Cpu *c = &s->cpu;
    m_write8(s, m_dp(s, 0x55), m_lda8(s, 0xCF));
    m_write8(s, m_dp(s, 0x59), m_lda8(s, 0xCF));
    m_rep(s, CPU_FLAG_M);
    m_txa(s);
    c->a = m_asl16(s, c->a);
    set_dp16(s, 0x57, c->a);
    for (int i = 0; i < 5; i++)
        c->a = m_asl16(s, c->a);
    m_set_flag(s, CPU_FLAG_C, false);
    set_dp16(s, 0x53, c->a = m_adc16(s, c->a, 0xA000));
    m_ldx16(s, dp16(s, 0x57));
    uint16_t palettes = m_lda16(s, m_read16(s, 0xCFE000 + c->x));
    m_write16(s, m_abs(s, 0xCC9D), palettes);
    m_write16(s, m_abs(s, 0xCC9F), palettes);
    m_tdc(s);
    m_set_index(s, &c->y, c->a);
    set_dp16(s, 0x5B, 0x0000);
    static const struct { uint16_t site; void (*fn)(Snes *); } handlers[4] = {
        { 0x13B9, decode_tile }, { 0x13B9, decode_flipped_horizontally },
        { 0x13B9, decode_flipped_vertically }, { 0x13B9, decode_flipped_both },
    };
    do {
        /* $CD:1391 */
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        uint8_t v = m_lda8(s, dp8(s, 0x5B));
        v = m_lda8(s, v & 0x18);
        for (int i = 0; i < 3; i++)
            v = m_lsr8(s, v);
        m_set_a8(s, v);
        m_tax(s);
        m_write8(s, m_dp(s, 0x5D), m_lda8(s, m_read8(s, m_abs(s, 0xCC9D) + c->x)));

        /* JSR $13CA: this tile's two palette bits, times 2 */
        m_push16(s, 0x13A3);
        uint8_t shift = m_lda8(s, (uint8_t)(dp8(s, 0x5B) & 0x06));
        m_tax(s);
        uint8_t bits = m_lda8(s, dp8(s, 0x5D));
        for (int i = 0; i < shift; i++)
            bits = m_lsr8(s, bits);
        bits = m_lda8(s, bits & 0x03);
        m_set_a8(s, m_asl8(s, bits));
        m_pull16(s);

        m_tax(s);
        m_rep(s, CPU_FLAG_M);
        set_dp16(s, 0x5F, m_lda16(s, m_read16(s, 0xCD1300 + c->x)));
        m_lda16(s, m_read16(s, long_ptr(s, 0x53)));
        m_xba(s);
        for (int i = 0; i < 5; i++)
            c->a = m_lsr16(s, c->a);
        m_lda16(s, (uint16_t)(c->a & 0x0006));
        m_tax(s);
        local_call(s, handlers[c->x >> 1].site, handlers[c->x >> 1].fn);
        step_dp16(s, 0x5B, +1);
        step_dp16(s, 0x53, +1);
        step_dp16(s, 0x53, +1);
    } while (step_dp16(s, 0x67, -1) != 0);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}
