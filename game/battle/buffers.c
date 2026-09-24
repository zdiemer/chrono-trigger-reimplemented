/* Battle: resetting per-frame work buffers. */
#include "battle/entries.h"

#include "port/machine.h"

// $C1:1C3A
/* Reset the 384-byte table at $0B40 from its initial contents at $D1:5800. */
void battle_reset_0b40_table(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_set_index(s, &c->x, c->a);
    do {
        uint8_t v = m_lda8(s, m_read8(s, 0xD15800 + c->x));
        m_write8(s, m_abs(s, 0x0B40) + c->x, v);
        m_set_index(s, &c->x, (uint16_t)(c->x + 1));
        m_cmp16(s, c->x, 0x0180);
    } while (c->x != 0x0180);
    m_rts(s);
}

/* LDX #last : (writes) : DEX : BPL -- run `body` for X = last down to 0. */
static void for_each_down(Snes *s, uint16_t last, void (*body)(Snes *))
{
    m_ldx16(s, last);
    do {
        body(s);
        m_ldx16(s, (uint16_t)(s->cpu.x - 1));
    } while (!(s->cpu.x & 0x8000));
}

static void stz_x(Snes *s, uint16_t table) { m_write8(s, m_abs(s, table) + s->cpu.x, 0x00); }
static void clear_5d80(Snes *s) { stz_x(s, 0x5D80); }
static void clear_a240(Snes *s) { stz_x(s, 0xA240); }
static void clear_a124(Snes *s) { stz_x(s, 0xA124); }
static void reset_object_slot(Snes *s)
{
    m_write8(s, m_abs(s, 0xA014) + s->cpu.x, (uint8_t)s->cpu.a);
    stz_x(s, 0xA3D4);
    stz_x(s, 0xA4A4);
    stz_x(s, 0xA4AF);
    stz_x(s, 0xA0DC);
}
static void clear_a1a8(Snes *s) { stz_x(s, 0xA1A8); }
static void clear_a337(Snes *s) { stz_x(s, 0xA337); }
static void clear_palette_slot(Snes *s)
{
    stz_x(s, 0xA3B9);
    stz_x(s, 0xA3BD);
    stz_x(s, 0xA4E8);
}
static void clear_a3df(Snes *s) { stz_x(s, 0xA3DF); }
static void fill_a_x(Snes *s, uint16_t table) { m_write8(s, m_abs(s, table) + s->cpu.x, (uint8_t)s->cpu.a); }
static void fill_a437(Snes *s) { fill_a_x(s, 0xA437); }
static void fill_ab93(Snes *s) { fill_a_x(s, 0xAB93); }

// $CC:F06B
/* Reset the battle's object, effect, anchor, palette and trail state. Returns
 * with RTL. */
void battle_reset_object_state(Snes *s)
{
    for_each_down(s, 0x008C, clear_5d80);
    for_each_down(s, 0x003F, clear_a240);
    for_each_down(s, 0x0083, clear_a124);
    m_ldx16(s, 0x000A);
    m_lda8(s, 0xFF);
    do {
        reset_object_slot(s);
        m_ldx16(s, (uint16_t)(s->cpu.x - 1));
    } while (!(s->cpu.x & 0x8000));
    for_each_down(s, 0x003F, clear_a1a8);
    for_each_down(s, 0x0071, clear_a337);
    for_each_down(s, 0x0003, clear_palette_slot);
    m_write8(s, m_abs(s, 0xA4EC), 0x00);
    m_write8(s, m_abs(s, 0xA4ED), 0x00);
    for_each_down(s, 0x0057, clear_a3df);
    m_ldx16(s, 0x0007);
    m_lda8(s, 0xFF);
    do {
        fill_a437(s);
        m_ldx16(s, (uint16_t)(s->cpu.x - 1));
    } while (!(s->cpu.x & 0x8000));
    m_ldx16(s, 0x0057);
    m_lda8(s, 0xF0);
    do {
        fill_ab93(s);
        m_ldx16(s, (uint16_t)(s->cpu.x - 1));
    } while (!(s->cpu.x & 0x8000));
    static const uint16_t cells[10] = { 0xA3D1, 0x9931, 0x9876, 0xA21F, 0xA012, 0xAAFB, 0xAAFC, 0xAAFD, 0xAC5A, 0xA2B7 };
    for (int i = 0; i < 10; i++)
        m_write8(s, m_abs(s, cells[i]), 0x00);
    m_rtl(s);
}
