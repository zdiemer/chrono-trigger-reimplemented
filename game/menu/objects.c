/* Menu: sprite objects, run and drawn each frame from the vblank handler of
 * the mode started at $C2:0000 (every movie that shows a menu screen reaches
 * it, the new-game movie included).
 *
 * Objects are $40-byte slots from $0B30 to $1B2F; a slot's first word is its
 * handler (0 for a free slot). Objects queue sprite groups into a list of
 * $10-byte nodes at $7E:B000-$B3FF: a node has previous and next links (+0,
 * +2), a position (+4, +6), attribute bits (+8-$0A) and a long pointer to its
 * sprite group (+$0B), whose first byte counts 4-byte entries (x and y offset,
 * tile word). $B500 heads the list in drawing order, $B504 the free nodes.
 * Drawing writes sprites into the OAM buffer at $0700 (index $4D) and size and
 * X high bits into $0900. */
#include "menu/objects.h"

#include "platform/video.h"
#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }
static uint32_t abs_x(Snes *s, uint16_t offset) { return m_abs(s, offset) + s->cpu.x; }
static uint32_t abs_y(Snes *s, uint16_t offset) { return m_abs(s, offset) + s->cpu.y; }

/* The 24-bit address [cell],Y. */
static uint32_t long_y(Snes *s, uint8_t cell)
{
    uint32_t p = m_read16(s, m_dp(s, cell)) | (uint32_t)m_read8(s, m_dp(s, (uint8_t)(cell + 2))) << 16;
    return (p + s->cpu.y) & 0xFFFFFF;
}

/* The address (cell),Y: a 16-bit pointer in the data bank. */
static uint32_t ptr_y(Snes *s, uint8_t cell) { return (m_abs(s, dp16(s, cell)) + s->cpu.y) & 0xFFFFFF; }

static void plb(Snes *s)
{
    s->cpu.db = m_pull8(s);
    m_set_nz8(s, s->cpu.db);
}

/* LDA #$7E : PHA : PLB (after PHB), 8-bit A */
static void set_bank_7e(Snes *s)
{
    m_push8(s, m_lda8(s, 0x7E));
    plb(s);
}

/* BIT #imm, 16-bit: only Z. */
static void bit_imm16(Snes *s, uint16_t imm) { m_set_flag(s, CPU_FLAG_Z, (s->cpu.a & imm) == 0); }

/* DEC on a 16-bit word; returns the result. */
static uint16_t dec16(Snes *s, uint32_t addr)
{
    uint16_t v = (uint16_t)(m_read16(s, addr) - 1);
    m_write16(s, addr, v);
    m_set_nz16(s, v);
    return v;
}

/* DEC on a byte; returns the result. */
static uint8_t dec8(Snes *s, uint32_t addr)
{
    uint8_t v = (uint8_t)(m_read8(s, addr) - 1);
    m_write8(s, addr, v);
    m_set_nz8(s, v);
    return v;
}

static bool widths_are(Snes *s, uint8_t mx)
{
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == mx;
}

// $C2:0C4D
/* Reset the sprite node list: an empty drawing list at $B500, the free list
 * at $B504 ($B3F0 last, $B000 first), and each node's links from the table
 * at $B400 (built by $C2:0BF6). Returns with 16-bit A. */
void menu_reset_sprite_nodes(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    set_bank_7e(s);
    m_rep(s, CPU_FLAG_M);
    m_ldx(s, 0xB500);
    m_txa(s);
    m_write16(s, abs_x(s, 0x0000), c->a);
    m_write16(s, abs_x(s, 0x0002), c->a);
    m_ldx(s, 0xB504);
    m_write16(s, abs_x(s, 0x0000), m_lda16(s, 0xB3F0));
    m_write16(s, abs_x(s, 0x0002), m_lda16(s, 0xB000));
    m_ldx(s, 0xB400);
    m_ldy(s, 0xB000);
    set_dp16(s, 0x08, m_lda16(s, 0x0008));
    do {
        /* 16 nodes a round: links +0/+2 of node Y + $10n from the words at X + 4n */
        for (uint16_t i = 0; i < 0x20; i += 2) {
            m_lda16(s, m_read16(s, abs_x(s, i)));
            m_write16(s, abs_y(s, (uint16_t)((i >> 2) << 4 | (i & 2))), c->a);
        }
        m_txa(s);
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, 0x0020);
        m_tax(s);
        m_lda16(s, c->y);
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, 0x0080);
        m_ldy(s, c->a);
    } while (dec16(s, m_dp(s, 0x08)) != 0);
    plb(s);
    m_rts(s);
}

// $C2:034D
/* Hide OAM buffer sprites (Y = $E0 in $0701 + 4n for 128 sprites, in blocks
 * of 8 per $20 steps of X) and clear the high bits table $0900-$091F. */
void menu_hide_all_sprites(Snes *s)
{
    Cpu *c = &s->cpu;
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_ldx(s, 0x60);
    static const uint16_t base[4] = { 0x0701, 0x0781, 0x0801, 0x0881 };
    do {
        m_lda8(s, 0xE0);
        for (int b = 0; b < 4; b++)
            for (uint16_t i = 0; i < 8; i++)
                m_write8(s, abs_x(s, (uint16_t)(base[b] + i * 4)), (uint8_t)c->a);
        m_txa(s);
        m_set_flag(s, CPU_FLAG_C, true);
        m_set_a8(s, m_sbc8(s, (uint8_t)c->a, 0x20));
        m_tax(s);
    } while (!(c->x & 0x80));
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    for (uint16_t i = 0; i < 0x20; i += 2)
        m_write16(s, m_abs(s, (uint16_t)(0x0900 + i)), 0x0000);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:051D
/* Run every object slot's handler (the slot's first word, reached through
 * JMP ($0050) at $C2:0553 with the slot in $4E): a handler returning carry
 * set frees its slot, otherwise the slot's counter at +3 is incremented. */
void menu_run_objects(Snes *s)
{
    Cpu *c = &s->cpu;
    m_ldx(s, 0x0B30);
    for (;;) {
        m_rep(s, CPU_FLAG_M);
        if (m_lda16(s, m_read16(s, abs_x(s, 0x0000))) != 0) {
            set_dp16(s, 0x50, m_lda16(s, m_read16(s, abs_x(s, 0x0000))));
            m_sep(s, CPU_FLAG_M);
            set_dp16(s, 0x4E, c->x);
            m_jsr(s, 0xC2053E, 0xC20553);
            if (m_x8(s) || c->e) {
                m_stop_at(s, 0xC20541, 0);
                return;
            }
            m_ldx(s, dp16(s, 0x4E));
            m_rep(s, CPU_FLAG_M);
            if (m_flag(s, CPU_FLAG_C)) {
                m_write16(s, abs_x(s, 0x0000), 0x0000);
            } else {
                uint16_t v = (uint16_t)(m_read16(s, abs_x(s, 0x0003)) + 1);
                m_write16(s, abs_x(s, 0x0003), v);
                m_set_nz16(s, v);
            }
        }
        m_txa(s);
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, 0x0040);
        m_tax(s);
        m_cmp16(s, c->x, 0x1B30);
        if (c->x == 0x1B30)
            break;
    }
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

/* The signed byte at [$55],Y as a word (LDA [$55],Y : AND #$00FF : BIT
 * #$0080 : BEQ : ORA #$FF00). */
static void signed_offset(Snes *s)
{
    Cpu *c = &s->cpu;
    m_lda16(s, m_read16(s, long_y(s, 0x55)));
    m_lda16(s, c->a & 0x00FF);
    bit_imm16(s, 0x0080);
    if (c->a & 0x0080)
        m_lda16(s, c->a | 0xFF00);
}

/* Make the coordinate in `cell` relative to the scroll position in `scroll`,
 * wrapping by `span` (the map's size) when it falls below `low` or at or above
 * `high`. */
static uint16_t wrap_relative(Snes *s, uint8_t cell, uint8_t scroll, uint16_t low, uint16_t high, uint16_t span)
{
    Cpu *c = &s->cpu;
    m_set_flag(s, CPU_FLAG_C, true);
    m_lda16(s, dp16(s, cell));
    c->a = m_sbc16(s, c->a, dp16(s, scroll));
    if (c->a & 0x8000) {
        m_cmp16(s, c->a, low);
        if (!m_flag(s, CPU_FLAG_C))
            c->a = m_adc16(s, c->a, span);
    } else {
        m_cmp16(s, c->a, high);
        if (m_flag(s, CPU_FLAG_C))
            c->a = m_sbc16(s, c->a, span);
    }
    set_dp16(s, cell, c->a);
    return c->a;
}

/* $C2:0D08: draw sprite node X's group. */
static void draw_node(Snes *s)
{
    Cpu *c = &s->cpu;
    m_ldy(s, m_read16(s, abs_x(s, 0x000B)));
    set_dp16(s, 0x55, c->y);
    set_dp8(s, 0x57, m_lda8(s, m_read8(s, abs_x(s, 0x000D))));
    {
        uint16_t y = c->y;
        c->y = 0;
        set_dp8(s, 0x00, m_lda8(s, m_read8(s, long_y(s, 0x55))));
        c->y = y;
    }
    set_dp8(s, 0x01, 0x00);
    m_ldy(s, 0x0000);
    m_lda8(s, m_read8(s, abs_x(s, 0x000A)));
    m_bit_imm8(s, 0x40);
    if (c->a & 0x40)
        m_iny(s);
    set_dp16(s, 0x13, c->y);
    m_rep(s, CPU_FLAG_M);
    {
        uint16_t v = (uint16_t)(dp16(s, 0x55) + 1);
        set_dp16(s, 0x55, v);
        m_set_nz16(s, v);
    }
    m_lda16(s, m_read16(s, abs_x(s, 0x0008)));
    m_xba(s);
    set_dp16(s, 0x02, m_lda16(s, c->a & 0x3E00));
    m_lda16(s, m_read16(s, abs_x(s, 0x0009)));
    set_dp16(s, 0x06, m_lda16(s, c->a & 0x3FFF));
    set_dp16(s, 0x0C, m_lda16(s, m_read16(s, abs_x(s, 0x0004))));
    set_dp16(s, 0x0E, m_lda16(s, m_read16(s, abs_x(s, 0x0006))));
    m_lda16(s, m_read16(s, abs_x(s, 0x0008)));
    bit_imm16(s, 0x0080);
    if (c->a & 0x0080) {
        wrap_relative(s, 0x0C, 0x22, 0xFD00, 0x0300, 0x0600);
        wrap_relative(s, 0x0E, 0x28, 0xFE00, 0x0200, 0x0400);
    }
    for (;;) {
        /* $C2:0D82 */
        uint16_t y = c->y;
        c->y = 0;
        signed_offset(s);
        c->y = y;
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, dp16(s, 0x0C));
        set_dp16(s, 0x08, c->a);
        set_dp16(s, 0x04, c->a);
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, 0x000F);
        m_cmp16(s, c->a, 0x010F);
        if (!m_flag(s, CPU_FLAG_C)) {
            m_ldy(s, 0x0001);
            signed_offset(s);
            m_set_flag(s, CPU_FLAG_C, false);
            c->a = m_adc16(s, c->a, dp16(s, 0x0E));
            set_dp16(s, 0x09, c->a);
            m_set_flag(s, CPU_FLAG_C, false);
            c->a = m_adc16(s, c->a, 0x000F);
            m_cmp16(s, c->a, 0x00EF);
            if (!m_flag(s, CPU_FLAG_C)) {
                m_iny(s);
                m_lda16(s, m_read16(s, long_y(s, 0x55)));
                m_set_flag(s, CPU_FLAG_C, false);
                c->a = m_adc16(s, c->a, dp16(s, 0x06));
                m_lda16(s, c->a | dp16(s, 0x02));
                m_ldy(s, c->a);
                m_lda16(s, dp16(s, 0x4D));
                m_lda16(s, c->a & 0x007F);
                c->a = m_asl16(s, c->a);
                c->a = m_asl16(s, c->a);
                m_tax(s);
                m_write16(s, abs_x(s, 0x0700), m_lda16(s, dp16(s, 0x08)));
                m_lda16(s, c->y);
                m_write16(s, abs_x(s, 0x0702), c->a);
                m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
                set_dp8(s, 0x16, m_lda8(s, dp8(s, 0x13)));
                m_lda8(s, dp8(s, 0x4D));
                m_lda8(s, (uint8_t)c->a & 0x03);
                for (int i = 0; i < 2; i++) {
                    uint8_t cell = i == 0 ? 0x16 : 0x05;
                    set_dp8(s, cell, m_lsr8(s, dp8(s, cell)));
                    /* ROL A */
                    bool carry = c->a & 0x80;
                    uint8_t v = (uint8_t)((uint8_t)c->a << 1 | (m_flag(s, CPU_FLAG_C) ? 1 : 0));
                    m_set_a8(s, v);
                    m_set_flag(s, CPU_FLAG_C, carry);
                    m_set_nz8(s, v);
                }
                m_tax(s);
                m_lda8(s, dp8(s, 0x4D));
                m_set_a8(s, m_lsr8(s, (uint8_t)c->a));
                m_set_a8(s, m_lsr8(s, (uint8_t)c->a));
                m_ldy(s, c->a & 0xFF);
                m_lda8(s, m_read8(s, 0xC20E0D + c->x));
                m_lda8(s, (uint8_t)c->a | m_read8(s, abs_y(s, 0x0900)));
                m_write8(s, abs_y(s, 0x0900), (uint8_t)c->a);
                m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
                uint16_t n = (uint16_t)(dp16(s, 0x4D) + 1);
                set_dp16(s, 0x4D, n);
                m_set_nz16(s, n);
            }
        }
        /* $C2:0DFB */
        m_set_flag(s, CPU_FLAG_C, false);
        m_lda16(s, dp16(s, 0x55));
        c->a = m_adc16(s, c->a, 0x0004);
        set_dp16(s, 0x55, c->a);
        if (dec16(s, m_dp(s, 0x00)) == 0)
            break;
    }
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:0CEA
/* Draw the sprite groups of the nodes in the drawing list at $7E:B500, in
 * order, into the OAM buffer. */
void menu_draw_sprite_nodes(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    set_bank_7e(s);
    m_ldy(s, 0xB500);
    m_ldx(s, m_read16(s, abs_y(s, 0x0002)));
    for (;;) {
        m_cmp16(s, c->x, 0xB500);
        if (c->x == 0xB500)
            break;
        set_dp16(s, 0x10, c->x);
        m_push16(s, 0x0CFE);
        draw_node(s);
        m_ldy(s, dp16(s, 0x10));
        m_ldx(s, m_read16(s, abs_y(s, 0x0002)));
    }
    plb(s);
    m_rts(s);
}

// $C2:0B53
/* Take a node from the free list and insert it into the drawing list for
 * object $4E: position from the object's +$14 and +$18, attributes from +$0F
 * and +$10, sprite group from the long pointer in $55. A node with attribute
 * bit 0 set is linked in before the first node without it; otherwise, unless
 * +$0A bit 7 is set (then it goes last), before the first node without bit 0
 * whose +6 (Y) is lower than its own. */
void menu_insert_sprite_node(Snes *s)
{
    Cpu *c = &s->cpu;
    m_sep(s, CPU_FLAG_M);
    m_push8(s, c->db);
    set_bank_7e(s);
    m_rep(s, CPU_FLAG_M);
    /* unlink the first free node */
    m_ldy(s, 0xB504);
    m_ldx(s, m_read16(s, abs_y(s, 0x0002)));
    m_ldy(s, m_read16(s, abs_x(s, 0x0000)));
    m_write16(s, abs_y(s, 0x0002), m_lda16(s, m_read16(s, abs_x(s, 0x0002))));
    m_ldy(s, c->a);
    m_write16(s, abs_y(s, 0x0000), m_lda16(s, m_read16(s, abs_x(s, 0x0000))));
    /* fill it */
    m_ldy(s, dp16(s, 0x4E));
    m_write16(s, abs_x(s, 0x0004), m_lda16(s, m_read16(s, abs_y(s, 0x0014))));
    m_write16(s, abs_x(s, 0x0006), m_lda16(s, m_read16(s, abs_y(s, 0x0018))));
    m_write16(s, abs_x(s, 0x000B), m_lda16(s, dp16(s, 0x55)));
    m_write16(s, abs_x(s, 0x0009), m_lda16(s, m_read16(s, abs_y(s, 0x0010))));
    m_sep(s, CPU_FLAG_M);
    m_write8(s, abs_x(s, 0x000D), m_lda8(s, dp8(s, 0x57)));
    m_write8(s, abs_x(s, 0x0008), m_lda8(s, m_read8(s, abs_y(s, 0x000F))));
    /* find the node to insert after */
    m_ldy(s, 0xB500);
    m_lda8(s, m_read8(s, abs_x(s, 0x0008)));
    m_bit_imm8(s, 0x01);
    if (c->a & 0x01) {
        m_rep(s, CPU_FLAG_M);
        for (;;) {
            m_ldy(s, m_lda16(s, m_read16(s, abs_y(s, 0x0002))));
            m_cmp16(s, c->y, 0xB500);
            if (c->y == 0xB500)
                break;
            m_lda16(s, m_read16(s, abs_y(s, 0x0008)));
            bit_imm16(s, 0x0001);
            if (!(c->a & 0x0001))
                break;
        }
    } else {
        m_lda8(s, m_read8(s, abs_x(s, 0x000A)));
        m_rep(s, CPU_FLAG_M);
        if (!(c->a & 0x80)) {
            for (;;) {
                m_ldy(s, m_lda16(s, m_read16(s, abs_y(s, 0x0002))));
                m_cmp16(s, c->y, 0xB500);
                if (c->y == 0xB500)
                    break;
                m_lda16(s, m_read16(s, abs_y(s, 0x0008)));
                bit_imm16(s, 0x0001);
                if (c->a & 0x0001)
                    continue;
                m_lda16(s, m_read16(s, abs_x(s, 0x0006)));
                uint16_t other = m_read16(s, abs_y(s, 0x0006));
                m_cmp16(s, c->a, other);
                if (c->a > other)
                    break;
            }
        }
    }
    /* $C2:0BD9: link X in before Y */
    m_ldy(s, m_lda16(s, m_read16(s, abs_y(s, 0x0000))));
    m_write16(s, abs_x(s, 0x0002), m_lda16(s, m_read16(s, abs_y(s, 0x0002))));
    m_txa(s);
    m_write16(s, abs_y(s, 0x0002), c->a);
    m_ldy(s, m_read16(s, abs_x(s, 0x0002)));
    m_write16(s, abs_x(s, 0x0000), m_lda16(s, m_read16(s, abs_y(s, 0x0000))));
    m_txa(s);
    m_write16(s, abs_y(s, 0x0000), c->a);
    plb(s);
    m_rts(s);
}

/* The 8-bit operand byte [$52],1 in A (with 16-bit Y = 1 first). */
static uint8_t script_byte(Snes *s, uint16_t offset)
{
    m_ldy(s, offset);
    return m_lda8(s, m_read8(s, long_y(s, 0x52)));
}

/* A countdown in the object's +$0E: loaded from the operand at `offset` when
 * zero, else decremented; true when it reaches zero. */
static bool object_countdown(Snes *s, uint16_t offset)
{
    if (m_lda8(s, m_read8(s, abs_x(s, 0x000E))) == 0) {
        m_write8(s, abs_x(s, 0x000E), script_byte(s, offset));
        return false;
    }
    return dec8(s, abs_x(s, 0x000E)) == 0;
}

/* The sprite script commands ($C2:0E4A table), each ending with the
 * command's length in A (Z set to stop). */
static void sprite_command(Snes *s, uint16_t handler)
{
    Cpu *c = &s->cpu;
    switch (handler) {
    case 0x0E5A: /* clear byte (object),operand */
    case 0x0E6B: /* increment it */
    case 0x0E7E: /* decrement it */
        m_sep(s, CPU_FLAG_M);
        script_byte(s, 0x0001);
        m_set_index(s, &c->y, c->a);
        if (handler == 0x0E5A) {
            m_tdc(s);
            m_write8(s, ptr_y(s, 0x4E), (uint8_t)c->a);
        } else {
            m_lda8(s, m_read8(s, ptr_y(s, 0x4E)));
            m_lda8(s, (uint8_t)(c->a + (handler == 0x0E6B ? 1 : -1)));
            m_write8(s, ptr_y(s, 0x4E), (uint8_t)c->a);
        }
        m_rep(s, CPU_FLAG_M);
        m_lda16(s, 0x0002);
        break;
    case 0x0E91: /* jump by the signed operand */
        m_ldy(s, 0x0001);
        m_lda16(s, m_read16(s, long_y(s, 0x52)));
        m_lda16(s, c->a & 0x00FF);
        bit_imm16(s, 0x0080);
        if (c->a & 0x0080)
            m_lda16(s, c->a | 0xFF00);
        m_set_flag(s, CPU_FLAG_C, false);
        break;
    case 0x0EA3: /* show a sprite group, for a count of frames unless +$0F bit 6 */
        m_sep(s, CPU_FLAG_M);
        m_ldx(s, dp16(s, 0x4E));
        m_lda8(s, m_read8(s, abs_x(s, 0x000F)));
        m_bit_imm8(s, 0x40);
        if (!(c->a & 0x40) && object_countdown(s, 0x0003)) {
            m_rep(s, CPU_FLAG_M);
            m_lda16(s, 0x0004);
            m_set_flag(s, CPU_FLAG_C, false);
            break;
        }
        set_dp8(s, 0x57, m_lda8(s, dp8(s, 0x54)));
        m_rep(s, CPU_FLAG_M);
        m_ldy(s, 0x0001);
        set_dp16(s, 0x55, m_lda16(s, m_read16(s, long_y(s, 0x52))));
        m_push16(s, 0x0ED8);
        menu_insert_sprite_node(s);
        m_tdc(s);
        m_set_flag(s, CPU_FLAG_C, false);
        break;
    case 0x0EDC: /* wait for a count of frames */
        m_sep(s, CPU_FLAG_M);
        m_ldx(s, dp16(s, 0x4E));
        if (object_countdown(s, 0x0001)) {
            m_rep(s, CPU_FLAG_M);
            m_lda16(s, 0x0002);
            m_set_flag(s, CPU_FLAG_C, false);
            break;
        }
        m_rep(s, CPU_FLAG_M);
        m_tdc(s);
        m_set_flag(s, CPU_FLAG_C, false);
        break;
    case 0x0F00: { /* queue a VRAM upload in the table at $60 */
        m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
        m_ldx(s, dp8(s, 0x5F));
        {
            uint8_t v = (uint8_t)(dp8(s, 0x5E) + 1);
            set_dp8(s, 0x5E, v);
            m_set_nz8(s, v);
        }
        m_write8(s, abs_x(s, 0x0062), script_byte(s, 0x01));
        m_write8(s, (uint16_t)(c->d + 0x67 + c->x), m_lda8(s, 0x80));
        m_rep(s, CPU_FLAG_M);
        static const uint8_t cells[3] = { 0x60, 0x63, 0x65 };
        for (int i = 0; i < 3; i++) {
            m_ldy(s, (uint16_t)(2 + 2 * i));
            m_lda16(s, m_read16(s, long_y(s, 0x52)));
            m_write16(s, (uint16_t)(c->d + cells[i] + c->x), c->a);
        }
        m_sep(s, CPU_FLAG_M);
        m_txa(s);
        m_set_flag(s, CPU_FLAG_C, false);
        m_set_a8(s, m_adc8(s, (uint8_t)c->a, 0x08));
        set_dp8(s, 0x5F, (uint8_t)c->a);
        set_dp8(s, 0x5E, 0x00);
        m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
        m_lda16(s, 0x0008);
        break;
    }
    default: /* $C2:0F35: stop */
        m_tdc(s);
        m_set_flag(s, CPU_FLAG_C, true);
        break;
    }
}

// $C2:0E1D
/* Run object $4E's sprite script from the long pointer at +$0B until a
 * command stops it, saving the position back. */
void menu_run_sprite_script(Snes *s)
{
    Cpu *c = &s->cpu;
    m_sep(s, CPU_FLAG_M);
    m_ldx(s, dp16(s, 0x4E));
    set_dp8(s, 0x54, m_lda8(s, m_read8(s, abs_x(s, 0x000D))));
    m_rep(s, CPU_FLAG_M);
    set_dp16(s, 0x52, m_lda16(s, m_read16(s, abs_x(s, 0x000B))));
    for (;;) {
        uint16_t y = c->y;
        c->y = 0;
        m_lda16(s, m_read16(s, long_y(s, 0x52)));
        c->y = y;
        m_lda16(s, c->a & 0x00FF);
        c->a = m_asl16(s, c->a);
        m_tax(s);
        uint16_t handler = m_jump_table(s, 0xC2, 0x0E4A, c->x);
        switch (handler) {
        case 0x0E5A: case 0x0E6B: case 0x0E7E: case 0x0E91:
        case 0x0EA3: case 0x0EDC: case 0x0F00: case 0x0F35:
            m_push16(s, 0x0E36);
            sprite_command(s, handler);
            m_pull16(s);
            break;
        default:
            m_jsr(s, 0xC20E34, 0xC20000 | handler);
            if (!widths_are(s, 0)) {
                c->pb = 0xC2;
                c->pc = 0x0E37;
                return;
            }
            break;
        }
        if (m_flag(s, CPU_FLAG_Z))
            break;
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, dp16(s, 0x52));
        set_dp16(s, 0x52, c->a);
    }
    m_ldx(s, dp16(s, 0x4E));
    m_write16(s, abs_x(s, 0x000B), m_lda16(s, dp16(s, 0x52)));
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:0F63
/* Run object $4E's command script from the long pointer at +$07 (bank +$09)
 * through the handler table at $C2:0F91 until a handler returns zero, saving
 * the position and clearing +$32 after each command. The handlers stay in the
 * original. */
void menu_run_object_script(Snes *s)
{
    Cpu *c = &s->cpu;
    m_ldx(s, dp16(s, 0x4E));
    m_sep(s, CPU_FLAG_M);
    set_dp8(s, 0x5A, m_lda8(s, m_read8(s, abs_x(s, 0x0009))));
    m_rep(s, CPU_FLAG_M);
    set_dp16(s, 0x58, m_lda16(s, m_read16(s, abs_x(s, 0x0007))));
    for (;;) {
        uint16_t y = c->y;
        c->y = 0;
        m_lda16(s, m_read16(s, long_y(s, 0x58)));
        c->y = y;
        m_lda16(s, c->a & 0x00FF);
        c->a = m_asl16(s, c->a);
        m_tax(s);
        m_jsr(s, 0xC20F7A, 0xC20000 | m_jump_table(s, 0xC2, 0x0F91, c->x));
        if (!widths_are(s, 0)) {
            c->pb = 0xC2;
            c->pc = 0x0F7D;
            return;
        }
        if (m_flag(s, CPU_FLAG_Z))
            break;
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, dp16(s, 0x58));
        set_dp16(s, 0x58, c->a);
        m_ldx(s, dp16(s, 0x4E));
        m_write16(s, abs_x(s, 0x0007), c->a);
        m_write16(s, abs_x(s, 0x0032), 0x0000);
    }
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

/* ROR on an 8-bit value with the CPU's flags. */
static uint8_t ror8(Snes *s, uint8_t v)
{
    uint8_t r = (uint8_t)(v >> 1 | (m_flag(s, CPU_FLAG_C) ? 0x80 : 0));
    m_set_flag(s, CPU_FLAG_C, v & 0x01);
    m_set_nz8(s, r);
    return r;
}

// $C2:7759
/* Object handler (reached through the slot's handler word): unless $1C bit 7
 * (which only turns HDMA channel 1 off in $4A) or $0294 bit 4 is set, set up
 * HDMA channel 1 on the table at $7E:8A35 and fill the 112 entries at $8B56
 * with the scroll position $24 plus a wave: the sine table at $C0:F900 ($C2:225E,
 * a JSL ported inline) from a phase in $0A that starts at the word read at
 * $0022 + $01C0 (which advances by 4 each frame). Returns with carry clear. */
void menu_object_wave_hdma(Snes *s)
{
    Cpu *c = &s->cpu;
    if (m_lda8(s, dp8(s, 0x1C)) & 0x80) {
        m_lda8(s, 0x02);
        m_trb8(s, m_dp(s, 0x4A));
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    m_lda8(s, m_read8(s, m_abs(s, 0x0294)));
    m_bit_imm8(s, 0x10);
    if (c->a & 0x10) {
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    if (m_lda8(s, m_read8(s, abs_x(s, 0x0002))) == 0) {
        uint8_t v = (uint8_t)(m_read8(s, abs_x(s, 0x0002)) + 1);
        m_write8(s, abs_x(s, 0x0002), v);
        m_set_nz8(s, v);
        m_write8(s, abs_x(s, 0x0022), 0x00);
        m_write8(s, abs_x(s, 0x0023), 0x00);
    }
    m_lda8(s, 0xF0);
    m_write8(s, 0x7E8A35, (uint8_t)c->a);
    m_write8(s, 0x7E8A38, (uint8_t)c->a);
    m_tdc(s);
    m_write8(s, 0x7E8A3B, (uint8_t)c->a);
    m_rep(s, CPU_FLAG_M);
    m_write16(s, 0x7E8A21, m_lda16(s, 0x8B56));
    m_write16(s, 0x7E8A23, m_lda16(s, 0x8C36));
    m_sep(s, CPU_FLAG_M);
    /* STY $4310 : STY $4312 : STA $4314 : STA $4317 -- slot 1 reads the table
     * at $7E:8A35 indirectly and drives BG3's horizontal scroll per line. */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,     VID_STEP_DEST,          VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SOURCE_BANK, VID_STEP_INDIRECT_BANK, VID_STEP_END,
    };
    static const VidLineEffect wave = { .target = VID_LINE_SCROLL_BG3_H, .table = PF_GAME(0x7E8A35),
                                        .indirect = true, .indirect_bank = 0x7E };
    m_ldy(s, 0x1142);
    m_ldy(s, 0x8A35);
    m_lda8(s, 0x7E);
    vid_set_line_effect(m_platform(s), PF_AT(0xC2779E), 1, &wave, steps);
    m_lda8(s, 0x02);
    m_tsb8(s, m_dp(s, 0x4A));
    m_rep(s, CPU_FLAG_M);
    m_ldx(s, 0x0000);
    do {
        m_lda16(s, dp16(s, 0x24));
        m_write16(s, 0x7E8B56 + c->x, c->a);
        for (int i = 0; i < 4; i++)
            m_inx(s);
        m_cmp16(s, c->x, 0x01C0);
    } while (c->x != 0x01C0);
    set_dp16(s, 0x0A, m_lda16(s, m_read16(s, abs_x(s, 0x0022))));
    m_set_flag(s, CPU_FLAG_C, false);
    c->a = m_adc16(s, c->a, 0x0004);
    m_write16(s, abs_x(s, 0x0022), c->a);
    m_ldx(s, 0x0002);
    do {
        m_txy(s);
        m_lda16(s, dp16(s, 0x0A));
        /* JSL $C2:225E: A = sine of phase A (10 bits), signed */
        m_push8(s, 0xC2);
        m_push16(s, 0x77DC);
        m_lda16(s, c->a & 0x03FF);
        m_tax(s);
        m_lda16(s, m_read16(s, 0xC0F900 + c->x));
        m_lda16(s, c->a & 0x00FF);
        m_cmp16(s, c->x, 0x0200);
        if (m_flag(s, CPU_FLAG_C)) {
            m_lda16(s, c->a ^ 0xFFFF);
            m_lda16(s, (uint16_t)(c->a + 1));
        }
        m_pull16(s);
        m_pull8(s);
        uint16_t phase = (uint16_t)(dp16(s, 0x0A) + 1);
        set_dp16(s, 0x0A, phase);
        m_set_nz16(s, phase);
        m_tyx(s);
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, dp16(s, 0x24));
        m_write16(s, 0x7E8B56 + c->x, c->a);
        for (int i = 0; i < 4; i++)
            m_inx(s);
        m_cmp16(s, c->x, 0x01C2);
    } while (c->x != 0x01C2);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C2:F871
/* Draw the sprites of the record at the direct page (the records at $1800,
 * $40 bytes each, run by the task at $C2:F657): $02 low nibble * 2 sprites
 * into the OAM buffer at $0800 + D / 4 (low byte), each at the record's
 * position ($0F-$10 plus $0D2E, $0E plus $0D30) plus the offset pair at
 * $22,X, with tile word $19 plus 2 per sprite; X high bits go to $0910 + D /
 * 64 when the X addition overflows the way $0007's sign says. Works in the
 * scratch bytes $0000-$0008 of the data bank. */
void menu_draw_record_sprites(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_rep(s, CPU_FLAG_M);
    m_tdc(s);
    c->a = m_lsr16(s, c->a);
    c->a = m_lsr16(s, c->a);
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_set_index(s, &c->y, c->a);
    for (int i = 0; i < 4; i++)
        m_set_a8(s, m_lsr8(s, (uint8_t)c->a));
    m_write8(s, m_abs(s, 0x0002), (uint8_t)c->a);
    m_write8(s, m_abs(s, 0x0003), 0x00);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_lda16(s, dp16(s, 0x02));
    m_lda16(s, c->a & 0x000F);
    for (int i = 0; i < 3; i++)
        c->a = m_asl16(s, c->a);
    m_write16(s, m_abs(s, 0x0004), c->a);
    m_write16(s, m_abs(s, 0x0008), 0x0000);
    m_lda16(s, dp16(s, 0x0F));
    c->a = m_adc16(s, c->a, m_read16(s, m_abs(s, 0x0D2E)));
    m_write16(s, m_abs(s, 0x0006), c->a);
    m_sep(s, CPU_FLAG_M);
    uint32_t sign = m_abs(s, 0x0007);
    m_write8(s, sign, m_lsr8(s, m_read8(s, sign)));
    m_write8(s, sign, ror8(s, m_read8(s, sign)));
    m_ldx(s, 0x0000);
    m_write8(s, m_abs(s, 0x0000), m_lda8(s, 0x01));
    do {
        m_lda8(s, m_read8(s, m_abs(s, 0x0006)));
        m_set_flag(s, CPU_FLAG_C, false);
        m_set_a8(s, m_adc8(s, (uint8_t)c->a, m_read8(s, (uint16_t)(c->d + 0x22 + c->x))));
        m_write8(s, abs_y(s, 0x0800), (uint8_t)c->a);
        m_set_a8(s, ror8(s, (uint8_t)c->a));
        m_lda8(s, (uint8_t)c->a ^ m_read8(s, (uint16_t)(c->d + 0x22 + c->x)));
        m_lda8(s, (uint8_t)c->a ^ m_read8(s, sign));
        if (c->a & 0x80) {
            m_push16(s, c->y);
            m_ldy(s, m_read16(s, m_abs(s, 0x0002)));
            m_lda8(s, m_read8(s, abs_y(s, 0x0910)));
            m_lda8(s, (uint8_t)c->a | m_read8(s, m_abs(s, 0x0000)));
            m_write8(s, abs_y(s, 0x0910), (uint8_t)c->a);
            m_ldy(s, m_pull16(s));
        }
        m_lda8(s, dp8(s, 0x0E));
        m_set_flag(s, CPU_FLAG_C, false);
        m_set_a8(s, m_adc8(s, (uint8_t)c->a, m_read8(s, (uint16_t)(c->d + 0x23 + c->x))));
        m_set_flag(s, CPU_FLAG_C, false);
        m_set_a8(s, m_adc8(s, (uint8_t)c->a, m_read8(s, m_abs(s, 0x0D30))));
        m_write8(s, abs_y(s, 0x0801), (uint8_t)c->a);
        m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
        m_lda16(s, m_read16(s, m_abs(s, 0x0008)));
        c->a = m_adc16(s, c->a, dp16(s, 0x19));
        m_write16(s, abs_y(s, 0x0802), c->a);
        m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
        m_lda16(s, c->y);
        c->a = m_adc16(s, c->a, 0x0004);
        m_ldy(s, c->a);
        m_sep(s, CPU_FLAG_M);
        m_lda8(s, m_read8(s, m_abs(s, 0x0008)));
        m_lda8(s, (uint8_t)c->a | 0x10);
        m_lda8(s, (uint8_t)(c->a + 1));
        m_lda8(s, (uint8_t)(c->a + 1));
        m_lda8(s, (uint8_t)c->a & 0xEF);
        m_write8(s, m_abs(s, 0x0008), (uint8_t)c->a);
        for (int i = 0; i < 2; i++) {
            uint32_t bit = m_abs(s, 0x0000);
            m_write8(s, bit, m_asl8(s, m_read8(s, bit)));
        }
        m_inx(s);
        m_inx(s);
        m_cmp16(s, c->x, m_read16(s, m_abs(s, 0x0004)));
    } while (!m_flag(s, CPU_FLAG_C));
    s->cpu.p = m_pull8(s);
    if (s->cpu.p & CPU_FLAG_X) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
    m_rts(s);
}
