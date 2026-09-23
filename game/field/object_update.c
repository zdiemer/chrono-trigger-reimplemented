/* Field mode: the per-frame object update loop and the per-object steps it
 * runs.
 *
 * Objects are slots X = 0, 2, 4, ... up to twice $7F:2000. $1100,X is an
 * object's state, positions are 16-bit at $1800,X (X) and $1880,X (Y) with
 * the tile in the high byte, velocities $1900,X and $1980,X. $0A00,X and
 * $0A80,X hold the screen position the sprites are drawn at. Objects on
 * screen are kept in doubly linked rows by screen Y, so the sprite builder
 * can draw them in order: $0E00,Y is the first object of row Y ($0A80,X / 4,
 * doubled), $0E80,X and $0E81,X the previous and next object, $0E01,X
 * negative while the object isn't linked. A negative link means none. */
#include "field/object_update.h"

#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }
static uint8_t abs8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr)); }
static uint16_t abs16(Snes *s, uint16_t addr) { return m_read16(s, m_abs(s, addr)); }
static uint8_t obj8(Snes *s, uint16_t table) { return m_read8(s, m_abs(s, table) + s->cpu.x); }
static void set_obj8(Snes *s, uint16_t table, uint8_t v) { m_write8(s, m_abs(s, table) + s->cpu.x, v); }
static uint16_t obj16(Snes *s, uint16_t table) { return m_read16(s, m_abs(s, table) + s->cpu.x); }
static void set_obj16(Snes *s, uint16_t table, uint16_t v) { m_write16(s, m_abs(s, table) + s->cpu.x, v); }

/* INC/DEC table,X on an 8-bit byte, setting N and Z; returns the new value. */
static uint8_t step_obj8(Snes *s, uint16_t table, int delta)
{
    uint8_t v = (uint8_t)(obj8(s, table) + delta);
    set_obj8(s, table, v);
    m_set_nz8(s, v);
    return v;
}

/* SEC : SBC v and CLC : ADC v on the 8-bit accumulator. */
static uint8_t sec_sbc(Snes *s, uint8_t v)
{
    m_set_flag(s, CPU_FLAG_C, true);
    m_set_a8(s, m_sbc8(s, (uint8_t)s->cpu.a, v));
    return (uint8_t)s->cpu.a;
}

static uint8_t clc_adc(Snes *s, uint8_t v)
{
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_a8(s, m_adc8(s, (uint8_t)s->cpu.a, v));
    return (uint8_t)s->cpu.a;
}

/* EOR #$FF : INC A */
static uint8_t negate8(Snes *s, uint8_t v)
{
    uint8_t inverted = m_lda8(s, (uint8_t)~v);
    return m_lda8(s, (uint8_t)(inverted + 1));
}

/* TAY at the current index width. */
static void tay(Snes *s) { m_set_index(s, &s->cpu.y, s->cpu.a); }

/* JSR out, expecting 8-bit A and X back; false if the callee left other
 * widths (the port then stops at the return address). */
static bool call8(Snes *s, uint32_t site, uint32_t target)
{
    m_jsr(s, site, target);
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == (CPU_FLAG_M | CPU_FLAG_X);
}

/* One object of $C0:A810's loop (slot in $6D and X). States below 3 move
 * through $C0:AAFD and return from the loop's own copy of its tail (`simple`);
 * 7 checks the window through $C0:A947; 8 is skipped. Returns false if a
 * callee left unexpected widths. */
static bool update_one(Snes *s, bool *simple)
{
    uint8_t state = m_lda8(s, obj8(s, 0x1100));
    m_cmp8(s, state, 0x03);
    *simple = state < 0x03;
    if (!*simple) {
        m_cmp8(s, state, 0x08);
        if (state == 0x08)
            return true;
        m_cmp8(s, state, 0x07);
        if (state == 0x07)
            return call8(s, 0xC0A86B, 0xC0A947);
    }
    uint32_t site = *simple ? 0xC0A870 : 0xC0A82A;
    if (!call8(s, site, *simple ? 0xC0AAFD : 0xC0AA07) || !call8(s, site + 0x03, 0xC0A88D))
        return false;
    if (!m_flag(s, CPU_FLAG_C))
        return true;
    return call8(s, site + 0x08, 0xC0C98A) && call8(s, site + 0x0B, 0xC0A98A) &&
           call8(s, site + 0x0E, 0xC0AB45) && call8(s, site + 0x11, 0xC0A9CD);
}

// $C0:A810
/* Update every object slot: move it ($C0:AA07, or $C0:AAFD for states below
 * 3), activate or deactivate it by the screen window ($C0:A88D, native), and
 * for objects in view step the animation ($C0:C98A), recompute the screen
 * position ($C0:AB45) and move it to its row ($C0:A98A then $C0:A9CD). Then
 * the objects in $99 and $97, when set and in view, are moved to their rows
 * again, unless the last slot took the states-below-3 path, whose copy of the
 * loop's end returns directly. */
void field_update_objects(Snes *s)
{
    Cpu *c = &s->cpu;
    uint8_t count = m_asl8(s, m_lda8(s, m_read8(s, 0x7F2000)));
    m_set_a8(s, count);
    set_dp8(s, 0xBD, count);
    m_ldx(s, 0x00);
    bool simple;
    do {
        set_dp8(s, 0x6D, (uint8_t)c->x);
        if (!update_one(s, &simple))
            return;
        m_ldx(s, dp8(s, 0x6D));
        m_inx(s);
        m_inx(s);
        m_cmp8(s, (uint8_t)c->x, dp8(s, 0xBD));
    } while (m_flag(s, CPU_FLAG_N));
    if (simple) {
        m_rts(s);
        return;
    }
    static const struct { uint8_t cell; uint32_t site; } extra[2] = { { 0x99, 0xC0A852 }, { 0x97, 0xC0A864 } };
    for (int i = 0; i < 2; i++) {
        if (m_lda8(s, dp8(s, extra[i].cell)) & 0x80)
            continue;
        m_tax(s);
        if (m_lda8(s, obj8(s, 0x0F00)) == 0)
            continue;
        set_dp8(s, 0x6D, (uint8_t)c->x);
        if (!call8(s, extra[i].site, 0xC0A98A) || !call8(s, extra[i].site + 3, 0xC0A9CD))
            return;
    }
    m_rts(s);
}

// $C0:A947
/* For state-7 objects: set $0F00,X to $80 (carry set) when the doubled
 * position ($1801,X, $1881,X) is inside the screen bounds at $1D0A-$1D10
 * widened by 4 pixels on the sides and 5 at the bottom, compared as signed
 * bytes; otherwise clear it (carry clear). Bounds go to $C3-$C6. */
void field_update_object_in_window(Snes *s)
{
    m_lda8(s, abs8(s, 0x1D0A));
    set_dp8(s, 0xC3, sec_sbc(s, 0x04));
    m_lda8(s, abs8(s, 0x1D0C));
    set_dp8(s, 0xC4, clc_adc(s, 0x04));
    set_dp8(s, 0xC5, m_lda8(s, abs8(s, 0x1D0E)));
    m_lda8(s, abs8(s, 0x1D10));
    set_dp8(s, 0xC6, clc_adc(s, 0x05));
    m_ldx(s, dp8(s, 0x6D));

    bool inside = false;
    uint8_t x2 = m_asl8(s, m_lda8(s, obj8(s, 0x1801)));
    m_set_a8(s, x2);
    m_cmp8(s, x2, dp8(s, 0xC3));
    if (!m_flag(s, CPU_FLAG_N)) {
        m_cmp8(s, x2, dp8(s, 0xC4));
        if (m_flag(s, CPU_FLAG_N)) {
            uint8_t y2 = m_asl8(s, m_lda8(s, obj8(s, 0x1881)));
            m_set_a8(s, y2);
            m_cmp8(s, y2, dp8(s, 0xC5));
            if (!m_flag(s, CPU_FLAG_N)) {
                m_cmp8(s, y2, dp8(s, 0xC6));
                inside = m_flag(s, CPU_FLAG_N);
            }
        }
    }
    if (inside)
        set_obj8(s, 0x0F00, m_lda8(s, 0x80));
    else
        set_obj8(s, 0x0F00, 0x00);
    m_set_flag(s, CPU_FLAG_C, inside);
    m_rts(s);
}

/* LDA $0A80,X : LSR A : AND #$FE : TAY -- the object's row. */
static void load_row(Snes *s)
{
    uint8_t v = m_lsr8(s, m_lda8(s, obj8(s, 0x0A80)));
    m_lda8(s, v & 0xFE);
    tay(s);
}

// $C0:A98A
/* Take object $6D out of its row list if it's linked ($0E01,X not negative):
 * the row's first object or the previous object's next link, and the next
 * object's previous link, skip it; its own links and $0E01,X become $80. As in
 * the original, an object with a previous object but no next one writes the
 * previous link of "object $80" ($0F00). Sets 8-bit A and X itself. */
void field_unlink_object_row(Snes *s)
{
    Cpu *c = &s->cpu;
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_ldx(s, dp8(s, 0x6D));
    if (m_lda8(s, obj8(s, 0x0E01)) & 0x80) {
        m_rts(s);
        return;
    }
    if (m_lda8(s, obj8(s, 0x0E80)) & 0x80) {
        load_row(s);
        uint8_t next = m_lda8(s, obj8(s, 0x0E81));
        m_write8(s, m_abs(s, 0x0E00) + c->y, next);
        if (!(next & 0x80)) {
            tay(s);
            m_lda8(s, 0x80);
            m_write8(s, m_abs(s, 0x0E80) + c->y, 0x80);
        }
        set_obj8(s, 0x0E81, (uint8_t)c->a);
        set_obj8(s, 0x0E01, (uint8_t)c->a);
        m_rts(s);
        return;
    }
    /* $C0:A9B4 */
    tay(s);
    m_xba(s);
    uint8_t next = m_lda8(s, obj8(s, 0x0E81));
    m_write8(s, m_abs(s, 0x0E81) + c->y, next);
    tay(s);
    m_xba(s);
    m_write8(s, m_abs(s, 0x0E80) + c->y, (uint8_t)c->a);
    m_lda8(s, 0x80);
    set_obj8(s, 0x0E80, 0x80);
    set_obj8(s, 0x0E81, 0x80);
    set_obj8(s, 0x0E01, 0x80);
    m_rts(s);
}

// $C0:A9CD
/* Put object $6D at the front of its row list ($0A80,X) if it isn't linked
 * ($0E01,X negative): it becomes the row's first object, linked before the old
 * first one, and $0E01,X is cleared. Uses $C5. Sets 8-bit A and X itself. */
void field_link_object_row(Snes *s)
{
    Cpu *c = &s->cpu;
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_ldx(s, dp8(s, 0x6D));
    if (!(m_lda8(s, obj8(s, 0x0E01)) & 0x80)) {
        m_rts(s);
        return;
    }
    load_row(s);
    if (m_lda8(s, m_read8(s, m_abs(s, 0x0E00) + c->y)) & 0x80) {
        m_txa(s);
        m_write8(s, m_abs(s, 0x0E00) + c->y, (uint8_t)c->a);
        set_obj8(s, 0x0E01, 0x00);
        m_lda8(s, 0x80);
        set_obj8(s, 0x0E80, 0x80);
        set_obj8(s, 0x0E81, 0x80);
        m_rts(s);
        return;
    }
    /* $C0:A9F2 */
    set_dp8(s, 0xC5, (uint8_t)c->y);
    tay(s);
    m_txa(s);
    m_write8(s, m_abs(s, 0x0E80) + c->y, (uint8_t)c->a);
    m_lda8(s, (uint8_t)c->y);
    set_obj8(s, 0x0E81, (uint8_t)c->a);
    m_ldy(s, dp8(s, 0xC5));
    m_txa(s);
    m_write8(s, m_abs(s, 0x0E00) + c->y, (uint8_t)c->a);
    set_obj8(s, 0x0E01, 0x00);
    m_rts(s);
}

/* One axis of $C0:AB45: the position in pixels (/16) minus the screen origin
 * in `origin`, as a 9-bit value into `out`,X. */
static void screen_axis(Snes *s, uint16_t pos, uint8_t origin, uint16_t out)
{
    Cpu *c = &s->cpu;
    m_lda16(s, obj16(s, pos));
    for (int i = 0; i < 4; i++)
        c->a = m_lsr16(s, c->a);
    m_set_flag(s, CPU_FLAG_C, true);
    c->a = m_sbc16(s, c->a, dp16(s, origin));
    if (!m_flag(s, CPU_FLAG_C))
        m_lda16(s, (uint16_t)(c->a | 0x0100));
    set_obj16(s, out, m_lda16(s, (uint16_t)(c->a & 0x01FF)));
}

// $C0:AB45
/* Object $6D's screen position: its position in pixels minus the screen
 * origin (screen tile at $1D0A/$1D0E times 8 plus the low 3 bits of
 * $1D87/$1D89, in $DD/$DF), 9 bits, into $0A00,X and $0A80,X. */
void field_update_screen_position(Snes *s)
{
    Cpu *c = &s->cpu;
    m_rep(s, CPU_FLAG_M);
    m_ldx(s, dp8(s, 0x6D));
    set_dp16(s, 0xD9, m_lda16(s, (uint16_t)(abs16(s, 0x1D87) & 0x0007)));
    set_dp16(s, 0xDB, m_lda16(s, (uint16_t)(abs16(s, 0x1D89) & 0x0007)));
    static const struct { uint16_t tile; uint8_t fine, out; } origins[2] = { { 0x1D0A, 0xD9, 0xDD }, { 0x1D0E, 0xDB, 0xDF } };
    for (int i = 0; i < 2; i++) {
        m_lda16(s, (uint16_t)(abs16(s, origins[i].tile) & 0x00FF));
        for (int k = 0; k < 3; k++)
            c->a = m_asl16(s, c->a);
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, dp16(s, origins[i].fine));
        set_dp16(s, origins[i].out, c->a);
    }
    screen_axis(s, 0x1800, 0xDD, 0x0A00);
    screen_axis(s, 0x1880, 0xDF, 0x0A80);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}
