/* Field mode: per-frame arithmetic for the three background layers.
 *
 * Positions and offsets live on the field's direct page: $99-$9E hold each
 * layer's position in tiles (X then Y), $7C/$7E/$80 base values added to the
 * tile map offsets computed from them. Tile maps are indexed as row * 32 +
 * column, with columns from $20 in a second half at $0400. */
#include "field/layers.h"

#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }

/* 16-bit accumulator arithmetic as CLC : ADC / SEC : SBC. */
static void clc_adc16(Snes *s, uint16_t v)
{
    m_set_flag(s, CPU_FLAG_C, false);
    s->cpu.a = m_adc16(s, s->cpu.a, v);
}

static void sec_sbc16(Snes *s, uint16_t v)
{
    m_set_flag(s, CPU_FLAG_C, true);
    s->cpu.a = m_sbc16(s, s->cpu.a, v);
}

static void tay16(Snes *s) { m_set_index(s, &s->cpu.y, s->cpu.a); }
static void tya16(Snes *s) { m_lda16(s, s->cpu.y); }

/* ASL on a 16-bit direct-page word. */
static void asl_dp16(Snes *s, uint8_t cell)
{
    set_dp16(s, cell, m_asl16(s, dp16(s, cell)));
}

/* STA $62 : TYA : ASL A x5 : STA $52 : LDA $62 : CMP #$0020 -- the start of
 * each helper: row Y * 32 into $52, the column (A) into $62. Returns true if
 * the column is in the second half ($20 or more). */
static bool helper_start(Snes *s, bool save_row)
{
    set_dp16(s, 0x62, s->cpu.a);
    tya16(s);
    if (save_row)
        set_dp16(s, 0x64, s->cpu.a);
    for (int i = 0; i < 5; i++)
        s->cpu.a = m_asl16(s, s->cpu.a);
    set_dp16(s, 0x52, s->cpu.a);
    m_lda16(s, dp16(s, 0x62));
    m_cmp16(s, s->cpu.a, 0x0020);
    return m_flag(s, CPU_FLAG_C);
}

/* SEC : SBC #$0020 : CLC : ADC $52 : CLC : ADC #$0400 -- the second half's
 * index of the column in A on row $52. */
static void second_half_index(Snes *s)
{
    sec_sbc16(s, 0x0020);
    clc_adc16(s, dp16(s, 0x52));
    clc_adc16(s, 0x0400);
}

/* $C0:813D: X = the index of column A on row Y, Y = the matching index in
 * the other half's row, $6C = the columns left to the half's end, doubled,
 * $6E = the rest of 34 columns, doubled; with 33 or more ($6A = 2) the rest
 * is 32 columns and $5E = X - 31. */
static void edge_left(Snes *s)
{
    Cpu *c = &s->cpu;
    if (!helper_start(s, false)) {
        clc_adc16(s, dp16(s, 0x52));
        m_tax(s);
        m_lda16(s, dp16(s, 0x52));
        clc_adc16(s, 0x0400);
        tay16(s);
        m_lda16(s, 0x0020);
    } else {
        second_half_index(s);
        m_tax(s);
        m_ldy(s, dp16(s, 0x52));
        m_lda16(s, 0x0040);
    }
    sec_sbc16(s, dp16(s, 0x62));
    set_dp16(s, 0x6C, c->a);
    /* $C0:8179 */
    m_lda16(s, 0x0022);
    sec_sbc16(s, dp16(s, 0x6C));
    m_cmp16(s, c->a, 0x0021);
    if (m_flag(s, CPU_FLAG_C)) {
        m_lda16(s, 0x0020);
        c->a = m_asl16(s, c->a);
        set_dp16(s, 0x6E, c->a);
        asl_dp16(s, 0x6C);
        set_dp16(s, 0x6A, m_lda16(s, 0x0002));
        m_txa(s);
        sec_sbc16(s, 0x001F);
        set_dp16(s, 0x5E, c->a);
        return;
    }
    c->a = m_asl16(s, c->a);
    set_dp16(s, 0x6E, c->a);
    asl_dp16(s, 0x6C);
    set_dp16(s, 0x6A, 0x0000);
}

/* $C0:81A1: X = the index of column A on row Y, Y = X - 31, A = the row's
 * start in the other half. */
static void edge_right(Snes *s)
{
    if (!helper_start(s, false)) {
        clc_adc16(s, dp16(s, 0x52));
        m_tax(s);
        sec_sbc16(s, 0x001F);
        tay16(s);
        m_lda16(s, dp16(s, 0x52));
        clc_adc16(s, 0x0400);
        return;
    }
    second_half_index(s);
    m_tax(s);
    sec_sbc16(s, 0x001F);
    tay16(s);
    m_lda16(s, dp16(s, 0x52));
}

/* $C0:81D6: X = the index of column A on row Y, Y = the column (plus $03E0 in
 * the second half), $6C = (32 - row) doubled, $6E = the row doubled. */
static void edge_top(Snes *s)
{
    Cpu *c = &s->cpu;
    if (!helper_start(s, true)) {
        clc_adc16(s, dp16(s, 0x52));
        m_tax(s);
        m_ldy(s, dp16(s, 0x62));
    } else {
        second_half_index(s);
        m_tax(s);
        m_lda16(s, dp16(s, 0x62));
        clc_adc16(s, 0x03E0);
        tay16(s);
    }
    /* $C0:8204 */
    m_lda16(s, 0x0020);
    sec_sbc16(s, dp16(s, 0x64));
    set_dp16(s, 0x6C, c->a);
    m_lda16(s, 0x0020);
    sec_sbc16(s, dp16(s, 0x6C));
    c->a = m_asl16(s, c->a);
    set_dp16(s, 0x6E, c->a);
    asl_dp16(s, 0x6C);
}

/* $C0:8218: X = the index of column A on row Y, A = the column (plus $03E0 in
 * the second half). */
static void edge_bottom(Snes *s)
{
    if (!helper_start(s, false)) {
        clc_adc16(s, dp16(s, 0x52));
        m_tax(s);
        m_lda16(s, dp16(s, 0x62));
        return;
    }
    second_half_index(s);
    m_tax(s);
    m_lda16(s, dp16(s, 0x62));
    clc_adc16(s, 0x03E0);
}

/* JSR to one of the helpers above, inlined with its stack bytes. */
static void local_call(Snes *s, uint32_t site, void (*helper)(Snes *))
{
    m_push16(s, (uint16_t)(site + 2));
    helper(s);
    m_pull16(s);
}

/* CLC : ADC base : STA cell */
static void store_offset(Snes *s, uint8_t base, uint8_t cell)
{
    clc_adc16(s, dp16(s, base));
    set_dp16(s, cell, s->cpu.a);
}

/* LDA cell : DEC A (optional) or CLC : ADC add : AND mask */
static void masked(Snes *s, uint8_t cell, int add, uint16_t mask)
{
    m_lda16(s, dp16(s, cell));
    if (add < 0)
        m_lda16(s, (uint16_t)(s->cpu.a - 1));
    else if (add > 0)
        clc_adc16(s, (uint16_t)add);
    m_lda16(s, (uint16_t)(s->cpu.a & mask));
}

// $C0:7F9A
/* For each of the three layers (position $99/$9A, $9B/$9C, $9D/$9E), the tile
 * map indexes of the screen's edges around its position -- left column,
 * right column, top row and bottom row, through the local helpers $C0:813D,
 * $C0:81A1, $C0:81D6 and $C0:8218 (inlined with their stack bytes) -- plus
 * the layer's base ($7C/$7E/$80), into $9F-$B2, $B3-$C6 and $C7-$DA, and
 * the helpers' counts ($6C, $6E, $6A) into $DB-$E4, $E5-$EE and $EF-$F8. */
void field_compute_layer_edges(Snes *s)
{
    Cpu *c = &s->cpu;
    m_rep(s, CPU_FLAG_M);
    for (int layer = 0; layer < 3; layer++) {
        const uint8_t px = (uint8_t)(0x99 + 2 * layer), py = (uint8_t)(0x9A + 2 * layer);
        const uint8_t base = (uint8_t)(0x7C + 2 * layer), out = (uint8_t)(0x9F + 0x14 * layer);
        const uint8_t counts = (uint8_t)(0xDB + 0x0A * layer);
        const uint32_t site = 0xC07FA9u + 0x8Au * (uint32_t)layer;

        masked(s, py, 0, 0x001F);
        tay16(s);
        masked(s, px, -1, 0x003F);
        m_push16(s, c->a);
        local_call(s, site, edge_left);
        m_txa(s);
        store_offset(s, base, out);
        tya16(s);
        store_offset(s, base, (uint8_t)(out + 2));
        m_lda16(s, dp16(s, 0x5E));
        store_offset(s, base, (uint8_t)(out + 4));
        set_dp16(s, counts, m_lda16(s, dp16(s, 0x6C)));
        set_dp16(s, (uint8_t)(counts + 2), m_lda16(s, dp16(s, 0x6E)));
        set_dp16(s, (uint8_t)(counts + 4), m_lda16(s, dp16(s, 0x6A)));

        masked(s, py, 0x1C, 0x001F);
        tay16(s);
        m_lda16(s, m_pull16(s));
        local_call(s, site + 0x2D, edge_right);
        store_offset(s, base, (uint8_t)(out + 8));
        m_txa(s);
        store_offset(s, base, (uint8_t)(out + 6));
        tya16(s);
        store_offset(s, base, (uint8_t)(out + 10));

        masked(s, py, -1, 0x001F);
        m_push16(s, c->a);
        tay16(s);
        masked(s, px, 0, 0x003F);
        local_call(s, site + 0x4E, edge_top);
        m_txa(s);
        store_offset(s, base, (uint8_t)(out + 12));
        tya16(s);
        store_offset(s, base, (uint8_t)(out + 14));
        set_dp16(s, (uint8_t)(counts + 6), m_lda16(s, dp16(s, 0x6C)));
        set_dp16(s, (uint8_t)(counts + 8), m_lda16(s, dp16(s, 0x6E)));

        m_ldy(s, m_pull16(s));
        masked(s, px, 0x20, 0x003F);
        local_call(s, site + 0x6F, edge_bottom);
        store_offset(s, base, (uint8_t)(out + 18));
        m_txa(s);
        store_offset(s, base, (uint8_t)(out + 16));
    }
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

/* 8-bit accumulator: CLC : ADC dp */
static uint8_t clc_adc_dp(Snes *s, uint8_t cell)
{
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_a8(s, m_adc8(s, (uint8_t)s->cpu.a, dp8(s, cell)));
    return (uint8_t)s->cpu.a;
}

/* ASL A (8-bit) n times. */
static void asl_a(Snes *s, int n)
{
    for (int i = 0; i < n; i++)
        m_set_a8(s, m_asl8(s, (uint8_t)s->cpu.a));
}

/* LSR A (8-bit) n times. */
static void lsr_a(Snes *s, int n)
{
    for (int i = 0; i < n; i++)
        m_set_a8(s, m_lsr8(s, (uint8_t)s->cpu.a));
}

/* EOR #$FF : INC A */
static void negate_a(Snes *s)
{
    m_lda8(s, (uint8_t)~s->cpu.a);
    m_lda8(s, (uint8_t)(s->cpu.a + 1));
}

/* LDA cells[2] : CLC : ADC cells[3] ... : STA cells[0] (cells[1] cells in all). */
static void add_cells(Snes *s, const uint8_t cells[7])
{
    m_lda8(s, dp8(s, cells[2]));
    for (int i = 1; i < cells[1]; i++)
        clc_adc_dp(s, cells[2 + i]);
    set_dp8(s, cells[0], (uint8_t)s->cpu.a);
}

/* One axis of the scroll target check at $C0:91B0: compare `pos` with
 * `target`; the step `step` becomes $10 below it, $F0 above, and on it $F0
 * until the fine position `fine` is aligned, then `bit` is set in $01.
 * Returns true if the bit was set. */
static bool approach(Snes *s, uint8_t pos, uint8_t target, uint8_t fine, uint8_t step, uint8_t bit)
{
    uint8_t v = m_lda8(s, dp8(s, pos));
    uint8_t t = dp8(s, target);
    m_cmp8(s, v, t);
    if (v == t) {
        if (m_lda8(s, (uint8_t)(dp8(s, fine) & 0x0F)) == 0) {
            m_lda8(s, bit);
            m_tsb8(s, m_dp(s, 0x01));
            return true;
        }
        set_dp8(s, step, m_lda8(s, 0xF0));
        return false;
    }
    set_dp8(s, step, m_lda8(s, v > t ? 0x10 : 0xF0));
    return false;
}

/* One of the offsets at $C0:91FD: (bits of $0400 & mask) shifted left
 * `shift` times, negated when `negate`. */
static void bit_offset(Snes *s, uint8_t mask, int shift, bool negate, uint8_t cell)
{
    m_lda8(s, (uint8_t)(m_read8(s, m_abs(s, 0x0400)) & mask));
    asl_a(s, shift);
    if (negate)
        negate_a(s);
    set_dp8(s, cell, (uint8_t)s->cpu.a);
}

// $C0:91AC
/* Per-frame scroll steps for the three layers. While $00 is set, the steps
 * $2E (X) and $30 (Y) are $10 or $F0 toward the target tile in $0A/$0E from
 * $02/$03, and on it $F0 until the fine position ($87/$89) is aligned, when
 * bit 0 or 1 of $01 is set; with both, $00 becomes 2 and $01 is cleared.
 * With $01BC set, offsets from $0400's bits 2 and 1 go to $4E and $4F
 * (negated when bit 0 is set, zero on an axis whose target is 0). Each
 * layer's 4.4 fixed-point accumulators ($3B-$40) then add the steps and
 * offsets (for layers 2 and 3 only while bit 1 or 2 of $36 is clear), the
 * speeds $37-$3A of layers 2 and 3 and the extra speeds $24-$29; the whole
 * pixels go to the positive or negative move counts ($41-$4C) and the
 * fractions stay, sign-extended. When the countdown $4D runs out, the extra
 * speeds are cleared. */
void field_update_layer_scroll(Snes *s)
{
    if (m_lda8(s, dp8(s, 0x00)) != 0) {
        approach(s, 0x02, 0x0A, 0x87, 0x2E, 0x01);
        if (approach(s, 0x03, 0x0E, 0x89, 0x30, 0x02)) {
            uint8_t both = m_lda8(s, dp8(s, 0x01));
            m_cmp8(s, both, 0x03);
            if (both == 0x03) {
                set_dp8(s, 0x00, m_lda8(s, 0x02));
                set_dp8(s, 0x01, 0x00);
            }
        }
    }
    /* $C0:91F8 */
    if (m_lda8(s, m_read8(s, m_abs(s, 0x01BC))) != 0) {
        uint8_t bits = m_lda8(s, m_read8(s, m_abs(s, 0x0400)));
        m_bit_imm8(s, 0x01);
        bool negative = bits & 0x01;
        /* the first read of $0400 is reused for $4E */
        m_lda8(s, (uint8_t)(bits & 0x04));
        asl_a(s, 2);
        if (negative)
            negate_a(s);
        set_dp8(s, 0x4E, (uint8_t)s->cpu.a);
        bit_offset(s, 0x02, 3, negative, 0x4F);
        if (m_lda8(s, dp8(s, 0x0A)) == 0)
            set_dp8(s, 0x4E, 0x00);
        if (m_lda8(s, dp8(s, 0x0E)) == 0)
            set_dp8(s, 0x4F, 0x00);
    }
    /* $C0:9238 */
    static const uint8_t sums[10][7] = {
        /* destination, count, cells */
        { 0x3B, 4, 0x2E, 0x3B, 0x24, 0x4E }, { 0x3C, 4, 0x30, 0x3C, 0x25, 0x4F },
        { 0x3D, 3, 0x37, 0x3D, 0x26 }, { 0x3E, 3, 0x38, 0x3E, 0x27 },
        { 0x3D, 5, 0x2E, 0x37, 0x3D, 0x26, 0x4E }, { 0x3E, 5, 0x30, 0x38, 0x3E, 0x27, 0x4F },
        { 0x3F, 3, 0x39, 0x3F, 0x28 }, { 0x40, 3, 0x3A, 0x40, 0x29 },
        { 0x3F, 5, 0x2E, 0x39, 0x3F, 0x28, 0x4E }, { 0x40, 5, 0x30, 0x3A, 0x40, 0x29, 0x4F },
    };
    add_cells(s, sums[0]);
    add_cells(s, sums[1]);
    for (int layer = 0; layer < 2; layer++) {
        m_lda8(s, dp8(s, 0x36));
        m_bit_imm8(s, layer == 0 ? 0x02 : 0x04);
        const int row = m_flag(s, CPU_FLAG_Z) ? 4 + 4 * layer : 2 + 4 * layer;
        add_cells(s, sums[row]);
        add_cells(s, sums[row + 1]);
    }
    /* $C0:92CA */
    for (uint8_t k = 0; k < 6; k++) {
        const uint8_t acc = (uint8_t)(0x3B + k), positive = (uint8_t)(0x42 + 2 * k), negative = (uint8_t)(0x41 + 2 * k);
        uint8_t v = m_lda8(s, dp8(s, acc));
        if (!(v & 0x80)) {
            lsr_a(s, 4);
            set_dp8(s, positive, (uint8_t)s->cpu.a);
            set_dp8(s, negative, 0x00);
        } else {
            negate_a(s);
            lsr_a(s, 4);
            set_dp8(s, negative, (uint8_t)s->cpu.a);
            set_dp8(s, positive, 0x00);
        }
        if (m_lda8(s, (uint8_t)(dp8(s, acc) & 0x0F)) != 0) {
            v = m_lda8(s, dp8(s, acc));
            m_lda8(s, (v & 0x80) ? (uint8_t)(v | 0xF0) : (uint8_t)(v & 0x0F));
        }
        set_dp8(s, acc, (uint8_t)s->cpu.a);
    }
    /* $C0:93CC */
    if (m_lda8(s, dp8(s, 0x4D)) != 0) {
        uint8_t n = (uint8_t)(dp8(s, 0x4D) - 1);
        set_dp8(s, 0x4D, n);
        m_set_nz8(s, n);
        if (n == 0) {
            for (uint8_t cell = 0x24; cell <= 0x29; cell++)
                set_dp8(s, cell, 0x00);
        }
    }
    m_rts(s);
}

/* LDA rows : REP #$20 : XBA : AND #$FF00 : STA $52 : SEP #$20 : LDA width :
 * REP #$20 : AND #$00FF : STA $54 : SEP #$20 -- a tile map's size, the row
 * count in $53 and the width in $54 (row counter $55 cleared). */
static void map_size(Snes *s, uint16_t rows, uint16_t width)
{
    Cpu *c = &s->cpu;
    m_lda8(s, m_read8(s, m_abs(s, rows)));
    m_rep(s, CPU_FLAG_M);
    m_xba(s);
    set_dp16(s, 0x52, m_lda16(s, (uint16_t)(c->a & 0xFF00)));
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, m_read8(s, m_abs(s, width)));
    m_rep(s, CPU_FLAG_M);
    set_dp16(s, 0x54, m_lda16(s, (uint16_t)(c->a & 0x00FF)));
    m_sep(s, CPU_FLAG_M);
}

/* LDA $2180 -- the next byte from the WRAM data port. */
static uint8_t port_byte(Snes *s) { return m_lda8(s, m_read8(s, m_abs(s, 0x2180))); }

/* INX : CPX $54 -- true at the end of the row. */
static bool next_column(Snes *s)
{
    m_inx(s);
    m_cmp16(s, s->cpu.x, dp16(s, 0x54));
    return m_flag(s, CPU_FLAG_C);
}

/* LDA $55 : INC A : STA $55 : XBA : LDA #$00 : TAX : CPX $52 -- X to the
 * start of the next row; true after the last one. */
static bool next_row(Snes *s)
{
    uint8_t row = m_lda8(s, dp8(s, 0x55));
    set_dp8(s, 0x55, m_lda8(s, (uint8_t)(row + 1)));
    m_xba(s);
    m_lda8(s, 0x00);
    m_tax(s);
    m_cmp16(s, s->cpu.x, dp16(s, 0x52));
    return m_flag(s, CPU_FLAG_C);
}

/* One byte per tile from the port into `dest` + row * 256 + column. */
static void unpack_map(Snes *s, uint32_t dest)
{
    m_ldx(s, 0x0000);
    do {
        do
            m_write8(s, dest + s->cpu.x, port_byte(s));
        while (!next_column(s));
    } while (!next_row(s));
}

// $C0:A521
/* Unpack a map's tile layers from the decompressed data at $7E:B506, read
 * through the WRAM data port: layer maps into $7E:3000 and $7E:3040 (sized
 * by $0BCB/$0BCD and $0BCF/$0BD1), a third into $7E:3080 when $0BCA bit 0 is
 * set ($0BD3/$0BD5), then per tile three bytes into $7E:30C0, $7E:7000 and
 * $7E:7040 (the map attributes), where a first byte with bit 7 set starts a
 * run: its three bytes (bit 7 cleared, kept in $58/$5A/$5C) repeat for the
 * count in the next byte (0 meaning 256). Rows are 256 apart. */
void field_unpack_tile_maps(Snes *s)
{
    Cpu *c = &s->cpu;
    m_write16(s, m_abs(s, 0x2181), m_ldx(s, 0xB506));
    m_write8(s, m_abs(s, 0x2183), m_lda8(s, 0x00));
    map_size(s, 0x0BCD, 0x0BCB);
    unpack_map(s, 0x7E3000);
    map_size(s, 0x0BD1, 0x0BCF);
    unpack_map(s, 0x7E3040);
    if (m_lda8(s, (uint8_t)(m_read8(s, m_abs(s, 0x0BCA)) & 0x01)) != 0) {
        map_size(s, 0x0BD5, 0x0BD3);
        unpack_map(s, 0x7E3080);
    }
    /* $C0:A5D2 */
    map_size(s, 0x0BCD, 0x0BCB);
    m_ldx(s, 0x0000);
    m_tdc(s);
    m_xba(s);
    for (;;) {
        uint8_t first = port_byte(s);
        if (!(first & 0x80)) {
            m_write8(s, 0x7E30C0u + c->x, first);
            m_write8(s, 0x7E7000u + c->x, port_byte(s));
            m_write8(s, 0x7E7040u + c->x, port_byte(s));
        } else {
            /* $C0:A61A */
            uint8_t v = m_lda8(s, first & 0x7F);
            set_dp8(s, 0x58, v);
            m_write8(s, 0x7E30C0u + c->x, v);
            set_dp8(s, 0x5A, v = port_byte(s));
            m_write8(s, 0x7E7000u + c->x, v);
            set_dp8(s, 0x5C, v = port_byte(s));
            m_write8(s, 0x7E7040u + c->x, v);
            m_tdc(s);
            m_xba(s);
            if (port_byte(s) != 0)
                m_set_index(s, &c->y, c->a);
            else
                m_ldy(s, 0x0100);
            for (;;) {
                /* $C0:A641 */
                if (m_ldy(s, (uint16_t)(c->y - 1)) == 0)
                    break;
                if (next_column(s) && next_row(s)) {
                    m_rts(s);
                    return;
                }
                m_write8(s, 0x7E30C0u + c->x, m_lda8(s, dp8(s, 0x58)));
                m_write8(s, 0x7E7000u + c->x, m_lda8(s, dp8(s, 0x5A)));
                m_write8(s, 0x7E7040u + c->x, m_lda8(s, dp8(s, 0x5C)));
            }
        }
        /* $C0:A607 */
        if (next_column(s) && next_row(s)) {
            m_rts(s);
            return;
        }
    }
}
