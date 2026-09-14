/* Palette animation, run every frame in every game mode.
 *
 * Twelve animation slots of 12 bytes at $0520 (the routines run with the
 * direct page at $0500): +0 type (high nibble) and step count (low nibble),
 * +1 first color, +2 color count, +3 step, +4/+5 frame delay counter and
 * period, and type-specific bytes after. Colors are read from the palette
 * copy at $7E:2000 and written to the working palette at $7E:2200. */
#include "core/palette.h"

#include "port/machine.h"

static uint8_t absy8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.y); }
static void set_absy8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.y, v); }
static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }

/* JSR out; false if the callee didn't come back with 8-bit A and 16-bit X. */
static bool call(Snes *s, uint32_t site, uint32_t target)
{
    m_jsr(s, site, target);
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M;
}

/* INC/DEC A (8-bit) */
static uint8_t step_a8(Snes *s, int delta) { return m_lda8(s, (uint8_t)((uint8_t)s->cpu.a + delta)); }

/* STA $4203 : LDA dp : CLC : ADC $4217 -- the component plus the high byte of
 * (the step's factor in $4202) * `distance`. */
static uint8_t add_scaled(Snes *s, uint8_t cell, uint8_t distance)
{
    m_write8(s, m_abs(s, 0x4203), distance);
    uint8_t v = m_lda8(s, dp8(s, cell));
    m_set_flag(s, CPU_FLAG_C, false);
    return m_adc8(s, v, m_read8(s, m_abs(s, 0x4217)));
}

// $FD:E8D7
/* Fade sub-mode 0: move each of the slot's colors (X = first color * 2) toward
 * white by the factor in $4202 / 256, component by component, using the
 * hardware multiplier. */
void palette_fade_toward_white(Snes *s)
{
    Cpu *c = &s->cpu;
    m_lda8(s, absy8(s, 0x0522));
    m_set_index(s, &c->y, c->a);
    do {
        uint8_t blue = m_lda8(s, (uint8_t)(m_read8(s, 0x7E2001 + c->x) & 0x7C));
        set_dp8(s, 0x0D, blue);
        m_lda8(s, (uint8_t)(blue ^ 0x7C));
        m_set_a8(s, add_scaled(s, 0x0D, (uint8_t)c->a));
        m_xba(s);
        uint8_t red = m_lda8(s, (uint8_t)(m_read8(s, 0x7E2000 + c->x) & 0x1F));
        set_dp8(s, 0x0C, red);
        m_lda8(s, (uint8_t)(red ^ 0x1F));
        m_set_a8(s, add_scaled(s, 0x0C, (uint8_t)c->a));
        m_rep(s, CPU_FLAG_M);
        m_write16(s, m_dp(s, 0x0C), m_lda16(s, c->a & 0x7C1F));
        uint16_t g = m_lda16(s, (uint16_t)(m_read16(s, 0x7E2000 + c->x) & 0x03E0));
        g = m_lsr16(s, g);
        c->a = m_lsr16(s, g);
        m_sep(s, CPU_FLAG_M);
        uint8_t green = (uint8_t)c->a;
        set_dp8(s, 0x0E, green);
        m_lda8(s, (uint8_t)(green ^ 0xF8));
        m_set_a8(s, add_scaled(s, 0x0E, (uint8_t)c->a));
        m_lda8(s, (uint8_t)(c->a & 0xF8));
        m_rep(s, CPU_FLAG_M);
        c->a = m_asl16(s, c->a);
        c->a = m_asl16(s, c->a);
        uint16_t color = m_lda16(s, (uint16_t)(c->a | m_read16(s, m_dp(s, 0x0C))));
        m_write16(s, 0x7E2200 + c->x, color);
        m_sep(s, CPU_FLAG_M);
        m_inx(s);
        m_inx(s);
        m_set_index(s, &c->y, (uint16_t)(c->y - 1));
    } while (c->y != 0);
    m_rts(s);
}

// $FD:E82C
/* Animation type 4, fading: step the slot's level (+7) by its step (+3)
 * toward its target (+8), ending the animation (type 0) when it gets there, or
 * take the level straight from +5 when that's negative. Then load the level
 * into the multiplier and apply the slot's fade sub-mode (+6). Fading down past
 * zero ends the animation and restores the slot's colors unchanged. */
void palette_animate_fade(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push16(s, c->y);
    uint8_t level = m_lda8(s, absy8(s, 0x0525));
    if (!(level & 0x80)) {
        level = m_lda8(s, absy8(s, 0x0527));
        uint8_t target = absy8(s, 0x0528);
        m_cmp8(s, level, target);
        bool done;
        if (level < target) {
            level = m_adc8(s, level, absy8(s, 0x0523));
            m_set_a8(s, level);
            done = m_flag(s, CPU_FLAG_C);
            if (!done) {
                m_cmp8(s, level, absy8(s, 0x0528));
                done = level >= absy8(s, 0x0528);
            }
        } else {
            level = m_sbc8(s, level, absy8(s, 0x0523));
            m_set_a8(s, level);
            if (!m_flag(s, CPU_FLAG_C)) {
                /* $FD:E8AC: restore the colors and stop */
                set_absy8(s, 0x0520, m_lda8(s, 0x00));
                m_lda8(s, absy8(s, 0x0521));
                m_rep(s, CPU_FLAG_M);
                c->a = m_asl16(s, c->a);
                m_tax(s);
                m_lda16(s, 0x0000);
                m_sep(s, CPU_FLAG_M);
                m_lda8(s, absy8(s, 0x0522));
                m_set_index(s, &c->y, c->a);
                m_rep(s, CPU_FLAG_M);
                do {
                    m_write16(s, 0x7E2200 + c->x, m_lda16(s, m_read16(s, 0x7E2000 + c->x)));
                    m_inx(s);
                    m_inx(s);
                    m_set_index(s, &c->y, (uint16_t)(c->y - 1));
                } while (c->y != 0);
                m_lda16(s, 0x0000);
                m_sep(s, CPU_FLAG_M);
                c->y = m_pull16(s);
                m_set_nz16(s, c->y);
                m_rts(s);
                return;
            }
            m_cmp8(s, level, absy8(s, 0x0528));
            done = level < absy8(s, 0x0528);
        }
        if (done) {
            set_absy8(s, 0x0520, m_lda8(s, 0x00));
            level = m_lda8(s, absy8(s, 0x0528));
        }
    }
    set_absy8(s, 0x0527, level);
    m_write8(s, m_abs(s, 0x4202), m_lda8(s, absy8(s, 0x0527)));
    m_lda8(s, absy8(s, 0x0521));
    m_rep(s, CPU_FLAG_M);
    c->a = m_asl16(s, c->a);
    m_tax(s);
    m_lda16(s, 0x0000);
    m_sep(s, CPU_FLAG_M);

    static const struct { uint32_t site, target; } modes[7] = {
        { 0xFDE87A, 0xFDE8D7 }, { 0xFDE882, 0xFDE962 }, { 0xFDE88A, 0xFDE98F }, { 0xFDE892, 0xFDEA5C },
        { 0xFDE89A, 0xFDE935 }, { 0xFDE8A2, 0xFDE9CA }, { 0xFDE8A7, 0xFDEA11 },
    };
    uint8_t mode = m_lda8(s, absy8(s, 0x0526));
    int i = 0;
    if (mode != 0) {
        do {
            mode = step_a8(s, -1);
            i++;
        } while (mode != 0 && i < 5);
        if (mode != 0)
            i = 6;   /* sub-modes 6 and up share the last handler */
    }
    if (!call(s, modes[i].site, modes[i].target))
        return;
    c->y = m_pull16(s);
    m_set_nz16(s, c->y);
    m_rts(s);
}

// $FD:E39C
/* Run the palette animations: for each of the 12 slots, count its frame delay
 * (+4 up to +5) and when it's due, advance the step counter of types 1 and 8
 * (+3, wrapping after the low nibble's count) and run the type's handler. */
void palette_animate(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_push16(s, c->d);
    m_tdc(s);
    m_push8(s, (uint8_t)c->a);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_ldx16(s, 0x0500);
    m_push16(s, c->x);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    m_lda8(s, 0x00);
    m_xba(s);
    m_set_index(s, &c->y, 0x0000);

    static const struct { uint32_t site, target; } handlers[8] = {
        { 0xFDE3ED, 0xFDE437 }, { 0xFDE3F5, 0xFDE532 }, { 0xFDE3FD, 0xFDE4E8 }, { 0xFDE405, 0xFDE82C },
        { 0xFDE40D, 0xFDE5A4 }, { 0xFDE415, 0xFDE807 }, { 0xFDE41D, 0xFDE57C }, { 0xFDE422, 0xFDE485 },
    };
    for (;;) {
        /* $FD:E3AC */
        uint8_t delay = m_lda8(s, (uint8_t)(absy8(s, 0x0524) + 1));
        set_absy8(s, 0x0524, delay);
        uint8_t period = absy8(s, 0x0525);
        m_cmp8(s, delay, period);
        if (delay == period) {
            set_absy8(s, 0x0524, m_lda8(s, 0x00));
            m_lda8(s, absy8(s, 0x0520));
            uint8_t kind = m_lda8(s, (uint8_t)(c->a & 0xF0));
            m_cmp8(s, kind, 0x10);
            if (kind != 0x10)
                m_cmp8(s, kind, 0x80);
            if (kind == 0x10 || kind == 0x80) {
                uint8_t step = m_lda8(s, (uint8_t)(absy8(s, 0x0523) + 1));
                set_absy8(s, 0x0523, step);
                m_lda8(s, absy8(s, 0x0520));
                uint8_t count = m_lda8(s, (uint8_t)((c->a & 0x0F) + 1));
                uint8_t cur = absy8(s, 0x0523);
                m_cmp8(s, count, cur);
                if (count == cur)
                    set_absy8(s, 0x0523, m_lda8(s, 0x00));
            }
            uint8_t type = m_lda8(s, absy8(s, 0x0520));
            for (int i = 0; i < 4; i++)
                type = m_lsr8(s, type);
            m_set_a8(s, type);
            if (type != 0) {
                int h = 0;
                do {
                    type = step_a8(s, -1);
                    if (type == 0)
                        break;
                    h++;
                } while (h < 7);
                if (!call(s, handlers[h].site, handlers[h].target))
                    return;
            }
        }
        /* $FD:E425 */
        m_lda8(s, 0x00);
        m_xba(s);
        m_lda8(s, (uint8_t)c->y);
        m_set_flag(s, CPU_FLAG_C, false);
        uint8_t next = m_adc8(s, (uint8_t)c->a, 0x0C);
        m_set_a8(s, next);
        m_set_index(s, &c->y, c->a);
        m_cmp8(s, next, 0x90);
        if (next == 0x90)
            break;
    }
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rtl(s);
}
