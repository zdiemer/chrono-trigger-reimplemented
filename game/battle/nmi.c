/* Battle: the NMI handler. */
#include "battle/battle.h"

#include "port/machine.h"

/* Call out; false if the callee didn't leave 8-bit A and 16-bit X (the port
 * then stops at the return address and the original carries on). */
static bool jsl(Snes *s, uint32_t site, uint32_t target)
{
    m_jsl(s, site, target);
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M;
}

static uint8_t abs8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr)); }
static void set_abs8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr), v); }

/* INC/DEC on an absolute byte, setting N and Z. */
static uint8_t step_abs8(Snes *s, uint16_t addr, int delta)
{
    uint8_t v = (uint8_t)(abs8(s, addr) + delta);
    set_abs8(s, addr, v);
    m_set_nz8(s, v);
    return v;
}

/* REP #$30 : LDX #src : LDY #dst : LDA #$002A : MVN $7E,$7E -- copy 43 bytes. */
static void copy_43_bytes(Snes *s, uint16_t src, uint16_t dst)
{
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_ldx16(s, src);
    m_set_index(s, &s->cpu.y, dst);
    m_lda16(s, 0x002A);
    m_mvn(s, 0x7E, 0x7E);
}

/* REP #$30 : PLD : PLB : PLY : PLX : PLA : RTI */
static void leave(Snes *s)
{
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    s->cpu.d = m_pull16(s);
    s->cpu.db = m_pull8(s);
    s->cpu.y = m_pull16(s);
    s->cpu.x = m_pull16(s);
    s->cpu.a = m_pull16(s);
    m_rti(s);
}

// $D1:E77C
/* The battle NMI handler (installed at $0500 for battles). Unless the previous
 * one is still running ($7F), it saves direct page $00-$2A to $7E:CC65 around
 * its work and restores it after: copies of the $BE89/$BE8B/$BE91/$BE93 values,
 * forced blank, the per-frame work at $CD:0C95 and (unless paused, $CD02)
 * $CD:0C89, a 16-bit frame count in $7C, the $CCEB countdown, pause toggling on
 * Start ($4219 bit 4; while $7E is 1 and neither $CD27 bit 5 nor $CE16 is set),
 * $D1:FE71, and, when $CD:3FFF is nonzero, a frame-advance mode on Select
 * ($4218 bits 4-5). Finally it clears $7F and $7E:CA20, which releases the
 * battle's wait loops. */
void battle_nmi(Snes *s)
{
    Cpu *c = &s->cpu;
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_push16(s, c->a);
    m_push16(s, c->x);
    m_push16(s, c->y);
    m_push8(s, c->db);
    m_push16(s, c->d);
    c->d = m_lda16(s, 0x0000);
    m_set_nz16(s, c->d);
    m_lda16(s, c->d);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, 0x00420C, (uint8_t)c->a);
    m_lda8(s, m_read8(s, 0x004210));
    m_write8(s, 0x004200, m_lda8(s, 0xA1));
    if (m_lda8(s, m_read8(s, m_dp(s, 0x7F))) != 0) {
        leave(s);
        return;
    }
    uint8_t busy = (uint8_t)(m_read8(s, m_dp(s, 0x7F)) + 1);
    m_write8(s, m_dp(s, 0x7F), busy);
    m_set_nz8(s, busy);

    copy_43_bytes(s, 0x0000, 0xCC65);
    static const uint16_t pairs[4][2] = { { 0xBE89, 0xBE8D }, { 0xBE8B, 0xBE8F }, { 0xBE91, 0xBE95 }, { 0xBE93, 0xBE97 } };
    for (int i = 0; i < 4; i++)
        m_write16(s, m_abs(s, pairs[i][1]), m_lda16(s, m_read16(s, m_abs(s, pairs[i][0]))));
    m_lda16(s, c->d);
    m_sep(s, CPU_FLAG_M);
    m_push8(s, (uint8_t)c->a);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    set_abs8(s, 0x420C, (uint8_t)c->a);
    m_lda8(s, abs8(s, 0x213F));
    set_abs8(s, 0x2100, m_lda8(s, 0x80));
    set_abs8(s, 0x2115, 0x80);
    if (!jsl(s, 0xD1E7DA, 0xCD0C95))
        return;

    if (m_lda8(s, abs8(s, 0xCD02)) == 0) {
        if (!jsl(s, 0xD1E7E3, 0xCD0C89))
            return;
        m_ldx16(s, m_read16(s, m_dp(s, 0x7C)));
        m_ldx16(s, (uint16_t)(c->x + 1));
        m_write16(s, m_dp(s, 0x7C), c->x);
        if (m_lda8(s, abs8(s, 0xCCED)) != 0 && step_abs8(s, 0xCCEB, -1) == 0) {
            if (m_lda8(s, abs8(s, 0xCD1D)) != 0)
                set_abs8(s, 0xCE17, m_lda8(s, 0xFF));
            set_abs8(s, 0xCD1D, 0x00);
            set_abs8(s, 0xCCEC, 0x00);
            set_abs8(s, 0xCCED, 0x00);
            step_abs8(s, 0xCA24, +1);
        }
    }

    /* $D1:E80C: pause toggle */
    uint8_t mode = m_lda8(s, m_read8(s, m_dp(s, 0x7E)));
    m_cmp8(s, mode, 0x01);
    bool clear_pause = false, clear_toggle = true;
    if (mode == 0x01) {
        uint8_t v = m_lda8(s, abs8(s, 0xCD27));
        if (m_lda8(s, v & 0x20) != 0 || m_lda8(s, abs8(s, 0xCE16)) != 0) {
            clear_pause = true;
        } else {
            v = m_lda8(s, m_read8(s, 0x004219));
            if (m_lda8(s, v & 0x10) != 0) {
                clear_toggle = false;
                if (m_lda8(s, abs8(s, 0xCD03)) == 0) {
                    uint8_t paused = m_lda8(s, (uint8_t)(abs8(s, 0xCD02) + 1));
                    set_abs8(s, 0xCD02, m_lda8(s, paused & 0x01));
                    set_abs8(s, 0xCD03, m_lda8(s, 0x01));
                }
            }
        }
    }
    if (clear_pause)
        set_abs8(s, 0xCD02, 0x00);
    if (clear_toggle)
        set_abs8(s, 0xCD03, 0x00);

    /* $D1:E841 */
    if (!jsl(s, 0xD1E841, 0xD1FE71))
        return;
    if (m_lda8(s, m_read8(s, 0xCD3FFF)) != 0) {
        uint8_t v = m_lda8(s, m_read8(s, 0x004218));
        if (m_lda8(s, v & 0x10) != 0) {
            set_abs8(s, 0xCD02, 0x00);
            set_abs8(s, 0xCD03, 0x00);
        } else if (m_lda8(s, abs8(s, 0xCD03)) != 0) {
            uint8_t count = step_abs8(s, 0xCE19, +1);
            uint8_t a = m_lda8(s, abs8(s, 0xCE19));
            a = m_lda8(s, a & 0x03);
            (void)count;
            if (a == 0) {
                a = m_lda8(s, m_read8(s, 0x004218));
                a = m_lda8(s, a & 0x20);
                a = m_lda8(s, a ^ 0x20);
                for (int i = 0; i < 5; i++)
                    a = m_lsr8(s, a);
                m_set_a8(s, a);
            }
            set_abs8(s, 0xCD02, (uint8_t)c->a);
        }
    }

    /* $D1:E87A */
    copy_43_bytes(s, 0xCC65, 0x0000);
    m_lda16(s, c->d);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_dp(s, 0x7F), (uint8_t)c->a);
    m_write8(s, 0x7ECA20, (uint8_t)c->a);
    leave(s);
}
