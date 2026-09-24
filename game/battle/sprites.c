/* Battle: building the objects' sprite graphics and the bitmap sprites (banks
 * C1 and CC). Drawing the objects into the OAM buffer is battle/draw_objects.c. */
#include "battle/entries.h"

#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }
static uint8_t absx8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.x); }
static void set_absx8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.x, v); }
static uint8_t absy8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.y); }
static void set_absy8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.y, v); }
static uint8_t abs8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr)); }

/* JSR out; false if the callee didn't leave the M and X flags as `widths` (the
 * port then stops at the return address and the original carries on). */
static bool call_widths(Snes *s, uint32_t site, uint32_t target, uint8_t widths)
{
    m_jsr(s, site, target);
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == widths;
}

/* JSR out, expecting 8-bit A and 16-bit X back. */
static bool call(Snes *s, uint32_t site, uint32_t target) { return call_widths(s, site, target, CPU_FLAG_M); }

/* DEC on a direct-page byte, setting N and Z; returns the new value. */
static uint8_t dec_dp8(Snes *s, uint8_t cell)
{
    uint8_t v = (uint8_t)(dp8(s, cell) - 1);
    set_dp8(s, cell, v);
    m_set_nz8(s, v);
    return v;
}

/* The 24-bit address a long pointer on the direct page holds. */
static uint32_t long_ptr(Snes *s, uint8_t cell)
{
    return m_read16(s, m_dp(s, cell)) | (uint32_t)m_read8(s, m_dp(s, (uint8_t)(cell + 2))) << 16;
}

// $C1:1C55
/* Assemble the sprite graphics of object $9679 for its animation frame $967B,
 * unless it's already showing that frame ($9907 + object): the frame's tile
 * list (through $CC:F40A by the object's graphics kind $967C, and the per-
 * object table at $CC:F92D) is walked row by row ($85 rows of $80 tiles, from
 * $CC:F3C2 by kind) into the object's tile buffer in $7E (base from $CC:F941):
 * each 32-byte tile is cleared (descriptor $0000), copied from the graphics
 * set, or, with descriptor bit 14, copied through the flip table at
 * $C0:FD00. Then the frame's part offsets are copied into $9F47 (sign-extended
 * to 16 bits) and $9912 is incremented. */
void battle_build_object_graphics(Snes *s)
{
    Cpu *c = &s->cpu;
    m_ldx16(s, m_read16(s, m_abs(s, 0x9679)));
    set_dp16(s, 0x88, c->x);
    set_absx8(s, 0x9922, 0x00);
    uint8_t frame = m_lda8(s, abs8(s, 0x967B));
    uint8_t shown = absx8(s, 0x9907);
    m_cmp8(s, frame, shown);
    if (frame == shown) {
        m_rts(s);
        return;
    }
    set_absx8(s, 0x9907, frame);
    m_set_a8(s, m_asl8(s, m_lda8(s, absx8(s, 0x967C))));
    m_tax(s);
    set_dp8(s, 0xA7, m_lda8(s, m_read8(s, 0xCCF40A + c->x)));
    set_dp8(s, 0xA8, m_lda8(s, m_read8(s, 0xCCF40B + c->x)));
    set_dp8(s, 0xA5, m_lda8(s, abs8(s, 0x967B)));
    if (!call(s, 0xC11C7F, 0xC100A7))
        return;
    m_ldx16(s, dp16(s, 0x88));
    m_lda8(s, m_read8(s, 0xCCF92D + c->x));
    m_tax(s);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
    m_lda16(s, m_read16(s, m_abs(s, 0x1C78) + c->x));
    set_dp16(s, 0xBA, c->a = m_adc16(s, c->a, dp16(s, 0xA9)));
    set_dp16(s, 0xBD, m_lda16(s, m_read16(s, m_abs(s, 0x1C57) + c->x)));
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_push8(s, c->db);
    set_dp8(s, 0xBC, m_lda8(s, absx8(s, 0x1C7A)));
    m_push8(s, m_lda8(s, absx8(s, 0x1C59)));
    m_ldx16(s, dp16(s, 0x88));
    uint8_t kind = m_lda8(s, absx8(s, 0x967C));
    set_dp8(s, 0xAD, kind);
    m_cmp8(s, kind, 0x02);
    if (kind == 0x02) {
        uint8_t object = m_lda8(s, abs8(s, 0x9679));
        m_cmp8(s, object, 0x03);
        if (object != 0x03)
            set_dp8(s, 0xAD, m_lda8(s, 0x05));
    }
    set_dp8(s, 0xAE, m_lda8(s, 0x0E));
    if (!call(s, 0xC11CBE, 0xC10089))
        return;
    m_ldx16(s, dp16(s, 0xAF));
    uint8_t per_row = m_lda8(s, m_read8(s, 0xCCF3C2 + c->x));
    set_dp8(s, 0x80, per_row);
    set_dp8(s, 0x81, per_row);
    set_dp8(s, 0x85, m_lda8(s, m_read8(s, 0xCCF3C3 + c->x)));
    m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
    m_txa(s);
    set_dp16(s, 0x82, c->a = m_adc16(s, c->a, 0xF3B6));
    uint32_t p88 = m_dp(s, 0x88);
    m_write16(s, p88, m_asl16(s, m_read16(s, p88)));
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    set_dp8(s, 0x84, m_lda8(s, 0xCC));
    set_dp8(s, 0xAF, m_lda8(s, 0x7E));
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);

    for (;;) {
        /* $C1:1CE7: next row */
        m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
        m_ldx16(s, dp16(s, 0x88));
        m_lda16(s, m_read16(s, 0xCCF941 + c->x));
        c->a = m_adc16(s, c->a, m_read16(s, long_ptr(s, 0x82)));
        m_tax(s);
        for (int i = 0; i < 2; i++) {
            uint16_t v = (uint16_t)(dp16(s, 0x82) + 1);
            set_dp16(s, 0x82, v);
            m_set_nz16(s, v);
        }
        for (;;) {
            /* $C1:1CF6: next tile */
            m_rep(s, CPU_FLAG_M);
            uint16_t tile = m_lda16(s, m_read16(s, long_ptr(s, 0xBA)));
            set_dp16(s, 0x8C, tile);
            uint16_t number = m_lda16(s, tile & 0x07FF);
            if (number == 0) {
                for (uint32_t k = 0; k < 32; k += 2)
                    m_write16(s, 0x7E0000 + c->x + k, number);
                m_txa(s);
                m_set_flag(s, CPU_FLAG_C, false);
                c->a = m_adc16(s, c->a, 0x0020);
                m_tax(s);
            } else {
                for (int i = 0; i < 5; i++)
                    number = m_asl16(s, number);
                m_set_flag(s, CPU_FLAG_C, false);
                c->a = m_adc16(s, number, dp16(s, 0xBD));
                m_set_index(s, &c->y, c->a);
                if (m_lda16(s, (uint16_t)(dp16(s, 0x8C) & 0x4000)) == 0) {
                    for (uint32_t k = 0; k < 32; k += 2) {
                        uint16_t w = m_lda16(s, m_read16(s, m_abs(s, 0x0000) + c->y + k));
                        m_write16(s, 0x7E0000 + c->x + k, w);
                    }
                    m_txa(s);
                    m_set_flag(s, CPU_FLAG_C, false);
                    c->a = m_adc16(s, c->a, 0x0020);
                    m_tax(s);
                } else {
                    /* $C1:1DD3: flipped */
                    set_dp16(s, 0x8C, c->y);
                    set_dp16(s, 0xAD, c->x);
                    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
                    m_set_index(s, &c->y, 0x00);
                    uint32_t src = ((uint32_t)c->db << 16) + dp16(s, 0x8C);
                    uint32_t dst = long_ptr(s, 0xAD);
                    for (int k = 0; k < 32; k++) {
                        m_lda8(s, m_read8(s, src + c->y));
                        m_tax(s);
                        uint8_t v = m_lda8(s, m_read8(s, 0xC0FD00 + c->x));
                        m_write8(s, dst + c->y, v);
                        m_iny(s);
                    }
                    m_rep(s, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_C);
                    m_lda16(s, dp16(s, 0xAD));
                    c->a = m_adc16(s, c->a, 0x0020);
                    m_tax(s);
                }
            }
            /* $C1:1F23 */
            for (int i = 0; i < 2; i++) {
                uint16_t v = (uint16_t)(dp16(s, 0xBA) + 1);
                set_dp16(s, 0xBA, v);
                m_set_nz16(s, v);
            }
            m_tdc(s);
            m_sep(s, CPU_FLAG_M);
            if (dec_dp8(s, 0x80) == 0)
                break;
        }
        if (dec_dp8(s, 0x85) == 0)
            break;
        set_dp8(s, 0x80, m_lda8(s, dp8(s, 0x81)));
    }

    /* $C1:1F3C: part offsets */
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_lda8(s, abs8(s, 0x9679));
    m_tax(s);
    m_lda8(s, absx8(s, 0x967C));
    m_tax(s);
    m_lda8(s, m_read8(s, 0xCCF4FC + c->x));
    m_tax(s);
    set_dp16(s, 0x80, c->x);
    m_ldx16(s, dp16(s, 0x88));
    set_dp8(s, 0x82, m_lda8(s, m_read8(s, 0xCCF917 + c->x)));
    set_dp8(s, 0x83, m_lda8(s, m_read8(s, 0xCCF918 + c->x)));
    m_ldx16(s, dp16(s, 0x82));
    m_set_index(s, &c->y, 0x0000);
    do {
        uint8_t v = m_lda8(s, m_read8(s, long_ptr(s, 0xBA) + c->y));
        set_absx8(s, 0x9F47, v);
        if (v & 0x80)
            m_lda8(s, 0xFF);
        else
            m_tdc(s);
        set_absx8(s, 0x9F48, (uint8_t)c->a);
        m_inx(s);
        m_inx(s);
        m_iny(s);
        m_cmp16(s, c->y, dp16(s, 0x80));
    } while (c->y != dp16(s, 0x80));
    uint8_t count = (uint8_t)(abs8(s, 0x9912) + 1);
    m_write8(s, m_abs(s, 0x9912), count);
    m_set_nz8(s, count);
    m_rts(s);
}

// $C1:1C4A
/* battle_build_object_graphics, except for objects of graphics kind 3. Stops
 * where it falls through into $C1:1C55, which has its own entry. */
void battle_build_object_graphics_checked(Snes *s)
{
    m_ldx16(s, m_read16(s, m_abs(s, 0x9679)));
    uint8_t kind = m_lda8(s, absx8(s, 0x967C));
    m_cmp8(s, kind, 0x03);
    if (kind == 0x03) {
        m_rts(s);
        return;
    }
    m_stop_at(s, 0xC11C55, 0);
}

// $CC:F1E7
/* Step the four object palette animations: when slot k is active ($A3B9)
 * and its delay ($A3C1, reload $A3C5) runs out, advance its frame ($A3BD,
 * from $A3C9 up to $A3CD, wrapping) and copy that frame's colors into sprite
 * palette row ($CD:0000 table through $9882: color set, then row and flags) at
 * $2302 + row * 8: 6 bytes from $D1:4100 + set * 6, or with the flag's bit 7
 * 14 bytes from $D1:4400 + set * 14 (via the multiply at $CC:F365). */
void battle_step_object_palettes(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    m_write16(s, m_dp(s, 0x80), c->x);
    uint8_t slot;
    do {
        m_ldx16(s, m_read16(s, m_dp(s, 0x80)));
        if (m_lda8(s, absx8(s, 0xA3B9)) != 0) {
            uint8_t delay = (uint8_t)(absx8(s, 0xA3C1) - 1);
            set_absx8(s, 0xA3C1, delay);
            m_set_nz8(s, delay);
            if (delay == 0) {
                set_absx8(s, 0xA3C1, m_lda8(s, absx8(s, 0xA3C5)));
                uint8_t step = (uint8_t)(absx8(s, 0xA3BD) + 1);
                set_absx8(s, 0xA3BD, step);
                m_set_nz8(s, step);
                m_set_flag(s, CPU_FLAG_C, false);
                uint8_t frame = m_adc8(s, m_lda8(s, absx8(s, 0xA3C9)), absx8(s, 0xA3BD));
                m_set_a8(s, frame);
                uint8_t last = absx8(s, 0xA3CD);
                m_cmp8(s, frame, last);
                if (frame > last) {
                    set_absx8(s, 0xA3BD, 0x00);
                    frame = m_lda8(s, absx8(s, 0xA3C9));
                }
                m_set_a8(s, m_asl8(s, frame));
                m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
                c->a = m_adc16(s, c->a, m_read16(s, m_abs(s, 0x9882)));
                m_tax(s);
                m_tdc(s);
                m_write16(s, m_dp(s, 0x82), c->a);
                m_sep(s, CPU_FLAG_M);
                set_dp8(s, 0x82, m_lda8(s, m_read8(s, 0xCD0000 + c->x)));
                uint8_t row = m_lda8(s, m_read8(s, 0xCD0001 + c->x));
                set_dp8(s, 0x84, row);
                uint8_t r = m_lda8(s, row & 0x03);
                for (int i = 0; i < 3; i++)
                    r = m_asl8(s, r);
                m_set_a8(s, r);
                m_set_index(s, &c->y, c->a);
                bool wide = m_lda8(s, dp8(s, 0x84)) & 0x80;
                set_dp8(s, 0xAD, m_lda8(s, wide ? 0x0E : 0x06));
                set_dp8(s, 0xAE, m_lda8(s, dp8(s, 0x82)));
                /* JSR $F365: $AF = $AD * $AE */
                m_push16(s, wide ? 0xF25C : 0xF240);
                uint8_t a = m_lda8(s, dp8(s, 0xAD));
                set_dp8(s, 0x77, a);
                m_write8(s, 0x004202, a);
                uint8_t b = m_lda8(s, dp8(s, 0xAE));
                set_dp8(s, 0x78, b);
                m_write8(s, 0x004203, b);
                m_rep(s, CPU_FLAG_M);
                m_write16(s, m_dp(s, 0xAF), m_lda16(s, m_read16(s, 0x004216)));
                m_tdc(s);
                m_sep(s, CPU_FLAG_M);
                m_pull16(s);
                m_ldx16(s, m_read16(s, m_dp(s, 0xAF)));
                uint32_t colors = wide ? 0xD14400 : 0xD14100;
                uint8_t n;
                do {
                    set_absy8(s, 0x2302, m_lda8(s, m_read8(s, colors + c->x)));
                    m_inx(s);
                    m_iny(s);
                    n = (uint8_t)(dp8(s, 0xAD) - 1);
                    set_dp8(s, 0xAD, n);
                    m_set_nz8(s, n);
                } while (n != 0);
            }
        }
        slot = (uint8_t)(dp8(s, 0x80) + 1);
        set_dp8(s, 0x80, slot);
        m_set_nz8(s, slot);
        slot = m_lda8(s, dp8(s, 0x80));
        m_cmp8(s, slot, 0x04);
    } while (slot != 0x04);
    m_rtl(s);
}

/* INX x4 */
static void inx4(Snes *s)
{
    for (int i = 0; i < 4; i++)
        m_inx(s);
}

/* INC on a direct-page byte, setting N and Z; returns the new value. */
static uint8_t inc_dp8(Snes *s, uint8_t cell)
{
    uint8_t v = (uint8_t)(dp8(s, cell) + 1);
    set_dp8(s, cell, v);
    m_set_nz8(s, v);
    return v;
}

// $C1:48EC
/* Index the 4-byte records at $CE:0000 + $9885 into 16 groups: group k
 * starts at $A280 + k * 2, and $A2A0 + k gets its record count. By a record's
 * top three bits: none, it counts toward the group; bit 6 (without bit 7), the
 * count so far including it is stored and counting restarts; bit 7, the next
 * group starts after it; bit 5 alone, the count is stored as 0 and the next
 * group starts. */
void battle_index_record_groups(Snes *s)
{
    Cpu *c = &s->cpu;
    m_ldx16(s, m_read16(s, m_abs(s, 0x9885)));
    m_write16(s, m_abs(s, 0xA280), c->x);
    m_tdc(s);
    m_set_index(s, &c->y, c->a);
    set_dp16(s, 0x80, c->y);
    set_dp16(s, 0x82, c->y);
    for (;;) {
        /* $C1:48F8 */
        uint8_t kind = m_lda8(s, (uint8_t)(m_lda8(s, m_read8(s, 0xCE0000 + c->x)) & 0xE0));
        if (kind == 0) {
            inx4(s);
            inc_dp8(s, 0x80);
            continue;
        }
        if (!(kind & 0x80)) {
            if (m_lda8(s, kind & 0x40) != 0) {
                inx4(s);
                inc_dp8(s, 0x80);
                m_lda8(s, dp8(s, 0x82));
                m_set_index(s, &c->y, c->a);
                m_write8(s, m_abs(s, 0xA2A0) + c->y, m_lda8(s, dp8(s, 0x80)));
                set_dp8(s, 0x80, 0x00);
                continue;
            }
            m_lda8(s, dp8(s, 0x82));
            m_set_index(s, &c->y, c->a);
            m_tdc(s);
            m_write8(s, m_abs(s, 0xA2A0) + c->y, (uint8_t)c->a);
        }
        /* $C1:4921: next group */
        inx4(s);
        uint8_t group = inc_dp8(s, 0x82);
        group = m_lda8(s, dp8(s, 0x82));
        m_cmp8(s, group, 0x10);
        if (group == 0x10)
            break;
        m_set_a8(s, m_asl8(s, group));
        m_set_index(s, &c->y, c->a);
        m_rep(s, CPU_FLAG_M);
        m_txa(s);
        m_write16(s, m_abs(s, 0xA280) + c->y, c->a);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        set_dp8(s, 0x80, 0x00);
        inx4(s);
        inc_dp8(s, 0x80);
    }
    m_rts(s);
}

// $C1:4943
/* After waiting a frame ($C1:007E) and running $C1:10FA, build sprite entries
 * for up to four shapes from the stream at $2D00 + $A652: a header byte
 * (width in the high nibble, row count in the low one, 0 ends the stream), a
 * bitmask per row, then 2 bytes (tile, attributes) for each set bit. Each set
 * bit at row r, column c adds a 4-byte entry at $4500 + $CC:F7C0[shape]: X and
 * Y from $CC:F820 by column and row, the tile, and the attributes ORed with
 * $A5E3; $A05B + shape counts them. */
void battle_build_bitmap_sprites(Snes *s)
{
    Cpu *c = &s->cpu;
    if (!call(s, 0xC14943, 0xC1007E) || !call(s, 0xC14946, 0xC110FA))
        return;
    set_dp8(s, 0x8C, 0x00);
    for (;;) {
        /* $C1:494B */
        m_rep(s, CPU_FLAG_M);
        m_lda16(s, m_read16(s, m_abs(s, 0xA650)));
        c->a = m_asl16(s, c->a);
        m_tax(s);
        set_dp16(s, 0x84, m_lda16(s, m_read16(s, 0xCCF7C0 + c->x)));
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        m_ldx16(s, m_read16(s, m_abs(s, 0xA650)));
        set_absx8(s, 0xA05B, 0x00);
        m_ldx16(s, m_read16(s, m_abs(s, 0xA652)));
        uint8_t header = m_lda8(s, absx8(s, 0x2D00));
        if (header == 0)
            break;
        uint8_t rows = m_lda8(s, header & 0x0F);
        set_dp8(s, 0x81, rows);
        set_dp8(s, 0x82, rows);
        uint8_t width = m_lda8(s, absx8(s, 0x2D00));
        for (int i = 0; i < 4; i++)
            width = m_lsr8(s, width);
        set_dp8(s, 0x80, width);
        m_inx(s);
        m_tdc(s);
        m_set_index(s, &c->y, c->a);
        uint8_t n;
        do {
            set_absy8(s, 0xA2D3, m_lda8(s, absx8(s, 0x2D00)));
            m_inx(s);
            m_iny(s);
            n = (uint8_t)(dp8(s, 0x82) - 1);
            set_dp8(s, 0x82, n);
            m_set_nz8(s, n);
        } while (n != 0);
        m_write16(s, m_abs(s, 0xA652), c->x);
        m_tdc(s);
        m_set_index(s, &c->y, c->a);
        set_dp16(s, 0x82, c->y);
        do {
            /* $C1:4991: row */
            m_tdc(s);
            m_set_index(s, &c->y, c->a);
            set_dp16(s, 0x86, c->y);
            uint8_t col;
            do {
                /* $C1:4995 */
                m_set_index(s, &c->y, dp16(s, 0x82));
                uint8_t mask = m_asl8(s, m_lda8(s, absy8(s, 0xA2D3)));
                m_set_a8(s, mask);
                set_absy8(s, 0xA2D3, mask);
                if (m_flag(s, CPU_FLAG_C)) {
                    m_ldx16(s, m_read16(s, m_abs(s, 0xA652)));
                    m_set_index(s, &c->y, dp16(s, 0x84));
                    set_absy8(s, 0x4502, m_lda8(s, absx8(s, 0x2D00)));
                    uint8_t attr = m_lda8(s, absx8(s, 0x2D01));
                    set_absy8(s, 0x4503, m_lda8(s, (uint8_t)(attr | m_read8(s, m_abs(s, 0xA5E3)))));
                    m_ldx16(s, dp16(s, 0x82));
                    set_absy8(s, 0x4501, m_lda8(s, m_read8(s, 0xCCF820 + c->x)));
                    m_ldx16(s, dp16(s, 0x86));
                    set_absy8(s, 0x4500, m_lda8(s, m_read8(s, 0xCCF820 + c->x)));
                    m_ldx16(s, m_read16(s, m_abs(s, 0xA652)));
                    m_inx(s);
                    m_inx(s);
                    m_write16(s, m_abs(s, 0xA652), c->x);
                    for (int i = 0; i < 4; i++)
                        m_iny(s);
                    set_dp16(s, 0x84, c->y);
                    m_ldx16(s, m_read16(s, m_abs(s, 0xA650)));
                    uint8_t count = (uint8_t)(absx8(s, 0xA05B) + 1);
                    set_absx8(s, 0xA05B, count);
                    m_set_nz8(s, count);
                }
                col = inc_dp8(s, 0x86);
                col = m_lda8(s, dp8(s, 0x86));
                m_cmp8(s, col, 0x08);
            } while (col != 0x08);
            inc_dp8(s, 0x82);
            n = (uint8_t)(dp8(s, 0x81) - 1);
            set_dp8(s, 0x81, n);
            m_set_nz8(s, n);
        } while (n != 0);
        m_ldx16(s, m_read16(s, m_abs(s, 0xA652)));
        m_inx(s);
        m_inx(s);
        m_write16(s, m_abs(s, 0xA652), c->x);
        uint32_t shape = m_abs(s, 0xA650);
        uint8_t v = (uint8_t)(m_read8(s, shape) + 1);
        m_write8(s, shape, v);
        m_set_nz8(s, v);
        inc_dp8(s, 0x8C);
        uint8_t done = m_lda8(s, dp8(s, 0x8C));
        m_cmp8(s, done, 0x04);
        if (done == 0x04)
            break;
    }
    m_rts(s);
}

