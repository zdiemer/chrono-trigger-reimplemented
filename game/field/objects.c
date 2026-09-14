/* Field mode: object management. */
#include "field/field.h"

#include "port/machine.h"

/* Loads an 8-bit value into A (keeping B), setting N and Z like LDA. */
static uint8_t lda8(Snes *s, uint8_t v)
{
    m_set_a8(s, v);
    m_set_nz8(s, v);
    return v;
}

static uint8_t ldx8(Snes *s, uint8_t v)
{
    s->cpu.x = v;
    m_set_nz8(s, v);
    return v;
}

/* $C0:A88D
 * Activate or deactivate the current field object (index in $6D) depending on
 * whether its position is inside a window around the screen.
 *
 * The window is the screen bounds at $1D0A/$1D0C (left/right) and $1D0E/$1D10
 * (top/bottom) widened by 4 pixels horizontally, 2 at the top and 5 at the
 * bottom, clamped to 0-255; object positions ($1801,X and $1881,X) are compared
 * doubled. Objects that are disabled ($1100,X negative) or have no sprite
 * ($1A81,X zero or negative) count as outside.
 *
 * Coming into view: mark the object's state bytes active and run its setup
 * routines. Leaving view: clear $0F00,X, run its teardown routines, and set
 * $29 to $0D if the object is flagged in $7F:0A00 and $30 is clear. In both
 * leaving and staying-out cases, a nonzero $7F:0B01,X becomes 2.
 *
 * Returns with carry set if the object is inside the window. Entry: 8-bit A
 * and X, binary mode. X, N and Z are dead after return (liveness), so they
 * aren't reproduced at the final RTS; registers passed to callees are. */
void field_update_object_visibility(Snes *s)
{
    const uint8_t index = m_read8(s, m_dp(s, 0x6D));
    ldx8(s, index);

    bool inside = false;
    if (!(lda8(s, m_read8(s, m_abs(s, 0x1100) + index)) & 0x80)) {
        uint8_t sprite = lda8(s, m_read8(s, m_abs(s, 0x1A81) + index));
        if (sprite != 0 && !(sprite & 0x80)) {
            /* Window bounds, clamped. */
            m_set_flag(s, CPU_FLAG_C, true);
            uint8_t left = m_sbc8(s, m_read8(s, m_abs(s, 0x1D0A)), 4);
            m_set_a8(s, left);
            if (!m_flag(s, CPU_FLAG_C))
                left = lda8(s, 0x00);
            m_write8(s, m_dp(s, 0xC3), left);

            m_set_flag(s, CPU_FLAG_C, false);
            uint8_t right = m_adc8(s, m_read8(s, m_abs(s, 0x1D0C)), 4);
            m_set_a8(s, right);
            if (m_flag(s, CPU_FLAG_C))
                right = lda8(s, 0xFF);
            m_write8(s, m_dp(s, 0xC4), right);

            m_set_flag(s, CPU_FLAG_C, true);
            uint8_t top = m_sbc8(s, m_read8(s, m_abs(s, 0x1D0E)), 2);
            m_set_a8(s, top);
            if (!m_flag(s, CPU_FLAG_C))
                top = lda8(s, 0x00);
            m_write8(s, m_dp(s, 0xC5), top);

            m_set_flag(s, CPU_FLAG_C, false);
            uint8_t bottom = m_adc8(s, m_read8(s, m_abs(s, 0x1D10)), 5);
            m_set_a8(s, bottom);
            if (m_flag(s, CPU_FLAG_C))
                bottom = lda8(s, 0xFF);
            m_write8(s, m_dp(s, 0xC6), bottom);

            ldx8(s, m_read8(s, m_dp(s, 0x6D)));
            uint8_t i = (uint8_t)s->cpu.x;
            uint8_t x2 = m_asl8(s, m_read8(s, m_abs(s, 0x1801) + i));
            m_set_a8(s, x2);
            m_cmp8(s, x2, left);
            if (m_flag(s, CPU_FLAG_C)) {
                m_cmp8(s, x2, right);
                if (!m_flag(s, CPU_FLAG_C)) {
                    uint8_t y2 = m_asl8(s, m_read8(s, m_abs(s, 0x1881) + i));
                    m_set_a8(s, y2);
                    m_cmp8(s, y2, top);
                    if (m_flag(s, CPU_FLAG_C)) {
                        m_cmp8(s, y2, bottom);
                        inside = !m_flag(s, CPU_FLAG_C);
                    }
                }
            }
        }
    }

    uint8_t i = (uint8_t)s->cpu.x;
    if (inside) {
        if (!(lda8(s, m_read8(s, m_abs(s, 0x0F00) + i)) & 0x80)) {
            lda8(s, 0x80);
            m_write8(s, m_abs(s, 0x0E80) + i, 0x80);
            m_write8(s, m_abs(s, 0x0E81) + i, 0x80);
            m_write8(s, m_abs(s, 0x0F00) + i, 0x80);
            lda8(s, 0x00);
            m_write8(s, m_abs(s, 0x1681) + i, 0x00);
            m_write8(s, m_abs(s, 0x1601) + i, 0x00);
            m_write8(s, m_abs(s, 0x1B00) + i, 0x00);
            lda8(s, 0xFF);
            m_write8(s, m_abs(s, 0x0F01) + i, 0xFF);
            m_jsr(s, 0xC0A905, 0xC07170);
            m_jsr(s, 0xC0A908, 0xC06F9A);
            m_jsr(s, 0xC0A90B, 0xC0A9CD);
        }
        m_set_flag(s, CPU_FLAG_C, true);
        m_rts(s);
        return;
    }

    if (lda8(s, m_read8(s, m_abs(s, 0x0F00) + i)) & 0x80) {
        m_write8(s, m_abs(s, 0x0F00) + i, 0x00);
        m_jsr(s, 0xC0A918, 0xC0EA42);
        m_jsr(s, 0xC0A91B, 0xC0734C);
        m_jsr(s, 0xC0A91E, 0xC07056);
        m_jsr(s, 0xC0A921, 0xC0A98A);
        i = ldx8(s, m_read8(s, m_dp(s, 0x6D)));
        if (lda8(s, m_read8(s, 0x7F0A00 + i)) != 0 && lda8(s, m_read8(s, m_dp(s, 0x30))) == 0) {
            lda8(s, 0x0D);
            m_write8(s, m_dp(s, 0x29), 0x0D);
        }
    }
    if (lda8(s, m_read8(s, 0x7F0B01 + i)) != 0) {
        lda8(s, 0x02);
        m_write8(s, 0x7F0B01 + i, 0x02);
    }
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}
