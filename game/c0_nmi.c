/* Bank C0 field-mode NMI handler. */
#include "natives.h"

#include "port/machine.h"

enum { DP_SYSTEM = 0x0100, DP_UPLOADS = 0x1D00 };

/* Switch the direct page the way `REP #$20; LDA #page; TCD; SEP #$20` does. */
static void set_direct_page(Snes *s, uint16_t page)
{
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, page);
    s->cpu.d = page;
    m_sep(s, CPU_FLAG_M);
}

/* JSL to the routine that finishes a frame's field update, then clear the
 * pending-frame flag ($0152 through direct page $0100). */
static void finish_frame_update(Snes *s, uint32_t site)
{
    m_jsl(s, site, 0xFDC1EE);
    m_write8(s, m_dp(s, 0x52), 0x00);
}

/* One screen-transition job requested through the bit flags in $47. Returns
 * whether one ran. */
static bool run_screen_transition(Snes *s)
{
    static const struct { uint8_t bit; uint32_t site, routine; } jobs[] = {
        { 0x01, 0xC0EA9E, 0xC06EB0 },
        { 0x02, 0xC0EAAC, 0xC06EF1 },
        { 0x04, 0xC0EABA, 0xC06E5C },
        { 0x08, 0xC0EAC8, 0xC06F5E },
    };
    for (size_t i = 0; i < sizeof jobs / sizeof jobs[0]; i++) {
        m_bit_imm8(s, jobs[i].bit);
        if ((uint8_t)s->cpu.a & jobs[i].bit) {
            m_lda8(s, jobs[i].bit);
            m_trb8(s, m_dp(s, 0x47));
            m_jsr(s, jobs[i].site, jobs[i].routine);
            return true;
        }
    }
    return false;
}

/* A map transition requested through $46. Values 1-4 load a destination from
 * the tables at $1D7C-$1D83; bits $08/$10/$20 run other transition steps.
 * Returns false when the frame should continue with the per-frame jobs. */
static bool run_map_transition(Snes *s, uint8_t request)
{
    static const struct {
        uint8_t value;
        uint16_t destination;
        bool sets_flag;
        uint32_t load_site, finish_site;
    } loads[] = {
        { 1, 0x1D7C, true, 0xC0EAE1, 0xC0EAEF },
        { 2, 0x1D7E, true, 0xC0EB01, 0xC0EB0F },
        { 3, 0x1D82, false, 0xC0EB21, 0xC0EB26 },
        { 4, 0x1D80, false, 0xC0EB38, 0xC0EB3D },
    };
    for (size_t i = 0; i < sizeof loads / sizeof loads[0]; i++) {
        m_cmp8(s, request, loads[i].value);
        if (request != loads[i].value)
            continue;
        uint16_t dest = m_ldx16(s, m_read16(s, m_abs(s, loads[i].destination)));
        m_write16(s, m_dp(s, 0x48), dest);
        m_jsr(s, loads[i].load_site, 0xC07F62);
        m_write8(s, m_dp(s, 0x46), 0x00);
        if (loads[i].sets_flag) {
            uint8_t v = m_lda8(s, m_read8(s, m_abs(s, 0x1D7D)));
            m_lda8(s, v | 0x01);
            m_write8(s, 0x7F14F0, v | 0x01);
        }
        finish_frame_update(s, loads[i].finish_site);
        return true;
    }

    static const struct { uint8_t bit; uint32_t site, routine, finish_site; } steps[] = {
        { 0x08, 0xC0EB4A, 0xC07F5D, 0xC0EB51 },
        { 0x10, 0xC0EB5E, 0xC07F58, 0xC0EB65 },
    };
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        m_bit_imm8(s, steps[i].bit);
        if (request & steps[i].bit) {
            m_jsr(s, steps[i].site, steps[i].routine);
            m_lda8(s, steps[i].bit);
            m_trb8(s, m_dp(s, 0x46));
            finish_frame_update(s, steps[i].finish_site);
            return true;
        }
    }
    m_bit_imm8(s, 0x20);
    if (request & 0x20) {
        m_jsr(s, 0xC0EB72, 0xC0EC77);
        m_lda8(s, 0x20);
        m_trb8(s, m_dp(s, 0x46));
    }
    return false;
}

/* Deferred video-memory uploads: bits 0-2 of $1D78-$1D7B each select a
 * routine; the bit sets are cleared afterwards. */
static void run_video_uploads(Snes *s)
{
    static const uint32_t routines[4][3][2] = {
        { { 0xC0EB9D, 0xC08445 }, { 0xC0EBA6, 0xC084C1 }, { 0xC0EBAF, 0xC08523 } },
        { { 0xC0EBB8, 0xC08585 }, { 0xC0EBC1, 0xC085E7 }, { 0xC0EBCA, 0xC08649 } },
        { { 0xC0EBD3, 0xC086AB }, { 0xC0EBDC, 0xC086F7 }, { 0xC0EBE5, 0xC08729 } },
        { { 0xC0EBEE, 0xC0875B }, { 0xC0EBF7, 0xC0878D }, { 0xC0EC00, 0xC087BF } },
    };
    set_direct_page(s, DP_UPLOADS);
    for (uint8_t group = 0; group < 4; group++) {
        uint32_t flags_addr = m_dp(s, (uint8_t)(0x78 + group));
        m_lda8(s, m_read8(s, flags_addr));
        for (int bit = 0; bit < 3; bit++) {
            m_bit_imm8(s, (uint8_t)(1 << bit));
            if ((uint8_t)s->cpu.a & (1 << bit)) {
                m_jsr(s, routines[group][bit][0], routines[group][bit][1]);
                if (bit < 2)
                    m_lda8(s, m_read8(s, flags_addr));
            }
        }
    }
    m_rep(s, CPU_FLAG_M);
    m_write16(s, m_dp(s, 0x78), 0x0000);
    m_write16(s, m_dp(s, 0x7A), 0x0000);
    m_lda16(s, DP_SYSTEM);
    s->cpu.d = DP_SYSTEM;
    m_sep(s, CPU_FLAG_M);
}

/* $C0:EA63 (installed at the $0500 NMI stub by $C0:0B64)
 * Field-mode NMI. Acknowledges the interrupt and, if the main loop has
 * finished a frame ($0152 nonzero), runs that frame's vblank work: one
 * pending screen or map transition, the per-frame object/OAM/sound jobs, the
 * deferred video uploads, then marks the frame done by clearing $0152. Every
 * frame it also runs the C2 per-frame routine and copies the window and color
 * math shadows, HDMA enable and brightness to the hardware. All registers are
 * restored before RTI. */
void c0_field_nmi(Snes *s)
{
    Cpu *c = &s->cpu;
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_push16(s, c->a);
    m_push16(s, c->x);
    m_push16(s, c->y);
    m_push16(s, c->d);
    m_push8(s, c->db);
    m_sep(s, CPU_FLAG_M);

    m_lda8(s, 0x00);
    m_push8(s, 0x00);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);

    m_lda8(s, m_read8(s, m_abs(s, 0x4210)));      /* acknowledge NMI */
    m_lda8(s, 0x00);
    m_write8(s, m_abs(s, 0x420C), 0x00);          /* HDMA off during vblank work */
    m_lda8(s, 0xA1);
    m_write8(s, m_abs(s, 0x4200), 0xA1);          /* NMI, V-timer IRQ, auto-joypad */
    m_lda8(s, 0x09);
    m_write8(s, m_abs(s, 0x2105), 0x09);          /* BG mode 1, BG3 priority */

    if (m_lda8(s, m_read8(s, m_abs(s, 0x0152))) != 0) {
        set_direct_page(s, DP_SYSTEM);
        bool frame_finished = false;
        uint8_t transitions = m_lda8(s, m_read8(s, m_dp(s, 0x47)));
        if (transitions != 0) {
            run_screen_transition(s);
        } else {
            uint8_t request = m_lda8(s, m_read8(s, m_dp(s, 0x46)));
            if (request != 0)
                frame_finished = run_map_transition(s, request);
            if (!frame_finished) {
                uint8_t fade = m_lda8(s, m_read8(s, m_dp(s, 0x36)));
                if (fade != 0) {
                    if (fade & 0x80)
                        m_write8(s, m_dp(s, 0x36), 0x00);
                    m_jsr(s, 0xC0EB81, 0xC06ECB);
                }
            }
        }
        if (!frame_finished) {
            m_jsr(s, 0xC0EB86, 0xC0712E);
            m_jsr(s, 0xC0EB89, 0xC0ECA3);
            m_jsr(s, 0xC0EB8C, 0xC0B1B2);
            run_video_uploads(s);
            finish_frame_update(s, 0xC0EC0F);
        }
    }

    m_jsl(s, 0xC0EC15, 0xC28002);
    static const struct { uint16_t shadow; uint16_t reg; } shadows[] = {
        { 0x0BD9, 0x2123 }, { 0x0BDA, 0x2124 }, { 0x0BDB, 0x2125 },   /* window masks */
        { 0x0BDC, 0x2128 }, { 0x0BDD, 0x2129 }, { 0x0BDE, 0x2130 },   /* window 2 position, color math */
    };
    for (size_t i = 0; i < sizeof shadows / sizeof shadows[0]; i++)
        m_write8(s, m_abs(s, shadows[i].reg), m_lda8(s, m_read8(s, m_abs(s, shadows[i].shadow))));
    m_lda8(s, 0xE0);
    m_write8(s, m_abs(s, 0x2132), 0xE0);                                         /* fixed color black */
    m_write8(s, m_abs(s, 0x2132), m_lda8(s, m_read8(s, m_abs(s, 0x0121))));
    m_write8(s, m_abs(s, 0x420C), m_lda8(s, m_read8(s, m_abs(s, 0x0128))));      /* HDMA enable */
    uint8_t brightness = m_lda8(s, m_read8(s, m_abs(s, 0x0119)));
    if (brightness == 0)
        brightness = m_lda8(s, 0x80);                                            /* force blank */
    m_write8(s, m_abs(s, 0x2100), brightness);

    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    c->y = m_pull16(s);
    m_set_nz16(s, c->y);
    c->x = m_pull16(s);
    m_set_nz16(s, c->x);
    c->a = m_pull16(s);
    m_set_nz16(s, c->a);
    m_rti(s);
}
