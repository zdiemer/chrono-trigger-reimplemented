/* Menu: drawing text and numbers into tilemap buffers.
 *
 * Strings are bytes read through the data bank: 0 ends the string, $01-$0F
 * are control codes (a handler table at $C2:EFD4), and every other byte is a
 * tile written as a word, tile then the attribute byte in $7E, into bank $7E
 * at X. The count in $87 limits how many tiles a string may write, and bit 7 of
 * $88 turns off the blank tile ($FF) otherwise written one row above ($40
 * bytes back) for tile $FF. Numbers are converted to binary-coded decimal by
 * adding table entries for each set bit, then written as digit tiles ($D4-$DD). */
#include "menu/text.h"

#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }

/* $offset,Y and $offset,X in the data bank. */
static uint32_t abs_y(Snes *s, uint16_t offset) { return m_abs(s, offset) + s->cpu.y; }
static uint32_t abs_x(Snes *s, uint16_t offset) { return m_abs(s, offset) + s->cpu.x; }

/* PHX/PHY/PLX/PLY at the current index width. */
static void push_index(Snes *s, uint16_t v)
{
    if (m_x8(s))
        m_push8(s, (uint8_t)v);
    else
        m_push16(s, v);
}

static uint16_t pull_index(Snes *s, uint16_t *reg)
{
    return m_set_index(s, reg, m_x8(s) ? m_pull8(s) : m_pull16(s));
}

/* PLA at the current accumulator width. */
static void pla(Snes *s)
{
    if (m_m8(s))
        m_lda8(s, m_pull8(s));
    else
        m_lda16(s, m_pull16(s));
}

static void plb(Snes *s)
{
    s->cpu.db = m_pull8(s);
    m_set_nz8(s, s->cpu.db);
}

static void plp(Snes *s)
{
    s->cpu.p = m_pull8(s);
    if (s->cpu.p & CPU_FLAG_X) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
}

/* LDA $offset,Y at the current accumulator width. */
static uint16_t lda_abs_y(Snes *s, uint16_t offset)
{
    if (m_m8(s))
        return m_lda8(s, m_read8(s, abs_y(s, offset)));
    return m_lda16(s, m_read16(s, abs_y(s, offset)));
}

/* BIT on an 8-bit direct-page byte. */
static void bit_dp8(Snes *s, uint8_t cell)
{
    uint8_t v = dp8(s, cell);
    m_set_flag(s, CPU_FLAG_N, v & 0x80);
    m_set_flag(s, CPU_FLAG_V, v & 0x40);
    m_set_flag(s, CPU_FLAG_Z, ((uint8_t)s->cpu.a & v) == 0);
}

/* DEC on an 8-bit direct-page byte; returns the result. */
static uint8_t dec_dp8(Snes *s, uint8_t cell)
{
    uint8_t v = (uint8_t)(dp8(s, cell) - 1);
    set_dp8(s, cell, v);
    m_set_nz8(s, v);
    return v;
}

/* 16-bit ADC in decimal mode, as the CPU core computes it (flags included). */
static uint16_t adc16_decimal(Snes *s, uint16_t a, uint16_t data)
{
    int carry = m_flag(s, CPU_FLAG_C) ? 1 : 0;
    int r = (a & 0x000F) + (data & 0x000F) + carry;
    if (r > 0x0009) r += 0x0006;
    carry = r > 0x000F;
    r = (a & 0x00F0) + (data & 0x00F0) + (carry << 4) + (r & 0x000F);
    if (r > 0x009F) r += 0x0060;
    carry = r > 0x00FF;
    r = (a & 0x0F00) + (data & 0x0F00) + (carry << 8) + (r & 0x00FF);
    if (r > 0x09FF) r += 0x0600;
    carry = r > 0x0FFF;
    r = (a & 0xF000) + (data & 0xF000) + (carry << 12) + (r & 0x0FFF);
    m_set_flag(s, CPU_FLAG_V, ~(a ^ data) & (a ^ r) & 0x8000);
    if (r > 0x9FFF) r += 0x6000;
    m_set_flag(s, CPU_FLAG_C, r > 0xFFFF);
    m_set_nz16(s, (uint16_t)r);
    return (uint16_t)r;
}

// $C2:F13F
/* Convert the $92-byte little-endian number at DB:Y to BCD in $8A-$8D: for
 * each set bit, add its 4-byte BCD value from the table at $C2:F17A (X starts
 * at the offset in $51) in decimal mode. Leaves Y past the number. */
void menu_number_to_bcd(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    set_dp16(s, 0x8A, 0x0000);
    set_dp16(s, 0x8C, 0x0000);
    m_ldx(s, dp16(s, 0x51));
    uint16_t bytes_left;
    do {
        m_push16(s, c->y);
        m_lda16(s, m_read16(s, abs_y(s, 0x0000)));
        set_dp16(s, 0x8E, m_lda16(s, c->a & 0x00FF));
        m_ldy(s, 0x0008);
        do {
            uint16_t bits = dp16(s, 0x8E);
            set_dp16(s, 0x8E, m_lsr16(s, bits));
            if (m_flag(s, CPU_FLAG_C)) {
                c->p |= CPU_FLAG_D;
                m_set_flag(s, CPU_FLAG_C, false);
                m_lda16(s, m_read16(s, 0xC2F17A + c->x));
                c->a = adc16_decimal(s, c->a, dp16(s, 0x8A));
                set_dp16(s, 0x8A, c->a);
                m_lda16(s, m_read16(s, 0xC2F17C + c->x));
                c->a = adc16_decimal(s, c->a, dp16(s, 0x8C));
                set_dp16(s, 0x8C, c->a);
                c->p &= (uint8_t)~CPU_FLAG_D;
            }
            for (int i = 0; i < 4; i++)
                m_inx(s);
        } while (m_set_index(s, &c->y, (uint16_t)(c->y - 1)) != 0);
        pull_index(s, &c->y);
        m_iny(s);
        bytes_left = (uint16_t)(dp16(s, 0x92) - 1);
        set_dp16(s, 0x92, bytes_left);
        m_set_nz16(s, bytes_left);
    } while (bytes_left != 0);
    plp(s);
    m_rts(s);
}

/* $C2:F20B: write the low nibble of A as a digit tile at DB:Y with the
 * attribute in $7E, or a blank ($FF) for a leading zero while $8F bit 7 is set.
 * Any other digit clears $8F. */
static void draw_digit(Snes *s)
{
    Cpu *c = &s->cpu;
    m_lda8(s, (uint8_t)c->a & 0x0F);
    bool blank = false;
    if ((uint8_t)c->a == 0) {
        bit_dp8(s, 0x8F);
        blank = m_flag(s, CPU_FLAG_N);
    }
    if (blank) {
        m_lda8(s, 0xFF);
    } else {
        set_dp8(s, 0x8F, 0x00);
        m_set_flag(s, CPU_FLAG_C, false);
        m_set_a8(s, m_adc8(s, (uint8_t)c->a, 0xD4));
    }
    m_write8(s, abs_y(s, 0x0000), (uint8_t)c->a);
    m_write8(s, abs_y(s, 0x0001), m_lda8(s, dp8(s, 0x7E)));
    m_iny(s);
    m_iny(s);
    m_rts(s);
}

/* The direct-page byte $8A,X. */
static uint8_t bcd_byte(Snes *s)
{
    return m_lda8(s, m_read8(s, (uint16_t)(s->cpu.d + 0x8A + s->cpu.x)));
}

/* $C2:F1DA: write $8E digits of the BCD number in $8A-$8D, most significant
 * first, into bank $7E at the address in $90. Leading zeros are blanked while
 * $8F bit 7 is set; unless $8F bit 3 is set, the units digit is always drawn. */
static void draw_bcd_digits(Snes *s)
{
    Cpu *c = &s->cpu;
    m_sep(s, CPU_FLAG_M);
    m_push16(s, 0x7E7E);
    plb(s);
    plb(s);
    m_ldy(s, dp16(s, 0x90));
    m_lda8(s, 0x00);
    m_xba(s);
    m_lda8(s, dp8(s, 0x8E));
    m_set_a8(s, m_lsr8(s, (uint8_t)c->a));
    m_tax(s);
    bool high_digit = m_flag(s, CPU_FLAG_C);
    for (;;) {
        if (high_digit) {
            bcd_byte(s);
            for (int i = 0; i < 4; i++)
                m_set_a8(s, m_lsr8(s, (uint8_t)c->a));
            m_push16(s, 0xF1F4);
            draw_digit(s);
        }
        high_digit = true;
        m_cmp16(s, c->x, 0x0000);
        if (c->x == 0) {
            m_lda8(s, dp8(s, 0x8F));
            if (m_lda8(s, (uint8_t)c->a & 0x08) == 0)
                set_dp8(s, 0x8F, 0x00);
        }
        bcd_byte(s);
        m_push16(s, 0xF206);
        draw_digit(s);
        if (m_set_index(s, &c->x, (uint16_t)(c->x - 1)) & (m_x8(s) ? 0x80 : 0x8000))
            break;
    }
    m_rts(s);
}

// $C2:F114
/* Draw the number at B:Y (A's high byte is its bank) as decimal digit tiles
 * at X in bank $7E. A's low byte: bits 0-1 the number's size in bytes, bits 4-6
 * the digit count times two plus one for an odd count, bit 7 clear to blank
 * leading zeros, bit 3 set to keep blanking through the last byte. Returns X
 * past the digits. */
void menu_draw_number(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_push8(s, c->p);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_push16(s, c->a);
    plb(s);
    plb(s);
    set_dp16(s, 0x90, c->x);
    m_sep(s, CPU_FLAG_M);
    m_push8(s, (uint8_t)c->a);
    set_dp8(s, 0x92, m_lda8(s, (uint8_t)c->a & 0x03));
    set_dp8(s, 0x93, 0x00);
    m_push16(s, 0xF128);
    NATIVE_TIME_AS(s, 0xC2F13F, menu_number_to_bcd(s));
    m_lda8(s, m_pull8(s));
    set_dp8(s, 0x8F, m_lda8(s, (uint8_t)c->a ^ 0x80));
    for (int i = 0; i < 4; i++)
        m_set_a8(s, m_lsr8(s, (uint8_t)c->a));
    set_dp8(s, 0x8E, m_lda8(s, (uint8_t)c->a & 0x07));
    m_push16(s, c->y);
    m_push16(s, 0xF139);
    draw_bcd_digits(s);
    m_tyx(s);
    pull_index(s, &c->y);
    plp(s);
    plb(s);
    m_rts(s);
}

// $C2:EF97
/* Write tile A with the attribute in $7E at X in bank $7E and advance X; tile
 * $FF also gets a blank tile one row above unless $88 bit 7 is set. Flags are
 * preserved. */
void menu_draw_tile(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_sep(s, CPU_FLAG_M);
    bit_dp8(s, 0x88);
    bool above = false;
    if (!m_flag(s, CPU_FLAG_N)) {
        m_cmp8(s, (uint8_t)c->a, 0xFF);
        above = (uint8_t)c->a == 0xFF;
    }
    if (above) {
        m_xba(s);
        m_write8(s, 0x7DFFC0 + c->x, m_lda8(s, 0xFF));
        m_write8(s, 0x7DFFC1 + c->x, m_lda8(s, dp8(s, 0x7E)));
        m_xba(s);
    }
    m_write8(s, 0x7E0000 + c->x, (uint8_t)c->a);
    m_write8(s, 0x7E0001 + c->x, m_lda8(s, dp8(s, 0x7E)));
    m_inx(s);
    m_inx(s);
    plp(s);
    m_rts(s);
}

static void menu_draw_text_loop(Snes *s);

/* INY INY RTS ($C2:F00E), without the RTS. */
static void iny2(Snes *s)
{
    m_iny(s);
    m_iny(s);
}

/* The control code handlers reached through JMP ($008A) with the return
 * address $EFD2 pushed by PER. Each runs with a 16-bit accumulator; the
 * handler's RTS is the m_pull16 at its end. */

/* $C2:EFF4: next line: $65 moves on $80 bytes (two rows), or $40 when $88
 * bit 7 is set; X = $65. */
static void code_new_line(Snes *s)
{
    Cpu *c = &s->cpu;
    m_lda16(s, 0x0080);
    uint16_t v = dp16(s, 0x87);
    m_set_flag(s, CPU_FLAG_N, v & 0x8000);
    m_set_flag(s, CPU_FLAG_V, v & 0x4000);
    m_set_flag(s, CPU_FLAG_Z, (c->a & v) == 0);
    if (v & 0x8000)
        m_lda16(s, 0x0040);
    m_set_flag(s, CPU_FLAG_C, false);
    c->a = m_adc16(s, c->a, dp16(s, 0x65));
    set_dp16(s, 0x65, c->a);
    m_tax(s);
    m_pull16(s);
}

/* $C2:F005: move to the word operand plus $61; X = $65. */
static void code_move(Snes *s)
{
    Cpu *c = &s->cpu;
    m_lda16(s, m_read16(s, abs_y(s, 0x0000)));
    m_set_flag(s, CPU_FLAG_C, false);
    c->a = m_adc16(s, c->a, dp16(s, 0x61));
    set_dp16(s, 0x65, c->a);
    m_tax(s);
    iny2(s);
    m_pull16(s);
}

/* $C2:F011: a number: word operands address and format (for $C2:F114). */
static void code_number(Snes *s)
{
    Cpu *c = &s->cpu;
    push_index(s, c->y);
    m_lda16(s, m_read16(s, abs_y(s, 0x0000)));
    m_push16(s, c->a);
    m_lda16(s, m_read16(s, abs_y(s, 0x0002)));
    pull_index(s, &c->y);
    m_push16(s, 0xF01C);
    NATIVE_TIME_AS(s, 0xC2F114, menu_draw_number(s));
    pull_index(s, &c->y);
    iny2(s);
    iny2(s);
    m_pull16(s);
}

/* $C2:F022: a substring at the 3-byte address operand. */
static void code_substring(Snes *s)
{
    Cpu *c = &s->cpu;
    push_index(s, c->y);
    m_push8(s, c->db);
    lda_abs_y(s, 0x0001);
    m_push16(s, c->a);
    lda_abs_y(s, 0x0000);
    m_set_index(s, &c->y, c->a);
    plb(s);
    plb(s);
    m_push16(s, 0xF030);
    menu_draw_text_loop(s);
    plb(s);
    pull_index(s, &c->y);
    m_iny(s);
    iny2(s);
    m_pull16(s);
}

/* $C2:F036: a name: the string at the word operand in bank $7E, at most 5
 * tiles, padded with blanks to 5; X moves on 10. */
static void code_name(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push16(s, m_read16(s, m_dp(s, 0x87)));
    push_index(s, c->y);
    m_push8(s, c->db);
    m_push8(s, c->p);
    m_txa(s);
    m_set_flag(s, CPU_FLAG_C, false);
    c->a = m_adc16(s, c->a, 0x000A);
    m_push16(s, c->a);
    set_dp16(s, 0x87, m_lda16(s, 0x8005));
    lda_abs_y(s, 0x0000);
    m_set_index(s, &c->y, c->a);
    m_push16(s, 0x7E7E);
    plb(s);
    plb(s);
    m_push16(s, 0xF051);
    menu_draw_text_loop(s);
    if (m_lda8(s, dp8(s, 0x87)) != 0) {
        do {
            m_lda8(s, 0xFF);
            m_push16(s, 0xF05A);
            NATIVE_TIME_AS(s, 0xC2EF97, menu_draw_tile(s));
        } while (dec_dp8(s, 0x87) != 0);
    }
    pull_index(s, &c->x);
    plp(s);
    plb(s);
    pull_index(s, &c->y);
    pla(s);
    if (m_m8(s))
        set_dp8(s, 0x87, (uint8_t)c->a);
    else
        set_dp16(s, 0x87, c->a);
    iny2(s);
    m_pull16(s);
}

/* $C2:F068: set the attribute byte $7E. */
static void code_attribute(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    set_dp8(s, 0x7E, m_lda8(s, m_read8(s, abs_y(s, 0x0000))));
    m_iny(s);
    m_pull16(s);
}

/* $C2:F071: copy a word from bank $7E at the word operand as a tile; counts
 * against $87. */
static void code_copy_tile(Snes *s)
{
    Cpu *c = &s->cpu;
    push_index(s, c->y);
    m_push8(s, c->db);
    lda_abs_y(s, 0x0000);
    m_set_index(s, &c->y, c->a);
    m_push16(s, 0x7E7E);
    plb(s);
    plb(s);
    lda_abs_y(s, 0x0000);
    if (m_m8(s))
        m_write8(s, abs_x(s, 0x0000), (uint8_t)c->a);
    else
        m_write16(s, abs_x(s, 0x0000), c->a);
    plb(s);
    pull_index(s, &c->y);
    m_sep(s, CPU_FLAG_M);
    m_inx(s);
    m_inx(s);
    dec_dp8(s, 0x87);
    iny2(s);
    m_pull16(s);
}

/* $C2:F08D: the byte operand's count of blank tiles. */
static void code_blanks(Snes *s)
{
    Cpu *c = &s->cpu;
    lda_abs_y(s, 0x0000);
    m_iny(s);
    push_index(s, c->y);
    m_lda16(s, c->a & 0x00FF);
    m_set_index(s, &c->y, c->a);
    m_sep(s, CPU_FLAG_M);
    do {
        m_lda8(s, 0xFF);
        m_push16(s, 0xF09C);
        NATIVE_TIME_AS(s, 0xC2EF97, menu_draw_tile(s));
    } while (m_set_index(s, &c->y, (uint16_t)(c->y - 1)) != 0);
    pull_index(s, &c->y);
    m_pull16(s);
}

/* $C2:F0A2: take the palette bits ($1C) of the attribute from the byte in
 * bank $7E at the word operand. */
static void code_palette_from(Snes *s)
{
    Cpu *c = &s->cpu;
    m_sep(s, CPU_FLAG_M);
    push_index(s, c->x);
    m_ldx(s, m_x8(s) ? m_read8(s, abs_y(s, 0x0000)) : m_read16(s, abs_y(s, 0x0000)));
    iny2(s);
    m_lda8(s, 0x1C);
    m_trb8(s, m_dp(s, 0x7E));
    m_lda8(s, m_read8(s, 0x7E0000 + c->x));
    m_lda8(s, (uint8_t)c->a & 0x1C);
    m_tsb8(s, m_dp(s, 0x7E));
    pull_index(s, &c->x);
    m_pull16(s);
}

/* $C2:F0B8: set $88. */
static void code_set_88(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    set_dp8(s, 0x88, m_lda8(s, m_read8(s, abs_y(s, 0x0000))));
    m_iny(s);
    m_pull16(s);
}

/* $C2:F0D1: a one-byte value from bank $7E at the word operand as two digits,
 * or two "--"-like tiles ($2F) when it isn't below the limit $FF:CF33 indexed
 * by the byte operand. */
static void code_limited_number(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    push_index(s, c->y);
    push_index(s, c->x);
    lda_abs_y(s, 0x0000);
    m_lda16(s, c->a & 0x00FF);
    m_tax(s);
    lda_abs_y(s, 0x0001);
    m_set_index(s, &c->y, c->a);
    m_sep(s, CPU_FLAG_M);
    m_push16(s, 0x7E7E);
    plb(s);
    plb(s);
    m_lda8(s, m_read8(s, abs_y(s, 0x0000)));
    m_cmp8(s, (uint8_t)c->a, m_read8(s, 0xFFCF33 + c->x));
    pull_index(s, &c->x);
    if (!m_flag(s, CPU_FLAG_C)) {
        m_lda8(s, 0x7E);
        m_xba(s);
        m_lda8(s, 0x11);
        m_push16(s, 0xF0F7);
        NATIVE_TIME_AS(s, 0xC2F114, menu_draw_number(s));
    } else {
        m_lda8(s, 0x2F);
        m_write8(s, abs_x(s, 0x0000), (uint8_t)c->a);
        m_write8(s, abs_x(s, 0x0002), (uint8_t)c->a);
        m_lda8(s, dp8(s, 0x7E));
        m_write8(s, abs_x(s, 0x0001), (uint8_t)c->a);
        m_write8(s, abs_x(s, 0x0003), (uint8_t)c->a);
        for (int i = 0; i < 4; i++)
            m_inx(s);
    }
    pull_index(s, &c->y);
    plb(s);
    m_iny(s);
    iny2(s);
    m_pull16(s);
}

/* $C2:EFBE (JSR from $EF8A): run control code A through the table at
 * $C2:EFD4, returning through the RTS at $C2:EFD3. Handlers not ported (and
 * codes past the table's ported entries) are called out at the JMP. */
static void control_code(Snes *s)
{
    Cpu *c = &s->cpu;
    push_index(s, c->x);
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, c->a & 0x00FF);
    c->a = m_asl16(s, c->a);
    m_tax(s);
    m_lda16(s, m_read16(s, 0xC2EFD4 + c->x));
    set_dp16(s, 0x8A, c->a);
    pull_index(s, &c->x);
    uint16_t handler = m_read16(s, 0x00008A);
    void (*fn)(Snes *) = NULL;
    switch (handler) {
    case 0xEFD3: break;
    case 0xEFF4: fn = code_new_line; break;
    case 0xF005: fn = code_move; break;
    case 0xF011: fn = code_number; break;
    case 0xF022: fn = code_substring; break;
    case 0xF036: fn = code_name; break;
    case 0xF068: fn = code_attribute; break;
    case 0xF071: fn = code_copy_tile; break;
    case 0xF08D: fn = code_blanks; break;
    case 0xF0A2: fn = code_palette_from; break;
    case 0xF0B8: fn = code_set_88; break;
    case 0xF0D1: fn = code_limited_number; break;
    default:
        /* PER $EFD2 : JMP ($008A) is a call returning to $EFD3 */
        m_jsr(s, 0xC2EFD0, 0xC20000 | handler);
        m_rts(s);
        return;
    }
    m_push16(s, 0xEFD2);
    if (fn)
        fn(s);
    else
        m_pull16(s);
    m_rts(s);
}

// $C2:EF7E
/* Draw the string at DB:Y into bank $7E at X (see the top of the file) until
 * its terminator or until $87 tiles have been written. Also entered
 * recursively by the substring and name codes. Returns with 8-bit A. */
static void menu_draw_text_loop(Snes *s)
{
    Cpu *c = &s->cpu;
    for (;;) {
        m_sep(s, CPU_FLAG_M);
        if (m_lda8(s, m_read8(s, abs_y(s, 0x0000))) == 0)
            break;
        m_iny(s);
        m_cmp8(s, (uint8_t)c->a, 0x10);
        if ((uint8_t)c->a < 0x10) {
            m_push16(s, 0xEF8C);
            control_code(s);
            continue;
        }
        m_push16(s, 0xEF91);
        NATIVE_TIME_AS(s, 0xC2EF97, menu_draw_tile(s));
        if (dec_dp8(s, 0x87) == 0)
            break;
    }
    m_rts(s);
}

// $C2:EF65
/* Draw the string at B:Y (A's high byte is its bank) at X: A's low byte goes
 * to $88 and, without bit 7, to the tile limit $87; $65 = X. Clears attribute
 * bits $DC in $7E afterwards. */
void menu_draw_text(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_push8(s, c->p);
    m_sep(s, CPU_FLAG_M);
    set_dp8(s, 0x88, (uint8_t)c->a);
    set_dp8(s, 0x87, m_lda8(s, (uint8_t)c->a & 0x7F));
    if (m_x8(s))
        set_dp8(s, 0x65, (uint8_t)c->x);
    else
        set_dp16(s, 0x65, c->x);
    m_xba(s);
    m_push8(s, (uint8_t)c->a);
    plb(s);
    m_push16(s, 0xEF76);
    menu_draw_text_loop(s);
    m_lda8(s, 0xDC);
    m_trb8(s, m_dp(s, 0x7E));
    plp(s);
    plb(s);
    m_rts(s);
}
