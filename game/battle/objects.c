/* Battle: per-object state updated each battle frame. */
#include "battle/entries.h"

#include "port/machine.h"

static uint8_t absx8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.x); }
static void set_absx8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.x, v); }

/* SEC or CLC, LDA base,X, then SBC or ADC size,X: a bound that doesn't cross
 * the screen edge. A positive base clamps to 0 when subtracting; a negative
 * base (past the right or bottom edge) clamps to $FF when adding. */
static uint8_t clamped_bound(Snes *s, uint16_t base, uint16_t size, bool subtract)
{
    m_set_flag(s, CPU_FLAG_C, subtract);
    uint8_t v = m_lda8(s, absx8(s, base));
    uint8_t r;
    if (subtract) {
        r = m_sbc8(s, v, absx8(s, size));
        m_set_a8(s, r);
        if (!(v & 0x80) && (r & 0x80)) {
            m_tdc(s);
            r = (uint8_t)s->cpu.a;
        }
    } else {
        r = m_adc8(s, v, absx8(s, size));
        m_set_a8(s, r);
        if ((v & 0x80) && !(r & 0x80))
            r = m_lda8(s, 0xFF);
    }
    return r;
}

static void object_bounds(Snes *s)
{
    set_absx8(s, 0x975A, clamped_bound(s, 0xA039, 0x971E, true));
    set_absx8(s, 0x9770, clamped_bound(s, 0xA039, 0x971E, false));
    set_absx8(s, 0x9765, clamped_bound(s, 0xA050, 0x9729, true));
    uint8_t flags = m_lda8(s, m_read8(s, m_abs(s, 0x2989)));
    if (m_lda8(s, flags & 0x04) != 0) {
        set_absx8(s, 0x977B, m_lda8(s, absx8(s, 0xA050)));
    } else {
        m_set_flag(s, CPU_FLAG_C, false);
        uint8_t v = m_adc8(s, m_lda8(s, absx8(s, 0xA050)), 0x08);
        m_set_a8(s, v);
        set_absx8(s, 0x977B, v);
    }
}

// $C1:285A
/* Compute object X's screen bounds from its position ($A039/$A050) and half
 * sizes ($971E/$9729): left $975A, right $9770, top $9765 and bottom $977B
 * (the Y position, plus 8 unless $2989 bit 2 is set). */
void battle_update_object_bounds(Snes *s)
{
    object_bounds(s);
    m_rts(s);
}

// $C1:283D
/* For each of the 11 objects present ($96F5 + slot), copy its position
 * ($1D0C/$1D23 + slot) to $A039/$A050 and compute its bounds. */
void battle_update_all_object_bounds(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    do {
        if (m_lda8(s, absx8(s, 0x96F5)) != 0) {
            set_absx8(s, 0xA039, m_lda8(s, absx8(s, 0x1D0C)));
            set_absx8(s, 0xA050, m_lda8(s, absx8(s, 0x1D23)));
            m_jsr_stack_bytes(s, 0x2852);   /* JSR $285A */
            object_bounds(s);
        }
        m_inx(s);
        m_cmp16(s, c->x, 0x000B);
    } while (c->x != 0x000B);
    m_rts(s);
}

// $C1:423A
/* The head of the battle animation scheduler: count down each object's
 * animation delay ($A12F + slot), then look for the next object, round-robin
 * from $A21F, that is animating ($A124) with its delay run out. Returns if
 * there is none; otherwise continues at $C1:4269 with X = that object. */
void battle_find_due_animation(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    do {
        uint8_t delay = m_lda8(s, absx8(s, 0xA12F));
        if (delay != 0) {
            delay = (uint8_t)(delay - 1);
            set_absx8(s, 0xA12F, delay);
            m_set_nz8(s, delay);
        }
        m_inx(s);
        m_cmp16(s, c->x, 0x000B);
    } while (c->x != 0x000B);
    m_tdc(s);
    m_set_index(s, &c->y, c->a);
    m_lda8(s, m_read8(s, m_abs(s, 0xA21F)));
    m_tax(s);
    do {
        if (m_lda8(s, absx8(s, 0xA124)) != 0 && m_lda8(s, absx8(s, 0xA12F)) == 0) {
            c->pc = 0x4269;
            return;
        }
        m_inx(s);
        m_cmp16(s, c->x, 0x000B);
        if (c->x == 0x000B) {
            m_tdc(s);
            m_tax(s);
        }
        m_iny(s);
        m_cmp16(s, c->y, 0x000B);
    } while (c->y != 0x000B);
    m_rts(s);
}

// $C1:4212
/* For each of the six animation slots with a position mode ($A4E8 + slot,
 * mode = value - 1), compute the slot's position through the handler for the
 * mode ($C1:762E, table $C1:79D3; result in $A2B0/$A2B2) and store it at
 * $5DA0 + slot * 2. */
void battle_update_animation_positions(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    m_write16(s, m_dp(s, 0x90), c->x);
    uint8_t slot;
    do {
        m_ldx16(s, m_read16(s, m_dp(s, 0x90)));
        uint8_t mode = m_lda8(s, absx8(s, 0xA4E8));
        if (mode != 0) {
            m_lda8(s, (uint8_t)(mode - 1));
            /* JSR $762E: PHY : PHX : TAY : ASL : TAX : JSR ($79D3,X) : PLX : PLY : RTS */
            m_push16(s, 0x4220);
            m_push16(s, c->y);
            m_push16(s, c->x);
            m_set_index(s, &c->y, c->a);
            m_set_a8(s, m_asl8(s, (uint8_t)c->a));
            m_tax(s);
            m_jsr(s, 0xC17633, 0xC10000 | m_jump_table(s, 0xC1, 0x79D3, c->x));
            if (c->e || (c->p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
                return;
            c->x = m_pull16(s);
            m_set_nz16(s, c->x);
            c->y = m_pull16(s);
            m_set_nz16(s, c->y);
            m_pull16(s);
            m_set_a8(s, m_asl8(s, m_lda8(s, m_read8(s, m_dp(s, 0x90)))));
            m_tax(s);
            set_absx8(s, 0x5DA0, m_lda8(s, m_read8(s, m_abs(s, 0xA2B0))));
            set_absx8(s, 0x5DA1, m_lda8(s, m_read8(s, m_abs(s, 0xA2B2))));
        }
        slot = (uint8_t)(m_read8(s, m_dp(s, 0x90)) + 1);
        m_write8(s, m_dp(s, 0x90), slot);
        m_set_nz8(s, slot);
        slot = m_lda8(s, m_read8(s, m_dp(s, 0x90)));
        m_cmp8(s, slot, 0x06);
    } while (slot != 0x06);
    m_rts(s);
}

