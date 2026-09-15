/* Battle: number sprites shown over the combatants.
 *
 * Up to 44 sprite entries at $922D (x, y, tile, attributes; four per
 * combatant, 11 combatants), their visibility bits per combatant in $92DD,
 * built from each combatant's number in $AD9C (a 4-byte record per combatant
 * at $AD9C + $CC:F829 offset: value, then a kind at $AD9E). Presumably damage
 * and recovery numbers. */
#include "battle/menu_window.h"

#include "port/machine.h"

static uint8_t abs8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr)); }
static void set_abs8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr), v); }
static uint8_t absx8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.x); }
static void set_absx8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.x, v); }
static uint8_t absy8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.y); }
static void set_absy8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.y, v); }
static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }

static void adc_a(Snes *s, uint8_t v) { m_set_a8(s, m_adc8(s, (uint8_t)s->cpu.a, v)); }

static void inc_abs8(Snes *s, uint16_t addr)
{
    uint8_t v = (uint8_t)(abs8(s, addr) + 1);
    set_abs8(s, addr, v);
    m_set_nz8(s, v);
}

static void inc_dp8(Snes *s, uint8_t cell)
{
    uint8_t v = (uint8_t)(dp8(s, cell) + 1);
    set_dp8(s, cell, v);
    m_set_nz8(s, v);
}

static void inx4(Snes *s)
{
    for (int i = 0; i < 4; i++)
        m_inx(s);
}

/* LDA digit : CMP #$90 -- true if the digit cell holds the blank tile. */
static bool blank(Snes *s, uint16_t digit)
{
    uint8_t v = m_lda8(s, abs8(s, digit));
    m_cmp8(s, v, 0x90);
    return v == 0x90;
}

/* One digit sprite at X: tile from `digit`, x = $94 + the digit's offset
 * (dp `offset`), y = $95, attributes $8C; clears its bits in $84. */
static void put_digit(Snes *s, uint16_t digit, uint8_t offset, uint8_t mask)
{
    set_absx8(s, 0x922F, m_lda8(s, abs8(s, digit)));
    m_set_flag(s, CPU_FLAG_C, false);
    m_lda8(s, dp8(s, 0x94));
    adc_a(s, dp8(s, offset));
    set_absx8(s, 0x922D, (uint8_t)s->cpu.a);
    set_absx8(s, 0x922E, m_lda8(s, dp8(s, 0x95)));
    set_absx8(s, 0x9230, m_lda8(s, dp8(s, 0x8C)));
    set_dp8(s, 0x84, m_lda8(s, (uint8_t)(m_lda8(s, dp8(s, 0x84)) & mask)));
}

// $C1:75BB
/* The shared end of the script opcode handlers: advance the script pointer
 * $E6-$E8 by A (8-bit A, with B from before) and return. */
static void advance_script(Snes *s)
{
    Cpu *c = &s->cpu;
    m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
    c->a = m_adc16(s, c->a, dp16(s, 0xE6));
    set_dp16(s, 0xE6, c->a);
    c->a = m_adc16(s, m_lda16(s, dp16(s, 0xE8)), 0x0000);
    set_dp16(s, 0xE8, c->a);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

/* $C1:5B2D: build each combatant's number sprites. */
static bool build_sprites(Snes *s)
{
    Cpu *c = &s->cpu;
    set_abs8(s, 0xA3D2, 0x00);
    inc_abs8(s, 0xA3D1);
    set_abs8(s, 0xA3D3, m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0x9895)) & 0x07)));
    m_tax(s);
    m_lda8(s, m_read8(s, 0xCCF829 + c->x));
    m_tax(s);
    set_dp16(s, 0x80, c->x);
    m_tdc(s);
    m_set_index(s, &c->y, c->a);
    set_dp16(s, 0x82, c->y);
    set_dp16(s, 0x92, c->y);
    do {
        /* $C1:5B49: the combatant's position */
        set_dp8(s, 0x8C, m_lda8(s, 0x31));
        if (m_lda8(s, absy8(s, 0xA003)) == 0) {
            set_dp8(s, 0x94, m_lda8(s, absy8(s, 0x1D0C)));
            set_dp8(s, 0x95, m_lda8(s, absy8(s, 0x1D23)));
        } else {
            m_set_flag(s, CPU_FLAG_C, false);
            m_lda8(s, absy8(s, 0x1D0C));
            adc_a(s, absy8(s, 0x9708));
            set_dp8(s, 0x94, (uint8_t)c->a);
            m_set_flag(s, CPU_FLAG_C, false);
            m_lda8(s, absy8(s, 0x1D23));
            adc_a(s, absy8(s, 0x9713));
            set_dp8(s, 0x95, (uint8_t)c->a);
        }
        m_ldx16(s, dp16(s, 0x80));
        uint8_t kind = m_lda8(s, absx8(s, 0xAD9E));
        if (kind == 0) {
            /* $C1:5CBF */
            m_set_flag(s, CPU_FLAG_C, false);
            m_lda8(s, dp8(s, 0x82));
            adc_a(s, 0x10);
            set_dp8(s, 0x82, (uint8_t)c->a);
        } else {
            m_cmp8(s, kind, 0x05);
            if (kind == 0x05) {
                m_lda8(s, 0x90);
                set_abs8(s, 0x949B, 0x90);
                set_abs8(s, 0x949C, 0x90);
                set_abs8(s, 0x949D, 0x90);
                set_abs8(s, 0x949E, m_lda8(s, 0x9A));
                set_abs8(s, 0x949F, m_lda8(s, 0x9B));
                set_dp8(s, 0x8E, m_lda8(s, 0x02));
            } else {
                m_cmp8(s, kind, 0x03);
                if (kind != 0x03) {
                    m_cmp8(s, kind, 0x01);
                    bool other = kind == 0x01;
                    if (!other) {
                        set_absy8(s, 0xA0DC, m_lda8(s, 0x01));
                        uint8_t k = m_lda8(s, absx8(s, 0xAD9E));
                        m_cmp8(s, k, 0x02);
                        other = k == 0x02;
                    }
                    if (other)
                        set_dp8(s, 0x8C, m_lda8(s, 0x39));
                }
                /* $C1:5BB1 */
                m_rep(s, CPU_FLAG_M);
                m_write16(s, m_abs(s, 0x9499), m_lda16(s, m_read16(s, m_abs(s, 0xAD9C) + c->x)));
                m_jsl(s, 0xC15BB9, 0xCFFDAD);
                if (c->e || (c->p & CPU_FLAG_X))
                    return false;
                m_sep(s, CPU_FLAG_M);
                set_dp8(s, 0x8E, 0x00);
                static const uint16_t leading[3] = { 0x949C, 0x949D, 0x949E };
                for (int i = 0; i < 3; i++) {
                    if (!blank(s, leading[i]))
                        break;
                    inc_dp8(s, 0x8E);
                }
            }

            /* $C1:5BDC: digit offsets for the number of leading blanks */
            m_lda8(s, dp8(s, 0x8E));
            m_set_a8(s, m_asl8(s, (uint8_t)c->a));
            m_set_a8(s, m_asl8(s, (uint8_t)c->a));
            m_tax(s);
            for (uint16_t k = 0; k < 4; k++)
                set_dp8(s, (uint8_t)(0x8E + k), m_lda8(s, m_read8(s, 0xCCF5C4 + k + c->x)));
            set_dp8(s, 0x84, m_lda8(s, 0x55));
            m_ldx16(s, dp16(s, 0x82));
            if (!blank(s, 0x949C))
                put_digit(s, 0x949C, 0x8E, 0xFC);
            inx4(s);
            if (!blank(s, 0x949D) || !blank(s, 0x949C))
                put_digit(s, 0x949D, 0x8F, 0xF3);
            inx4(s);
            if (!blank(s, 0x949E) || !blank(s, 0x949D) || !blank(s, 0x949C))
                put_digit(s, 0x949E, 0x90, 0xCF);
            inx4(s);
            put_digit(s, 0x949F, 0x91, 0x3F);
            inx4(s);
            set_dp16(s, 0x82, c->x);
            set_absy8(s, 0x92DD, m_lda8(s, dp8(s, 0x84)));
            set_absy8(s, 0xA3D4, m_lda8(s, 0x01));
        }

        /* $C1:5CC6 */
        m_ldx16(s, dp16(s, 0x80));
        inx4(s);
        set_dp16(s, 0x80, c->x);
        m_iny(s);
        m_cmp16(s, c->y, 0x000B);
    } while (c->y != 0x000B);
    set_abs8(s, 0xAC5A, m_lda8(s, 0x01));
    m_tdc(s);
    return true;
}

// $C1:5ADB
/* Battle script opcodes $50-$55 (through the $C1:7A6B table), in three steps
 * counted by $A3D1: first hide all number sprites ($922D-$92DC = $F0), clear
 * $92DD-$92E7 and $A3B9-$A3BC, and copy $999F-$99A6 to $2300 and $99A7-$99AE
 * to $2380; then build them ($C1:5B2D; the number's digits come from
 * $CF:FDAD) and set $AC5A; then, once $AC5A is clear, reset $A3D1 and move
 * past the opcode. Continues into $C1:75BB with the length to skip. */
void battle_script_build_number_sprites(Snes *s)
{
    Cpu *c = &s->cpu;
    uint8_t step = m_lda8(s, abs8(s, 0xA3D1));
    if (step == 0) {
        inc_abs8(s, 0xA3D1);
        m_ldx16(s, 0x00AF);
        m_lda8(s, 0xF0);
        do {
            set_absx8(s, 0x922D, 0xF0);
            m_ldx16(s, (uint16_t)(c->x - 1));
        } while (!(c->x & 0x8000));
        m_ldx16(s, 0x000A);
        do {
            set_absx8(s, 0x92DD, 0x00);
            m_ldx16(s, (uint16_t)(c->x - 1));
        } while (!(c->x & 0x8000));
        for (uint16_t k = 0; k < 4; k++)
            set_abs8(s, (uint16_t)(0xA3B9 + k), 0x00);
        static const uint16_t copies[2][2] = { { 0x999F, 0x2300 }, { 0x99A7, 0x2380 } };
        for (int i = 0; i < 2; i++) {
            m_tdc(s);
            m_set_index(s, &c->y, c->a);
            do {
                set_absy8(s, copies[i][1], m_lda8(s, absy8(s, copies[i][0])));
                m_iny(s);
                m_cmp16(s, c->y, 0x0008);
            } while (c->y != 0x0008);
        }
        m_tdc(s);
    } else {
        step = m_lda8(s, abs8(s, 0xA3D1));
        m_cmp8(s, step, 0x01);
        if (step == 0x01) {
            if (!build_sprites(s))
                return;
        } else if (m_lda8(s, abs8(s, 0xAC5A)) != 0) {
            /* $C1:5CE0 */
            m_tdc(s);
        } else {
            set_abs8(s, 0xA3D1, 0x00);
            m_lda8(s, 0x01);
        }
    }
    advance_script(s);
}
