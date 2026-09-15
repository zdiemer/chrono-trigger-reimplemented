/* Scenes: the NMI handler the RAM programs install at $0500. */
#include "scene/scene.h"

#include "port/machine.h"

static uint8_t abs8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr)); }
static void set_abs8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr), v); }

/* After a call-out: false unless the CPU is in native mode with 16-bit X (the
 * code that follows has 16-bit index immediates), and, when need_m8, 8-bit A. */
static bool widths_ok(Snes *s, bool need_m8)
{
    if (s->cpu.e || (s->cpu.p & CPU_FLAG_X))
        return false;
    return !need_m8 || (s->cpu.p & CPU_FLAG_M);
}

/* $C3:0A8E, entered with its JSR's return address pushed. Returns false where
 * the port stops: the auto-joypad read still busy (at the loop's BNE) or
 * L+R+Select+Start (the reset path), or a call-out returning with other
 * widths. */
static bool read_pause_button(Snes *s)
{
    Cpu *c = &s->cpu;
    m_sep(s, CPU_FLAG_M);
    uint8_t status = m_lda8(s, abs8(s, 0x4212));
    m_bit_imm8(s, 0x01);
    if (status & 0x01) {
        c->pc = 0x0A95;   /* BNE back to the LDA $4212 */
        return false;
    }
    m_rep(s, CPU_FLAG_M);
    uint16_t pad = m_lda16(s, m_read16(s, m_abs(s, 0x4218)));
    m_write16(s, m_dp(s, 0x02), pad);
    m_cmp16(s, pad, 0x3030);
    if (pad == 0x3030) {
        c->pc = 0x0AA3;   /* the reset check */
        return false;
    }
    if (m_lda16(s, m_read16(s, m_abs(s, 0x0390))) != 0) {
        m_lda16(s, m_read16(s, m_dp(s, 0x03)));
        m_set_flag(s, CPU_FLAG_Z, (c->a & 0x0010) == 0);
        if (!(c->a & 0x0010)) {
            m_write16(s, m_abs(s, 0x0394), 0x0000);
        } else if (m_lda16(s, m_read16(s, m_abs(s, 0x0394))) == 0) {
            uint16_t count = (uint16_t)(m_read16(s, m_abs(s, 0x0394)) + 1);
            m_write16(s, m_abs(s, 0x0394), count);
            m_set_nz16(s, count);
            m_lda16(s, m_read16(s, m_abs(s, 0x0392)));
            m_lda16(s, (uint16_t)(c->a + 1));
            m_lda16(s, c->a & 0x0001);
            m_write16(s, m_abs(s, 0x0392), c->a);
            uint32_t site;
            if (c->a != 0) {
                m_lda16(s, 0x00F2);
                m_write16(s, m_abs(s, 0x1E00), c->a);
                site = 0xC30AE1;
            } else {
                m_lda16(s, 0x4418);
                m_write16(s, m_abs(s, 0x1E00), c->a);
                m_lda16(s, 0x0080);
                m_write16(s, m_abs(s, 0x1E02), c->a);
                site = 0xC30AF3;
            }
            m_jsl(s, site, 0x7E377A);
            if (!widths_ok(s, false))
                return false;
        }
    }
    m_rep(s, CPU_FLAG_M);
    return true;
}

/* $C3:08D0, entered with its JSL's return address pushed; false where the port
 * stops. */
static bool frame_work(Snes *s)
{
    Cpu *c = &s->cpu;
    m_sep(s, CPU_FLAG_M);
    m_push16(s, 0x0000);                      /* PEA #$0000 : PLD */
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    m_tdc(s);
    m_push8(s, (uint8_t)c->a);                /* PHA : PLB */
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_cmp8(s, (uint8_t)c->a, abs8(s, 0x4210));
    set_abs8(s, 0x420C, 0x00);
    m_ldx16(s, 0x08C9);
    m_write16(s, m_abs(s, 0x0501), c->x);
    m_lda8(s, abs8(s, 0x0384));
    m_bit_imm8(s, 0x80);
    if (c->a & 0x80)
        m_set_flag(s, CPU_FLAG_I, false);     /* CLI */
    set_abs8(s, 0x2100, m_lda8(s, 0x80));
    if (m_lda8(s, abs8(s, 0x0392)) == 0) {
        m_jsl(s, 0xC308F7, 0x7E39BE);
        if (!widths_ok(s, false))
            return false;
    }
    m_sep(s, CPU_FLAG_M);
    set_abs8(s, 0x420C, m_lda8(s, abs8(s, 0x038C)));
    if (m_lda8(s, abs8(s, 0x038D)) == 0)
        m_lda8(s, 0x80);
    set_abs8(s, 0x2100, (uint8_t)c->a);
    m_rep(s, CPU_FLAG_M);
    uint16_t frames = (uint16_t)(m_read16(s, m_dp(s, 0x08)) + 1);
    m_write16(s, m_dp(s, 0x08), frames);
    m_set_nz16(s, frames);
    m_ldx16(s, 0x0920);
    m_set_index(s, &c->y, 0x0950);
    m_lda16(s, 0x001F);
    m_mvn(s, 0x00, 0x00);
    m_sep(s, CPU_FLAG_M);

    m_push16(s, 0x0921);                      /* JSR $0A8E */
    if (!read_pause_button(s))
        return false;
    m_pull16(s);                              /* RTS */

    if (m_lda16(s, m_read16(s, m_abs(s, 0x0392))) != 0) {
        m_sep(s, CPU_FLAG_M);
        uint8_t half = m_lsr8(s, m_lda8(s, abs8(s, 0x038D)));
        m_set_a8(s, half);
        if (half == 0)
            m_lda8(s, 0x80);
        set_abs8(s, 0x2100, (uint8_t)c->a);
        m_rep(s, CPU_FLAG_M);
    } else {
        m_jsl(s, 0xC30938, 0x7E398A);
        if (!widths_ok(s, false))
            return false;
    }
    m_sep(s, CPU_FLAG_M);
    m_ldx16(s, 0x08B3);
    m_write16(s, m_abs(s, 0x0501), c->x);
    return true;
}

/* $C3:09A4, entered with its JSR's return address pushed; false where the port
 * stops. Sends the song in $038A to the sound driver (command $10) when it
 * changed and neither a send is in progress ($0388) nor $0384 bit 5 is set. */
static bool update_song(Snes *s)
{
    Cpu *c = &s->cpu;
    m_sep(s, CPU_FLAG_M);
    if (m_lda8(s, abs8(s, 0x0388)) != 0)
        return true;
    m_lda8(s, abs8(s, 0x0384));
    m_bit_imm8(s, 0x20);
    if (c->a & 0x20)
        return true;
    uint8_t song = m_lda8(s, abs8(s, 0x038A));
    uint8_t playing = abs8(s, 0x0389);
    m_cmp8(s, song, playing);
    if (song == playing)
        return true;
    set_abs8(s, 0x0389, song);
    uint8_t busy = (uint8_t)(abs8(s, 0x0388) + 1);
    set_abs8(s, 0x0388, busy);
    m_set_nz8(s, busy);
    set_abs8(s, 0x1E00, m_lda8(s, 0x10));
    set_abs8(s, 0x1E01, m_lda8(s, abs8(s, 0x038A)));
    m_ldx16(s, 0xFFFF);
    m_write16(s, m_abs(s, 0x1E02), c->x);
    m_jsl(s, 0xC309D1, 0xC70004);
    if (!widths_ok(s, true))
        return false;
    set_abs8(s, 0x0388, 0x00);
    return true;
}

// $C3:08B3
/* The NMI handler of the scenes run from RAM. Saves the registers, then:
 * acknowledges the NMI, stops HDMA and forces blank while it works (pointing
 * the vector at $C3:08C9 meanwhile, and re-enabling interrupts when $0384 bit
 * 7 says IRQs are in use); unless paused ($0392), runs the task list at $0940
 * ($7E:39BE); restores HDMA ($038C) and brightness ($038D, or blank); counts
 * frames in $0008; copies the 16 task slots at $0920 to $0950; reads the
 * controller into $0002 and toggles pause on Start while $0390 allows it (with
 * a sound through $7E:377A); then, paused, shows the screen at half brightness,
 * or runs the task list at $0950 ($7E:398A); and finally sends a changed song
 * to the sound driver. */
void scene_nmi(Snes *s)
{
    Cpu *c = &s->cpu;
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_push16(s, c->a);
    m_push16(s, c->x);
    m_push16(s, c->y);
    m_push16(s, c->d);
    m_push8(s, c->db);

    m_push8(s, 0xC3);                         /* JSL $C308D0 */
    m_push16(s, 0x08BD);
    if (!frame_work(s))
        return;
    m_pull16(s);                              /* RTL */
    m_pull8(s);

    m_push16(s, 0x08C0);                      /* JSR $09A4 */
    if (!update_song(s))
        return;
    m_pull16(s);                              /* RTS */

    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    c->y = m_pull16(s);
    m_set_nz16(s, c->y);
    c->x = m_pull16(s);
    m_set_nz16(s, c->x);
    m_lda16(s, m_pull16(s));
    m_rti(s);
}
