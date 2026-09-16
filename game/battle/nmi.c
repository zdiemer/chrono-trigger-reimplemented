/* Battle: the NMI and IRQ handlers. */
#include "battle/battle.h"

#include "platform/input.h"
#include "platform/timing.h"
#include "platform/video.h"
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
    Platform *pf = m_platform(s);
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
    vid_enable_line_effects(pf, PF_AT(0xD1E78A), (uint8_t)c->a);
    m_lda8(s, tm_ack_vblank(pf, PF_AT(0xD1E78E)));
    m_lda8(s, 0xA1);
    tm_enable_interrupts(pf, PF_AT(0xD1E794), TM_INT_VBLANK | TM_INT_LINE | TM_INT_AUTO_JOYPAD);
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
    /* the data bank is the direct page's low byte, 0 */
    vid_enable_line_effects(pf, PF_AT(0xD1E7CC), (uint8_t)c->a);
    m_lda8(s, tm_reset_beam_latch(pf, PF_AT(0xD1E7CF)));
    m_lda8(s, 0x80);
    vid_set_display(pf, PF_AT(0xD1E7D4), &(VidDisplay){ .blank = true });
    vid_begin_vram_uploads(pf, PF_AT(0xD1E7D7), VID_VRAM_WORD_PORT, PF_GAME(0), NULL);
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
            v = m_lda8(s, in_read_high(pf, PF_AT(0xD1E81E), 0));
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
        uint8_t v = m_lda8(s, in_read_low(pf, PF_AT(0xD1E84B), 0));
        if (m_lda8(s, v & 0x10) != 0) {
            set_abs8(s, 0xCD02, 0x00);
            set_abs8(s, 0xCD03, 0x00);
        } else if (m_lda8(s, abs8(s, 0xCD03)) != 0) {
            uint8_t count = step_abs8(s, 0xCE19, +1);
            uint8_t a = m_lda8(s, abs8(s, 0xCE19));
            a = m_lda8(s, a & 0x03);
            (void)count;
            if (a == 0) {
                a = m_lda8(s, in_read_low(pf, PF_AT(0xD1E86A), 0));
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

// $D1:FE71
/* Restore the hardware multiplier's or divider's operands from $77/$78 or
 * $79-$7B (the divider when $7E:A029 is set), redoing the operation an
 * interrupt handler's own arithmetic clobbered. Returns with RTL. */
void battle_restore_math_operands(Snes *s)
{
    if (m_lda8(s, m_read8(s, 0x7EA029)) != 0) {
        m_write8(s, 0x004204, m_lda8(s, m_read8(s, m_dp(s, 0x79))));
        m_write8(s, 0x004205, m_lda8(s, m_read8(s, m_dp(s, 0x7A))));
        m_write8(s, 0x004206, m_lda8(s, m_read8(s, m_dp(s, 0x7B))));
    } else {
        m_write8(s, 0x004202, m_lda8(s, m_read8(s, m_dp(s, 0x77))));
        m_write8(s, 0x004203, m_lda8(s, m_read8(s, m_dp(s, 0x78))));
    }
    m_rtl(s);
}

// $D1:FE4C
/* On the IRQ's even frames: when the requested value $CA21 changed (latched
 * into $CA22) or $CA24 asks for it, run $CE:EE6E with $CA25 + $CA22 + $CCEC and
 * clear $CA24. */
void battle_irq_update_ca22(Snes *s)
{
    uint8_t v = m_lda8(s, abs8(s, 0xCA21));
    uint8_t latched = abs8(s, 0xCA22);
    m_cmp8(s, v, latched);
    if (v != latched)
        set_abs8(s, 0xCA22, v);
    else if (m_lda8(s, abs8(s, 0xCA24)) == 0) {
        m_rts(s);
        return;
    }
    uint8_t a = m_lda8(s, abs8(s, 0xCA25));
    m_set_flag(s, CPU_FLAG_C, false);
    a = m_adc8(s, a, abs8(s, 0xCA22));
    m_set_flag(s, CPU_FLAG_C, false);
    a = m_adc8(s, a, abs8(s, 0xCCEC));
    m_set_a8(s, a);
    if (!jsl(s, 0xD1FE69, 0xCEEE6E))
        return;
    set_abs8(s, 0xCA24, 0x00);
    m_rts(s);
}

// $D1:FE97
/* The battle IRQ handler, up to its wait for hblank: save the registers,
 * direct page 0, mark the handler busy ($7F), acknowledge the IRQ, re-enable
 * NMI and auto-joypad ($4200 = $81), data bank 0 and HDMA off. Continues at
 * the wait, $D1:FEB8. */
void battle_irq(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_push16(s, c->a);
    m_push16(s, c->x);
    m_push16(s, c->y);
    m_push8(s, c->db);
    m_push16(s, c->d);
    c->d = m_lda16(s, 0x0000);
    m_sep(s, CPU_FLAG_M);
    uint8_t busy = (uint8_t)(m_read8(s, m_dp(s, 0x7F)) + 1);
    m_write8(s, m_dp(s, 0x7F), busy);
    m_set_nz8(s, busy);
    m_rep(s, CPU_FLAG_X);
    m_lda8(s, tm_ack_line_irq(pf, PF_AT(0xD1FEA8)));
    m_lda8(s, 0x81);
    tm_enable_interrupts(pf, PF_AT(0xD1FEAE), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD);
    m_tdc(s);
    m_push8(s, (uint8_t)c->a);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    vid_enable_line_effects(pf, PF_AT(0xD1FEB5), (uint8_t)c->a);   /* the data bank is 0 */
    c->pc = 0xFEB8;
}

// $D1:FEBF
/* The rest of the battle IRQ handler, from hblank: forced blank, then on even
 * frames ($7C bit 0 clear) $D1:FE4C with data bank $7E, and on odd frames,
 * unless paused ($CD02), the VRAM transfers at $FD:FFFD. Restores the
 * multiplier operands, clears $7F and returns from the interrupt. */
void battle_irq_after_hblank(Snes *s)
{
    Cpu *c = &s->cpu;
    m_lda8(s, 0x80);
    vid_raster_display(m_platform(s), PF_AT(0xD1FEC1), VID_AT_LINE_HANDLER, &(VidDisplay){ .blank = true });
    uint8_t frame = m_lda8(s, m_read8(s, m_dp(s, 0x7C)));
    if (m_lda8(s, frame & 0x01) == 0) {
        m_lda8(s, 0x7E);
        m_push8(s, (uint8_t)c->a);
        c->db = m_pull8(s);
        m_set_nz8(s, c->db);
        m_jsr(s, 0xD1FECE, 0xD1FE4C);
        if (c->e || (c->p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
            return;
    } else if (m_lda8(s, m_read8(s, 0x7ECD02)) == 0) {
        m_push8(s, c->p);
        if (!jsl(s, 0xD1FEDA, 0xFDFFFD))
            return;
        c->p = m_pull8(s);
        if (c->p & CPU_FLAG_X) {
            c->x &= 0xFF;
            c->y &= 0xFF;
        }
    }
    m_ldx16(s, 0x0000);
    m_push16(s, c->x);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    m_sep(s, CPU_FLAG_M);
    if (!jsl(s, 0xD1FEE6, 0xD1FE71))
        return;
    m_write8(s, m_dp(s, 0x7F), 0x00);
    leave(s);
}

/* Guards. battle_irq_after_hblank and battle_setup_window_hdma address
 * registers through the data bank (their callers set it to 0), which the
 * platform services address in bank 0. */
bool battle_data_bank_is_io(Snes *s) { return m_io_bank(s->cpu.db); }

/* battle_upload_vram loads the OAM transfer's mode and address with TDC, which
 * the service writes as 0. */
bool battle_direct_page_low_is_zero(Snes *s) { return (s->cpu.d & 0xFF) == 0; }
