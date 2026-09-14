/* Field mode: the event script engine. */
#include "field/field.h"

#include "port/machine.h"

/* True if P has the widths the engine's own code assumes between commands:
 * 8-bit accumulator, 16-bit index registers. */
static bool engine_widths(const Snes *s)
{
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M;
}

/* $C0:5A93
 * Run the event script of the object in slot X (8-bit) for this frame: up to
 * $68 commands starting at the offset in $1180,X, continuing while handlers
 * return with carry set. A handler returning carry clear ends the object's
 * turn, leaving X at the offset to resume from, which is stored back to
 * $1180,X. The slot is kept in $6D (only its low byte is written here) and the
 * countdown in $67.
 *
 * Entry: 8-bit A and X; returns with both 8-bit. X, N, Z and C are dead after
 * return (liveness). */
void field_run_object_script(Snes *s)
{
    m_lda8(s, m_read8(s, m_dp(s, 0x68)));
    m_write8(s, m_dp(s, 0x67), (uint8_t)s->cpu.a);
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, m_read16(s, m_abs(s, 0x1180) + s->cpu.x));
    m_write8(s, m_dp(s, 0x6D), (uint8_t)s->cpu.x);
    m_rep(s, CPU_FLAG_X);
    m_ldx16(s, s->cpu.a);
    m_sep(s, CPU_FLAG_M);

    /* $C0:5AA3 */
    for (;;) {
        m_lda8(s, m_read8(s, 0x7F2001 + s->cpu.x));
        s->cpu.y = s->cpu.x;
        m_set_nz16(s, s->cpu.y);
        m_rep(s, CPU_FLAG_M);
        uint16_t op = s->cpu.a = m_asl16(s, m_lda16(s, s->cpu.a & 0x00FF));
        m_ldx16(s, op);
        m_sep(s, CPU_FLAG_M);
        uint16_t handler = m_read16(s, 0xC00000 | (uint16_t)(0x5D6E + op));
        m_jsr(s, 0xC05AB1, 0xC00000 | handler);
        if (!engine_widths(s)) {
            s->cpu.pb = 0xC0;
            s->cpu.pc = 0x5AB4;
            return;
        }
        if (!m_flag(s, CPU_FLAG_C))
            break;
        uint8_t left = (uint8_t)(m_read8(s, m_dp(s, 0x67)) - 1);
        m_write8(s, m_dp(s, 0x67), left);
        m_set_nz8(s, left);
        if (left == 0)
            break;
    }

    /* $C0:5ABA */
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, s->cpu.x);
    m_ldx16(s, m_read16(s, m_dp(s, 0x6D)));
    m_write16(s, m_abs(s, 0x1180) + s->cpu.x, s->cpu.a);
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_rts(s);
}

/* $C0:58DE (inside the map setup routine)
 * Run every field object's init script: the part of each object's event script
 * up to its first zero command byte.
 *
 * $7F:2000 holds the object count; object slots are 2 bytes wide. For each
 * slot $1B01,X and $1B80,X are cleared, and if the object is enabled ($1100,X
 * non-negative) its script, starting at the offset in $1180,X into the script
 * buffer at $7F:2001, is executed one command at a time through the 256-entry
 * handler table at $C0:5D6E. Handlers get the command's offset in Y (X holds
 * the doubled command byte) and return the next command's offset in X. The
 * offset just past the terminating zero is stored back to $1180,X, so the
 * per-frame executor (field_run_object_script) carries on from there. The
 * current slot is kept in $6D and the end slot in $BD.
 *
 * Handlers can take several frames (fades, waits); they run as call-outs.
 * Continues at $C0:9DC3 rather than returning. Entry: 8-bit A, 16-bit X. */
void field_run_object_init_scripts(Snes *s)
{
    m_lda8(s, m_read8(s, 0x7F2000));
    m_rep(s, CPU_FLAG_M);
    uint16_t end = s->cpu.a = m_asl16(s, m_lda16(s, s->cpu.a & 0x00FF));
    m_write16(s, m_dp(s, 0xBD), end);

    m_ldx16(s, 0x0000);
    do {
        const uint16_t slot = s->cpu.x;
        m_write16(s, m_abs(s, 0x1B01) + slot, 0x0000);
        m_write16(s, m_abs(s, 0x1B80) + slot, 0x0000);
        if (!(m_lda16(s, m_read16(s, m_abs(s, 0x1100) + slot)) & 0x8000)) {
            m_lda16(s, m_read16(s, m_abs(s, 0x1180) + slot));
            m_write16(s, m_dp(s, 0x6D), slot);
            m_ldx16(s, s->cpu.a);
            m_sep(s, CPU_FLAG_M);

            /* $C0:5900: fetch and dispatch. */
            while (m_lda8(s, m_read8(s, 0x7F2001 + s->cpu.x)) != 0) {
                s->cpu.y = s->cpu.x;
                m_set_nz16(s, s->cpu.y);
                m_rep(s, CPU_FLAG_M);
                uint16_t op = s->cpu.a = m_asl16(s, m_lda16(s, s->cpu.a & 0x00FF));
                m_ldx16(s, op);
                m_sep(s, CPU_FLAG_M);
                uint16_t handler = m_read16(s, 0xC00000 | (uint16_t)(0x5D6E + op));
                m_jsr(s, 0xC05910, 0xC00000 | handler);
                if (!engine_widths(s)) {
                    /* Not a state this port handles; let the original carry on. */
                    s->cpu.pb = 0xC0;
                    s->cpu.pc = 0x5913;
                    return;
                }
            }

            /* $C0:5915: save the position after the terminator. */
            m_rep(s, CPU_FLAG_M);
            m_ldx16(s, (uint16_t)(s->cpu.x + 1));
            m_lda16(s, s->cpu.x);
            m_ldx16(s, m_read16(s, m_dp(s, 0x6D)));
            m_write16(s, m_abs(s, 0x1180) + s->cpu.x, s->cpu.a);
        }
        m_ldx16(s, (uint16_t)(s->cpu.x + 2));
        m_cmp16(s, s->cpu.x, m_read16(s, m_dp(s, 0xBD)));
    } while (!m_flag(s, CPU_FLAG_Z));

    m_sep(s, CPU_FLAG_M);
    s->cpu.pb = 0xC0;
    s->cpu.pc = 0x9DC3;
}
