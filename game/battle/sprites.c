/* Battle: drawing battle objects' sprites into the OAM buffer.
 *
 * Each frame the visible objects are sorted by depth and each one's sprite
 * parts are written to the OAM buffer at $0700 (4 bytes per sprite; $0900
 * holds the high table bits). These routines run with the direct page at
 * $1D00, where the per-object tables live ($00+ X positions, $17+ Y positions,
 * $39+ flags, $73+ frame part counts, $8A+/$A1+ the draw list), apart from a
 * stretch at $0000. $C9 is the next free OAM offset. */
#include "battle/battle.h"

#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }
/* dp,X (16-bit index, wrapping in bank 0) */
static uint32_t dpx(const Snes *s, uint8_t cell) { return (uint16_t)(s->cpu.d + cell + s->cpu.x); }
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

/* PHX/PHY : PLD -- move the direct page, writing the stack bytes. */
static void set_direct_page(Snes *s, uint16_t d)
{
    m_push16(s, d);
    s->cpu.d = m_pull16(s);
    m_set_nz16(s, s->cpu.d);
}

/* LDA #$FF or #$00 by the sign of v. */
static void sign_byte(Snes *s, uint8_t v) { m_lda8(s, (v & 0x80) ? 0xFF : 0x00); }

// $CF:EAC5
/* Write the sprite parts of object $C5 to the OAM buffer: position = the
 * object's position ($00,X and $17,X on the direct page, plus the signed
 * offsets $A498,X/$A4A3,X) plus each part's offset ($9F47/$9F49 per part,
 * indexed from ($C5 - $0C) * 16); attribute bits 1-5 from $9930,X | $99A9,X;
 * parts needing the X high bit ($39 set and Y below $F0, or $33 set) set their
 * OAM high table bit. The part count comes from $CC:F50A by frame ($9670,X).
 * Skipped while $9FEB,X is set. Entered with 16-bit A; leaves 8-bit A. */
void battle_draw_sprite_parts(Snes *s)
{
    Cpu *c = &s->cpu;
    m_lda16(s, 0x0000);
    m_sep(s, CPU_FLAG_M);
    uint8_t object = m_lda8(s, dp8(s, 0xC5));
    m_tax(s);
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t index = m_sbc8(s, object, 0x0C);
    for (int i = 0; i < 4; i++)
        index = m_asl8(s, index);
    m_set_a8(s, index);
    m_set_index(s, &c->y, c->a);
    set_dp16(s, 0x42, c->y);
    if (m_lda8(s, absx8(s, 0x9FEB)) != 0) {
        m_rts(s);
        return;
    }
    set_dp8(s, 0x2E, m_lda8(s, m_read8(s, dpx(s, 0x00))));
    set_dp8(s, 0x2F, 0x00);
    uint8_t ox = m_lda8(s, absx8(s, 0xA498));
    set_dp8(s, 0x3A, ox);
    sign_byte(s, ox);
    set_dp8(s, 0x3B, (uint8_t)c->a);
    set_dp8(s, 0x30, m_lda8(s, m_read8(s, dpx(s, 0x17))));
    set_dp8(s, 0x31, 0x00);
    uint8_t oy = m_lda8(s, absx8(s, 0xA4A3));
    set_dp8(s, 0x3C, oy);
    sign_byte(s, oy);
    set_dp8(s, 0x3D, (uint8_t)c->a);
    m_rep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, false);
    m_lda16(s, dp16(s, 0x2E));
    c->a = m_adc16(s, c->a, dp16(s, 0x3A));
    set_dp16(s, 0x2E, c->a);
    m_set_flag(s, CPU_FLAG_C, false);
    m_lda16(s, dp16(s, 0x30));
    c->a = m_adc16(s, c->a, dp16(s, 0x3C));
    set_dp16(s, 0x30, c->a);
    m_lda16(s, 0x0000);
    m_sep(s, CPU_FLAG_M);
    uint8_t bits = m_lda8(s, absx8(s, 0x9930));
    set_dp8(s, 0x37, m_lda8(s, (uint8_t)(bits | absx8(s, 0x99A9))));
    set_dp8(s, 0x36, 0x00);
    uint8_t frame = m_asl8(s, m_lda8(s, absx8(s, 0x9670)));
    m_set_a8(s, frame);
    m_tax(s);
    m_lda8(s, m_read8(s, 0xCCF50A + c->x));
    m_tax(s);
    set_dp16(s, 0x34, c->x);

    do {
        m_set_index(s, &c->y, dp16(s, 0x42));
        m_ldx16(s, dp16(s, 0xC9));
        m_rep(s, CPU_FLAG_M);
        m_set_flag(s, CPU_FLAG_C, false);
        m_lda16(s, m_read16(s, m_abs(s, 0x9F47) + c->y));
        c->a = m_adc16(s, c->a, dp16(s, 0x2E));
        set_dp16(s, 0x32, c->a);
        m_set_flag(s, CPU_FLAG_C, false);
        m_lda16(s, m_read16(s, m_abs(s, 0x9F49) + c->y));
        c->a = m_adc16(s, c->a, dp16(s, 0x30));
        set_dp16(s, 0x38, c->a);
        uint32_t attr = m_abs(s, 0x0702) + c->x;
        uint16_t v = m_lda16(s, m_read16(s, attr));
        v = m_lda16(s, v & 0xC1FF);
        m_write16(s, attr, m_lda16(s, (uint16_t)(v | dp16(s, 0x36))));
        m_lda16(s, 0x0000);
        m_sep(s, CPU_FLAG_M);
        set_absx8(s, 0x0700, m_lda8(s, dp8(s, 0x32)));
        set_absx8(s, 0x0701, m_lda8(s, dp8(s, 0x38)));
        bool high;
        if (m_lda8(s, dp8(s, 0x39)) != 0) {
            uint8_t y = m_lda8(s, dp8(s, 0x38));
            m_cmp8(s, y, 0xF0);
            high = y < 0xF0;
        } else {
            high = m_lda8(s, dp8(s, 0x33)) != 0;
        }
        if (high) {
            m_lda8(s, absx8(s, 0x902D));
            m_tax(s);
            m_set_index(s, &c->y, dp16(s, 0xC9));
            uint8_t b = m_lda8(s, absx8(s, 0x0900));
            set_absx8(s, 0x0900, m_lda8(s, (uint8_t)(b | absy8(s, 0x8E2D))));
        }
        m_ldx16(s, dp16(s, 0xC9));
        for (int i = 0; i < 4; i++)
            m_inx(s);
        set_dp16(s, 0xC9, c->x);
        m_set_index(s, &c->y, dp16(s, 0x42));
        for (int i = 0; i < 4; i++)
            m_iny(s);
        set_dp16(s, 0x42, c->y);
    } while (dec_dp8(s, 0x34) != 0);
    m_rts(s);
}

// $CF:EB94
/* Copy the tile and attribute words (bytes 2-3 of each sprite) of $CD groups
 * of four sprites, eight groups at most, from X to Y. 16-bit A. */
void battle_copy_sprite_tiles(Snes *s)
{
    Cpu *c = &s->cpu;
    for (uint16_t group = 0; group < 8; group++) {
        for (uint16_t k = 0; k < 4; k++) {
            uint16_t offset = (uint16_t)(0x0002 + group * 16 + k * 4);
            uint16_t v = m_lda16(s, m_read16(s, m_abs(s, offset) + c->x));
            m_write16(s, m_abs(s, offset) + c->y, v);
        }
        if (group == 7)
            break;
        uint16_t count = (uint16_t)(dp16(s, 0xCD) - 1);
        set_dp16(s, 0xCD, count);
        m_set_nz16(s, count);
        if (count == 0)
            break;
    }
    m_rts(s);
}

// $CF:EA8A
/* Draw the shadow sprite under object X, if it has one ($A5CC,X) and isn't
 * hidden ($9FEB,X): tile 2 at the object's position less (8, 4), attributes
 * $A5E3 | $9F46. Advances $C9 by 16. */
void battle_draw_shadow(Snes *s)
{
    Cpu *c = &s->cpu;
    if (m_lda8(s, absx8(s, 0xA5CC)) == 0 || m_lda8(s, absx8(s, 0x9FEB)) != 0) {
        m_rts(s);
        return;
    }
    m_set_index(s, &c->y, dp16(s, 0xC9));
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t x = m_sbc8(s, m_lda8(s, absx8(s, 0x1D00)), 0x08);
    m_set_a8(s, x);
    set_absy8(s, 0x0700, x);
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t y = m_sbc8(s, m_lda8(s, absx8(s, 0x1D17)), 0x04);
    m_set_a8(s, y);
    set_absy8(s, 0x0701, y);
    set_absy8(s, 0x0702, m_lda8(s, 0x02));
    uint8_t attr = m_lda8(s, abs8(s, 0xA5E3));
    set_absy8(s, 0x0703, m_lda8(s, (uint8_t)(attr | abs8(s, 0x9F46))));
    m_rep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, false);
    m_lda16(s, c->y);
    c->a = m_adc16(s, c->a, 0x0010);
    m_set_index(s, &c->y, c->a);
    set_dp16(s, 0xC9, c->y);
    m_lda16(s, 0x0000);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

/* The loops of $CF:EDB8 and $CF:EDF7 over the eight effect slots of object
 * $C5 ($A3DF,X for X = $C5-$0C and then every 11): call $CF:EEEB for each slot
 * whose value passes `wanted` and whose effect ($A1D8 indexed by the value
 * masked with `mask`) is active. */
static void effect_slots(Snes *s, bool negative, uint8_t mask, uint32_t call_site)
{
    Cpu *c = &s->cpu;
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t first = m_sbc8(s, m_lda8(s, dp8(s, 0xC5)), 0x0C);
    m_set_a8(s, first);
    m_tax(s);
    for (;;) {
        m_push16(s, c->x);
        uint8_t v = m_lda8(s, absx8(s, 0xA3DF));
        bool pass = v != 0 && (negative ? (v & 0x80) != 0 : (v & 0x80) == 0);
        if (pass) {
            m_push16(s, c->x);
            m_lda8(s, (uint8_t)(v & mask));
            m_tax(s);
            bool active = m_lda8(s, absx8(s, 0xA1D8)) != 0;
            c->x = m_pull16(s);
            m_set_nz16(s, c->x);
            if (active && !call(s, call_site, 0xCFEEEB))
                return;
        }
        c->x = m_pull16(s);
        m_set_nz16(s, c->x);
        m_txa(s);
        m_set_flag(s, CPU_FLAG_C, false);
        uint8_t next = m_adc8(s, (uint8_t)c->a, 0x0B);
        m_set_a8(s, next);
        m_tax(s);
        m_cmp16(s, c->x, 0x0058);
        if (c->x >= 0x0058)
            break;
    }
    m_rts(s);
}

/* $CF:EE2C, from $CF:EDB8: with the direct page at $0000, draw the animated
 * marker sprite for object ($1DC5 - $0C) when $A48E,X is set, stepping its
 * animation ($A4BA countdown, reloaded from $A4C5; frame $A4DB of 4) and
 * placing the frame from $CC:F6D4 at the object's position ($1D0C,X/$1D23,X). */
static void draw_marker(Snes *s)
{
    Cpu *c = &s->cpu;
    m_set_index(s, &c->y, 0x0000);
    set_direct_page(s, c->y);
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t index = m_sbc8(s, m_lda8(s, abs8(s, 0x1DC5)), 0x0C);
    m_set_a8(s, index);
    m_tax(s);
    if (m_lda8(s, absx8(s, 0xA48E)) != 0) {
        set_dp8(s, 0xCC, m_lda8(s, absx8(s, 0x1D0C)));
        set_dp8(s, 0xCD, m_lda8(s, absx8(s, 0x1D23)));
        uint8_t count = (uint8_t)(absx8(s, 0xA4BA) - 1);
        set_absx8(s, 0xA4BA, count);
        m_set_nz8(s, count);
        if (count == 0) {
            set_absx8(s, 0xA4BA, m_lda8(s, absx8(s, 0xA4C5)));
            uint8_t frame = (uint8_t)(absx8(s, 0xA4DB) + 1);
            set_absx8(s, 0xA4DB, frame);
            m_set_nz8(s, frame);
            frame = m_lda8(s, absx8(s, 0xA4DB));
            m_cmp8(s, frame, 0x04);
            if (frame == 0x04) {
                m_lda16(s, c->d);
                set_absx8(s, 0xA4DB, (uint8_t)c->a);
            }
        }
        uint8_t f = m_lda8(s, absx8(s, 0xA4DB));
        f = m_asl8(s, f);
        f = m_asl8(s, f);
        m_set_flag(s, CPU_FLAG_C, false);
        f = m_adc8(s, f, absx8(s, 0xA4D0));
        m_set_a8(s, f);
        m_tax(s);
        m_set_index(s, &c->y, m_read16(s, m_abs(s, 0x1DC9)));
        m_set_flag(s, CPU_FLAG_C, false);
        uint8_t px = m_adc8(s, m_lda8(s, m_read8(s, 0xCCF6D4 + c->x)), dp8(s, 0xCC));
        m_set_a8(s, px);
        set_absy8(s, 0x0700, px);
        m_set_flag(s, CPU_FLAG_C, false);
        uint8_t py = m_adc8(s, m_lda8(s, m_read8(s, 0xCCF6D5 + c->x)), dp8(s, 0xCD));
        m_set_a8(s, py);
        set_absy8(s, 0x0701, py);
        set_absy8(s, 0x0702, m_lda8(s, m_read8(s, 0xCCF6D6 + c->x)));
        uint8_t attr = m_lda8(s, m_read8(s, 0xCCF6D7 + c->x));
        set_absy8(s, 0x0703, m_lda8(s, (uint8_t)(attr | abs8(s, 0xA5E3))));
        for (int i = 0; i < 4; i++)
            m_iny(s);
        m_write16(s, m_abs(s, 0x1DC9), c->y);
    }
    m_set_index(s, &c->y, 0x1D00);
    set_direct_page(s, c->y);
    m_rts(s);
}

/* $CF:EE9F, from $CF:EDB8: copy object ($C5 - $0C)'s four prepared sprites
 * ($922D + 16 * index) to the OAM buffer, with its high table byte ($92DD,X),
 * when $A3D4,X is set. */
static void copy_prepared_sprites(Snes *s)
{
    Cpu *c = &s->cpu;
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t index = m_sbc8(s, m_lda8(s, dp8(s, 0xC5)), 0x0C);
    m_set_a8(s, index);
    m_tax(s);
    if (m_lda8(s, absx8(s, 0xA3D4)) == 0) {
        m_rts(s);
        return;
    }
    m_set_index(s, &c->y, dp16(s, 0xC9));
    m_lda8(s, absy8(s, 0x902D));
    m_set_index(s, &c->y, c->a);
    set_absy8(s, 0x0900, m_lda8(s, absx8(s, 0x92DD)));
    m_txa(s);
    uint8_t v = (uint8_t)c->a;
    for (int i = 0; i < 4; i++)
        v = m_asl8(s, v);
    m_set_a8(s, v);
    m_tax(s);
    m_write8(s, m_abs(s, 0x00C8), m_lda8(s, 0x04));
    m_set_index(s, &c->y, dp16(s, 0xC9));
    uint8_t n;
    do {
        for (uint16_t k = 0; k < 4; k++)
            set_absy8(s, (uint16_t)(0x0700 + k), m_lda8(s, absx8(s, (uint16_t)(0x922D + k))));
        for (int i = 0; i < 4; i++)
            m_iny(s);
        for (int i = 0; i < 4; i++)
            m_inx(s);
        uint32_t counter = m_abs(s, 0x00C8);
        n = (uint8_t)(m_read8(s, counter) - 1);
        m_write8(s, counter, n);
        m_set_nz8(s, n);
    } while (n != 0);
    set_dp16(s, 0xC9, c->y);
    m_rts(s);
}

// $CF:EDB8
/* Before an object's parts are drawn: with $A3D1 set, copy its prepared
 * sprites; otherwise, unless $A013 or $A0FE is set, draw its marker; otherwise
 * run its negative effect slots. */
void battle_draw_object_underlays(Snes *s)
{
    if (m_lda8(s, abs8(s, 0xA3D1)) != 0) {
        copy_prepared_sprites(s);
        return;
    }
    if (m_lda8(s, abs8(s, 0xA013)) == 0 && m_lda8(s, abs8(s, 0xA0FE)) == 0) {
        draw_marker(s);
        return;
    }
    effect_slots(s, true, 0x7F, 0xCFEDE5);
}

// $CF:EDF7
/* After an object's parts are drawn: with $A013 set and $A3D1 clear, run its
 * positive effect slots. */
void battle_draw_object_overlays(Snes *s)
{
    if (m_lda8(s, abs8(s, 0xA013)) == 0 || m_lda8(s, abs8(s, 0xA3D1)) != 0) {
        m_rts(s);
        return;
    }
    effect_slots(s, false, 0xBF, 0xCFEE1A);
}

// $CF:E92C
/* Step the four-part effect for object X: when its countdown ($AB7C,X) runs
 * out, reload it from $AB71,X and run $CF:E99C for each part with $32-$35
 * loaded from that part's tables. */
void battle_step_object_effect(Snes *s)
{
    Cpu *c = &s->cpu;
    set_dp16(s, 0xCB, c->x);
    uint8_t count = (uint8_t)(absx8(s, 0xAB7C) - 1);
    set_absx8(s, 0xAB7C, count);
    m_set_nz8(s, count);
    if (count != 0) {
        m_rts(s);
        return;
    }
    set_absx8(s, 0xAB7C, m_lda8(s, absx8(s, 0xAB71)));
    static const struct { uint16_t t[4]; uint32_t site; } parts[4] = {
        { { 0xAB87, 0xAB92, 0xAC58, 0xAC63 }, 0xCFE94D },
        { { 0xAB9D, 0xABA8, 0xAC6E, 0xAC79 }, 0xCFE966 },
        { { 0xABB3, 0xABBE, 0xAC84, 0xAC8F }, 0xCFE97F },
        { { 0xABC9, 0xABD4, 0xAC9A, 0xACA5 }, 0xCFE998 },
    };
    for (int p = 0; p < 4; p++) {
        if (p > 0)
            m_ldx16(s, dp16(s, 0xCB));
        for (int k = 0; k < 4; k++)
            set_dp8(s, (uint8_t)(0x32 + k), m_lda8(s, absx8(s, parts[p].t[k])));
        if (!call(s, parts[p].site, 0xCFE99C))
            return;
    }
    m_rts(s);
}

// $CF:E83A
/* Draw all battle objects. With the direct page at $1D00: list the objects
 * that exist ($96E9,Y for Y = $0C-$16) with their Y position as the key and
 * slot | $40, plus a $FE-keyed entry for those with $A5C1,Y set; sort the list
 * by key (descending); then for each entry in order draw the object's
 * underlays, part tiles, parts and overlays, and its shadow unless $39 was set
 * (then marked $80; an object already marked, or with no parts, gets only the
 * shadow), and step its effect when $A013 is set. */
void battle_draw_objects(Snes *s)
{
    Cpu *c = &s->cpu;
    m_ldx16(s, 0x1D00);
    set_direct_page(s, c->x);
    m_set_index(s, &c->y, 0x000C);
    m_ldx16(s, 0x0000);
    do {
        if (m_lda8(s, absy8(s, 0x96E9)) != 0) {
            m_write8(s, dpx(s, 0x8A), m_lda8(s, absy8(s, 0x1D17)));
            m_lda8(s, (uint8_t)c->y);
            m_write8(s, dpx(s, 0xA1), m_lda8(s, (uint8_t)(c->a | 0x40)));
            m_inx(s);
            uint8_t extra = m_lda8(s, absy8(s, 0xA5C1));
            set_absy8(s, 0x1D39, extra);
            if (extra != 0) {
                set_absy8(s, 0x1D39, m_lda8(s, 0x01));
                m_write8(s, dpx(s, 0x8A), m_lda8(s, 0xFE));
                m_lda8(s, (uint8_t)c->y);
                m_write8(s, dpx(s, 0xA1), (uint8_t)c->a);
                m_inx(s);
            }
        }
        m_iny(s);
        m_cmp16(s, c->y, 0x0017);
    } while (c->y != 0x0017);
    m_ldx16(s, (uint16_t)(c->x - 1));
    set_dp16(s, 0x30, c->x);
    m_set_index(s, &c->y, 0x0000);

    /* $CF:E876: sort */
    if (m_lda8(s, dp8(s, 0x30)) != 0) {
        do {
            m_tyx(s);
            do {
                uint8_t a = m_lda8(s, absy8(s, 0x1D8A));
                uint8_t b = m_read8(s, dpx(s, 0x8B));
                m_cmp8(s, a, b);
                if (a < b) {
                    m_push8(s, a);
                    set_absy8(s, 0x1D8A, m_lda8(s, b));
                    uint8_t pulled = m_pull8(s);
                    m_lda8(s, pulled);
                    m_write8(s, dpx(s, 0x8B), pulled);
                    uint8_t sa = m_lda8(s, absy8(s, 0x1DA1));
                    m_push8(s, sa);
                    set_absy8(s, 0x1DA1, m_lda8(s, m_read8(s, dpx(s, 0xA2))));
                    pulled = m_pull8(s);
                    m_lda8(s, pulled);
                    m_write8(s, dpx(s, 0xA2), pulled);
                }
                m_inx(s);
                m_cmp16(s, c->x, dp16(s, 0x30));
            } while (c->x != dp16(s, 0x30));
            m_iny(s);
            m_cmp16(s, c->y, dp16(s, 0x30));
        } while (c->y != dp16(s, 0x30));
    }
    /* $CF:E8A1 */
    set_absy8(s, 0x1DA2, m_lda8(s, 0xFF));
    m_set_index(s, &c->y, 0x0000);
    set_dp16(s, 0xC5, c->y);
    set_dp16(s, 0xC7, c->y);
    set_dp16(s, 0xC3, c->y);
    m_set_index(s, &c->y, 0x0010);
    set_dp16(s, 0xC9, c->y);

    for (;;) {
        /* $CF:E8B4 */
        m_ldx16(s, dp16(s, 0xC3));
        uint8_t entry = m_lda8(s, m_read8(s, dpx(s, 0xA1)));
        if (entry & 0x80)
            break;
        uint8_t slot = m_lda8(s, entry & 0x1F);
        set_dp8(s, 0xC5, slot);
        m_tax(s);
        bool shadow = true;
        if (!(m_lda8(s, m_read8(s, dpx(s, 0x39))) & 0x80)) {
            m_ldx16(s, dp16(s, 0xC5));
            uint8_t parts = m_lda8(s, m_read8(s, dpx(s, 0x73)));
            if (parts != 0) {
                set_dp8(s, 0xC7, parts);
                if (!call(s, 0xCFE8D4, 0xCFEDB8))
                    return;
                m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
                m_set_flag(s, CPU_FLAG_C, false);
                m_lda16(s, dp16(s, 0xC9));
                c->a = m_adc16(s, c->a, 0x0700);
                m_set_index(s, &c->y, c->a);
                m_lda16(s, dp16(s, 0xC5));
                c->a = m_asl16(s, c->a);
                m_tax(s);
                m_lda16(s, m_read16(s, 0xCCF84D + c->x));
                m_tax(s);
                uint16_t n = m_lda16(s, (uint16_t)(dp16(s, 0xC7) + 1));
                for (int i = 0; i < 4; i++)
                    n = m_lsr16(s, n);
                c->a = n;
                set_dp16(s, 0xCD, n);
                if (!call_widths(s, 0xCFE8F1, 0xCFEB94, 0) || !call(s, 0xCFE8F4, 0xCFEAC5)
                    || !call(s, 0xCFE8F7, 0xCFEDF7))
                    return;
                m_ldx16(s, dp16(s, 0xC5));
                if (m_lda8(s, m_read8(s, dpx(s, 0x39))) != 0) {
                    m_write8(s, dpx(s, 0x39), m_lda8(s, 0x80));
                    shadow = false;
                }
            }
        }
        if (shadow && !call(s, 0xCFE906, 0xCFEA8A))
            return;
        /* $CF:E909 */
        if (m_lda8(s, abs8(s, 0xA013)) != 0) {
            m_ldx16(s, dp16(s, 0xC3));
            uint8_t v = m_lda8(s, m_read8(s, dpx(s, 0xA1)));
            m_bit_imm8(s, 0x40);
            if (v & 0x40) {
                m_lda8(s, v & 0x1F);
                m_tax(s);
                if (m_lda8(s, absx8(s, 0xA186)) != 0 && !call(s, 0xCFE91E, 0xCFE92C))
                    return;
            }
        }
        uint8_t next = (uint8_t)(dp8(s, 0xC3) + 1);
        set_dp8(s, 0xC3, next);
        m_set_nz8(s, next);
    }
    /* $CF:E926 */
    m_ldx16(s, 0x0000);
    set_direct_page(s, c->x);
    m_rts(s);
}

// $CF:E781
/* While $AC5A is set, add the next row of the table at $D1:5E10 (or $D1:5E49
 * for objects with $A0DC,Y set), indexed by $A3D2, to the Y byte of each of
 * the four prepared sprites of the 11 objects; a row starting with $80 ends
 * it (clearing $AC5A). */
void battle_step_prepared_sprites(Snes *s)
{
    Cpu *c = &s->cpu;
    if (m_lda8(s, abs8(s, 0xAC5A)) == 0) {
        m_rts(s);
        return;
    }
    m_lda8(s, abs8(s, 0xA3D2));
    m_tax(s);
    uint8_t first = m_lda8(s, m_read8(s, 0xD15E10 + c->x));
    m_cmp8(s, first, 0x80);
    if (first == 0x80) {
        m_write8(s, m_abs(s, 0xAC5A), 0x00);
        m_rts(s);
        return;
    }
    m_lda16(s, c->d);
    m_set_index(s, &c->y, c->a);
    do {
        uint8_t v;
        if (m_lda8(s, absy8(s, 0xA0DC)) == 0)
            v = m_lda8(s, m_read8(s, 0xD15E10 + c->x));
        else
            v = m_lda8(s, m_read8(s, 0xD15E49 + c->x));
        set_absy8(s, 0xA0E7, v);
        m_iny(s);
        m_cmp16(s, c->y, 0x000B);
    } while (c->y != 0x000B);
    uint32_t idx = m_abs(s, 0xA3D2);
    uint8_t next = (uint8_t)(m_read8(s, idx) + 1);
    m_write8(s, idx, next);
    m_set_nz8(s, next);
    m_lda16(s, c->d);
    m_tax(s);
    m_set_index(s, &c->y, c->a);
    do {
        set_dp8(s, 0xC6, m_lda8(s, 0x04));
        do {
            m_set_flag(s, CPU_FLAG_C, false);
            uint8_t y = m_adc8(s, m_lda8(s, absx8(s, 0x922E)), absy8(s, 0xA0E7));
            m_set_a8(s, y);
            set_absx8(s, 0x922E, y);
            for (int i = 0; i < 4; i++)
                m_inx(s);
        } while (dec_dp8(s, 0xC6) != 0);
        m_iny(s);
        m_cmp16(s, c->y, 0x000B);
    } while (c->y != 0x000B);
    m_rts(s);
}

// $CF:EC78
/* Move objects along their scripted paths: for each of the 11 objects with
 * $A15B set and steps left ($A500 counting down from 7), add the path's
 * X/Y deltas ($CC:F494/$CC:F594, path $A517 * 8 + step) to its position
 * ($1D0C/$1D23). */
void battle_step_object_paths(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_set_index(s, &c->y, c->a);
    do {
        if (m_lda8(s, absy8(s, 0xA15B)) != 0 && !(m_lda8(s, absy8(s, 0xA500)) & 0x80)) {
            m_set_flag(s, CPU_FLAG_C, true);
            uint8_t step = m_sbc8(s, m_lda8(s, 0x07), absy8(s, 0xA500));
            m_set_a8(s, step);
            m_tax(s);
            set_dp16(s, 0xC6, c->x);
            m_lda8(s, absy8(s, 0xA517));
            m_rep(s, CPU_FLAG_M);
            for (int i = 0; i < 3; i++)
                c->a = m_asl16(s, c->a);
            m_set_flag(s, CPU_FLAG_C, false);
            c->a = m_adc16(s, c->a, dp16(s, 0xC6));
            m_tax(s);
            m_tdc(s);
            m_sep(s, CPU_FLAG_M);
            m_set_flag(s, CPU_FLAG_C, false);
            uint8_t v = m_adc8(s, m_lda8(s, m_read8(s, 0xCCF494 + c->x)), absy8(s, 0x1D0C));
            m_set_a8(s, v);
            set_absy8(s, 0x1D0C, v);
            m_set_flag(s, CPU_FLAG_C, false);
            v = m_adc8(s, m_lda8(s, m_read8(s, 0xCCF594 + c->x)), absy8(s, 0x1D23));
            m_set_a8(s, v);
            set_absy8(s, 0x1D23, v);
            m_set_flag(s, CPU_FLAG_C, true);
            v = m_sbc8(s, m_lda8(s, absy8(s, 0xA500)), 0x01);
            m_set_a8(s, v);
            set_absy8(s, 0xA500, v);
        }
        m_iny(s);
        m_cmp16(s, c->y, 0x000B);
    } while (c->y != 0x000B);
    m_rts(s);
}

// $CF:ECC2
/* Set the 8 anchor points ($A240/$A250, 16-bit X/Y): for each slot from 7
 * down to 0 whose $A437 is positive, either take the position of the object
 * it names ($1D0C/$1D23) or, with bit 6 set, copy another anchor (low three
 * bits). */
void battle_update_anchor_points(Snes *s)
{
    Cpu *c = &s->cpu;
    m_ldx16(s, 0x0007);
    set_dp16(s, 0xC6, c->x);
    m_sep(s, CPU_FLAG_X);
    do {
        m_set_index(s, &c->x, dp8(s, 0xC6));
        uint8_t v = m_lda8(s, absx8(s, 0xA437));
        if (!(v & 0x80)) {
            if (m_lda8(s, v & 0x40) == 0) {
                m_lda8(s, absx8(s, 0xA437));
                m_set_index(s, &c->y, c->a);
                m_txa(s);
                m_set_a8(s, m_asl8(s, (uint8_t)c->a));
                m_tax(s);
                set_absx8(s, 0xA240, m_lda8(s, absy8(s, 0x1D0C)));
                set_absx8(s, 0xA241, 0x00);
                set_absx8(s, 0xA250, m_lda8(s, absy8(s, 0x1D23)));
                set_absx8(s, 0xA251, 0x00);
            } else {
                uint8_t src = m_lda8(s, (uint8_t)(m_lda8(s, absx8(s, 0xA437)) & 0x07));
                m_set_a8(s, m_asl8(s, src));
                m_set_index(s, &c->y, c->a);
                m_txa(s);
                m_set_a8(s, m_asl8(s, (uint8_t)c->a));
                m_tax(s);
                static const uint16_t cells[4] = { 0xA240, 0xA241, 0xA250, 0xA251 };
                for (int i = 0; i < 4; i++)
                    set_absx8(s, cells[i], m_lda8(s, absy8(s, cells[i])));
            }
        }
    } while (!(dec_dp8(s, 0xC6) & 0x80));
    m_rep(s, CPU_FLAG_X);
    m_rts(s);
}

/* $CF:ED18, one axis: shift the object's position history down one frame and
 * record its position plus the signed offset (`offset`) as a 16-bit value
 * (low bytes `low0`..`low3`, high bytes `high0`..`high3`). */
static void record_axis(Snes *s, const uint16_t low[4], const uint16_t high[4], uint16_t offset, uint16_t pos)
{
    for (int i = 3; i > 0; i--)
        set_absx8(s, low[i], m_lda8(s, absx8(s, low[i - 1])));
    for (int i = 3; i > 0; i--)
        set_absx8(s, high[i], m_lda8(s, absx8(s, high[i - 1])));
    set_absx8(s, high[0], 0x00);
    uint8_t off = m_lda8(s, absx8(s, offset));
    if (off & 0x80) {
        uint8_t h = (uint8_t)(absx8(s, high[0]) - 1);
        set_absx8(s, high[0], h);
        m_set_nz8(s, h);
    }
    m_set_flag(s, CPU_FLAG_C, false);
    uint8_t v = m_adc8(s, off, absx8(s, pos));
    m_set_a8(s, v);
    set_absx8(s, low[0], v);
    v = m_adc8(s, m_lda8(s, absx8(s, high[0])), 0x00);
    m_set_a8(s, v);
    set_absx8(s, high[0], v);
}

// $CF:ED18
/* Record the trail of the 11 objects with trails ($A192): every $AB67 frames
 * ($AB72 counting down), shift their last four positions and store the
 * current one, offset by $A4A4/$A4AF, as 16-bit X ($AB93.., $AC64..) and Y
 * ($AB9E.., $AC6F..). */
void battle_record_object_trails(Snes *s)
{
    Cpu *c = &s->cpu;
    static const uint16_t x_low[4] = { 0xAB93, 0xABA9, 0xABBF, 0xABD5 };
    static const uint16_t x_high[4] = { 0xAC64, 0xAC7A, 0xAC90, 0xACA6 };
    static const uint16_t y_low[4] = { 0xAB9E, 0xABB4, 0xABCA, 0xABE0 };
    static const uint16_t y_high[4] = { 0xAC6F, 0xAC85, 0xAC9B, 0xACB1 };
    m_ldx16(s, 0x000A);
    m_sep(s, CPU_FLAG_X);
    do {
        if (m_lda8(s, absx8(s, 0xA192)) != 0) {
            uint8_t count = (uint8_t)(absx8(s, 0xAB72) - 1);
            set_absx8(s, 0xAB72, count);
            m_set_nz8(s, count);
            if (count == 0) {
                set_absx8(s, 0xAB72, m_lda8(s, absx8(s, 0xAB67)));
                record_axis(s, x_low, x_high, 0xA4A4, 0x1D0C);
                record_axis(s, y_low, y_high, 0xA4AF, 0x1D23);
            }
        }
        m_set_index(s, &c->x, (uint16_t)(c->x - 1));
    } while (!(c->x & 0x80));
    m_rep(s, CPU_FLAG_X);
    m_rts(s);
}

// $CF:E7D7
/* Animate the palettes of the three party members' effects: for each of
 * slots 2-0 with $A46D set (and $A101 clear), copy the next color of its
 * palette cycle ($CE:0100 through $CC:F754 by $A441, position $A499 stepping by
 * 2 up to $7C) into both copies of color 1 of its sprite palette ($2302/$2318
 * + palette $993C * 16), unless the palette is 8. */
void battle_cycle_party_palettes(Snes *s)
{
    Cpu *c = &s->cpu;
    m_set_index(s, &c->y, 0x0002);
    set_dp16(s, 0xC6, c->y);
    m_sep(s, CPU_FLAG_X);
    do {
        m_set_index(s, &c->y, m_x8(s) ? dp8(s, 0xC6) : dp16(s, 0xC6));
        if (m_lda8(s, absy8(s, 0xA46D)) == 0 || m_lda8(s, abs8(s, 0xA101)) != 0)
            continue;
        m_rep(s, CPU_FLAG_X);
        m_set_a8(s, m_asl8(s, m_lda8(s, absy8(s, 0xA441))));
        m_tax(s);
        m_set_flag(s, CPU_FLAG_C, false);
        m_lda8(s, absy8(s, 0xA499));
        m_rep(s, CPU_FLAG_M);
        c->a = m_adc16(s, c->a, m_read16(s, 0xCCF754 + c->x));
        m_tax(s);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        uint8_t palette = m_lda8(s, absy8(s, 0x993C));
        m_cmp8(s, palette, 0x08);
        if (palette == 0x08)
            continue;
        for (int i = 0; i < 4; i++)
            palette = m_asl8(s, palette);
        m_set_a8(s, palette);
        m_set_index(s, &c->y, c->a);
        uint8_t lo = m_lda8(s, m_read8(s, 0xCE0100 + c->x));
        set_absy8(s, 0x2302, lo);
        set_absy8(s, 0x2318, lo);
        uint8_t hi = m_lda8(s, m_read8(s, 0xCE0101 + c->x));
        set_absy8(s, 0x2303, hi);
        set_absy8(s, 0x2319, hi);
        m_ldx16(s, dp16(s, 0xC6));
        for (int i = 0; i < 2; i++) {
            uint8_t pos = (uint8_t)(absx8(s, 0xA499) + 1);
            set_absx8(s, 0xA499, pos);
            m_set_nz8(s, pos);
        }
        uint8_t pos = m_lda8(s, absx8(s, 0xA499));
        m_cmp8(s, pos, 0x7C);
        if (pos == 0x7C)
            set_absx8(s, 0xA499, 0x00);
        m_sep(s, CPU_FLAG_X);
    } while (!(dec_dp8(s, 0xC6) & 0x80));
    m_rep(s, CPU_FLAG_X);
    m_rts(s);
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

