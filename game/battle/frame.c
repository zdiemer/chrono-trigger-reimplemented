/* Battle: the per-frame work the NMI handler runs. */
#include "battle/battle.h"

#include "port/machine.h"

static uint8_t abs8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr)); }
static void set_abs8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr), v); }
static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }

/* INC/DEC on an absolute byte, setting N and Z. */
static uint8_t step_abs8(Snes *s, uint16_t addr, int delta)
{
    uint8_t v = (uint8_t)(abs8(s, addr) + delta);
    set_abs8(s, addr, v);
    m_set_nz8(s, v);
    return v;
}

static bool widths_ok(const Snes *s) { return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M; }

/* JSR/JSL out; false if the callee didn't come back with 8-bit A and 16-bit X
 * (the port then stops at the return address and the original carries on). */
static bool jsr(Snes *s, uint32_t site, uint32_t target)
{
    m_jsr(s, site, target);
    return widths_ok(s);
}

static bool jsl(Snes *s, uint32_t site, uint32_t target)
{
    m_jsl(s, site, target);
    return widths_ok(s);
}

/* LDA #$7E : PHA : PLB (or TDC : PHA : PLB for bank 0) */
static void set_data_bank(Snes *s, uint8_t bank)
{
    m_push8(s, bank);
    s->cpu.db = m_pull8(s);
    m_set_nz8(s, s->cpu.db);
}

// $CD:0C95
/* The battle NMI's per-frame work: dispatch on the battle mode ($7E) through
 * the table at $CD:0C83. Returns with RTL. */
void battle_frame_dispatch(Snes *s)
{
    uint8_t mode = m_asl8(s, m_lda8(s, dp8(s, 0x7E)));
    m_set_a8(s, mode);
    m_tax(s);
    m_jsr(s, 0xCD0C99, 0xCD0000 | m_jump_table(s, 0xCD, 0x0C83, s->cpu.x));
    m_rtl(s);
}

// $CD:3EB4
/* Run a queued VRAM transfer ($CA26 set) with $CD:3E8E: source $CA27 in bank
 * $CA29, VRAM address $CA2A, size $CA2C; then clear the request. */
void battle_flush_vram_transfer(Snes *s)
{
    Cpu *c = &s->cpu;
    if (m_lda8(s, abs8(s, 0xCA26)) == 0) {
        m_rts(s);
        return;
    }
    m_ldx16(s, m_read16(s, m_abs(s, 0xCA2C)));
    m_write16(s, m_dp(s, 0x45), c->x);
    m_ldx16(s, m_read16(s, m_abs(s, 0xCA27)));
    m_set_index(s, &c->y, m_read16(s, m_abs(s, 0xCA2A)));
    m_lda8(s, abs8(s, 0xCA29));
    if (!jsr(s, 0xCD3EC7, 0xCD3E8E))
        return;
    set_abs8(s, 0xCA26, 0x00);
    m_rts(s);
}

// $CD:05B2
/* Step the sliding HDMA window ($CA1E: 0 idle, positive sliding one way, $FF
 * starting the other way, other negative values sliding back; $CA1F frames
 * left): each frame move it ($CD:053C or $CD:0577) and, when done, request
 * the follow-up ($CA24/$CA25) and clear $CA1E. */
void battle_step_window_slide(Snes *s)
{
    uint8_t state = m_lda8(s, abs8(s, 0xCA1E));
    if (state == 0) {
        m_rts(s);
        return;
    }
    bool finish = false;
    if (!(state & 0x80)) {
        if (m_lda8(s, abs8(s, 0xCA1F)) == 0) {
            finish = true;
        } else {
            if (!jsr(s, 0xCD05BE, 0xCD053C))
                return;
            if (step_abs8(s, 0xCA1F, -1) == 0) {
                set_abs8(s, 0xCA25, m_lda8(s, 0x02));
                step_abs8(s, 0xCA24, +1);
            }
        }
    } else {
        m_cmp8(s, state, 0xFF);
        if (state == 0xFF) {
            set_abs8(s, 0xCA25, 0x00);
            m_lda8(s, abs8(s, 0xCA22));
            if (!jsl(s, 0xCD05DA, 0xCEEE6E))
                return;
            set_abs8(s, 0xCA1E, m_lda8(s, 0x01));
        } else {
            if (!jsr(s, 0xCD05E5, 0xCD0577))
                return;
            if (step_abs8(s, 0xCA1F, -1) == 0) {
                set_abs8(s, 0xCA25, 0x00);
                step_abs8(s, 0xCA24, +1);
                finish = true;
            }
        }
    }
    if (finish) {
        if (!(m_lda8(s, abs8(s, 0xCA1E)) & 0x80))
            set_abs8(s, 0xCE16, 0x00);
        set_abs8(s, 0xCA1E, 0x00);
    }
    m_rts(s);
}

// $CD:06B4
/* Jump to the scroll table routine for the battle's scroll mode ($CA92)
 * through the table at $CD:06BC. */
void battle_update_scroll_dispatch(Snes *s)
{
    m_set_a8(s, m_asl8(s, m_lda8(s, abs8(s, 0xCA92))));
    m_tax(s);
    s->cpu.pc = m_jump_table(s, 0xCD, 0x06BC, s->cpu.x);
}

// $CD:0ADE
/* Unless palette animation slot 0 is counting ($0524), count $CDC9 down
 * through 0 and back to 13. */
void battle_step_cdc9(Snes *s)
{
    if (m_lda8(s, abs8(s, 0x0524)) == 0 && (step_abs8(s, 0xCDC9, -1) & 0x80))
        set_abs8(s, 0xCDC9, m_lda8(s, 0x0D));
    m_rts(s);
}

/* LDA $CA21 : INC A : AND #$01 : STA $CA21 : STA $EC -- toggle the battle's
 * alternating-frame flag. */
static void toggle_ca21(Snes *s)
{
    uint8_t v = m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0xCA21)) + 1));
    v = m_lda8(s, v & 0x01);
    set_abs8(s, 0xCA21, v);
    m_write8(s, m_dp(s, 0xEC), v);
}

// $CD:088D
/* The per-frame battle work in battle modes 1 and 2, run from the NMI handler:
 * a queued VRAM transfer, the window registers ($2123-$2125, $212A-$212B)
 * from their shadows at $C861, HDMA setup, the window slide (twice), then
 * either, when paused ($CD02), the brightness (halved unless $CD:3FFF is set),
 * or the full frame: the scroll tables, the alternating-frame flag, the OAM
 * buffer, the battle's own per-frame routines and palette animation, most of
 * them as call-outs. */
void battle_frame_work(Snes *s)
{
    Cpu *c = &s->cpu;
    if (m_lda8(s, m_read8(s, 0x7ECE0F)) != 0) {
        if (!jsl(s, 0xCD0893, 0xCD09CE))
            return;
        m_lda8(s, 0x7E);
        set_data_bank(s, 0x7E);
        if (!jsr(s, 0xCD089B, 0xCD3EB4))
            return;
    } else {
        m_lda8(s, 0x7E);
        set_data_bank(s, 0x7E);
        if (m_lda8(s, abs8(s, 0xCA1E)) == 0 && !jsr(s, 0xCD08A9, 0xCD0607))
            return;
        if (!jsr(s, 0xCD08AC, 0xCD3EB4))
            return;
        if (m_lda8(s, abs8(s, 0xCD02)) == 0 && !jsl(s, 0xCD08B4, 0xC10009))
            return;
    }

    /* $CD:08B8 */
    static const uint16_t regs[5] = { 0x2123, 0x2124, 0x2125, 0x212A, 0x212B };
    for (int i = 0; i < 5; i++)
        m_write8(s, regs[i], m_lda8(s, abs8(s, (uint16_t)(0xC861 + i))));
    set_abs8(s, 0xBE93, m_lda8(s, abs8(s, 0xC866)));
    m_write16(s, m_abs(s, 0xC151), m_ldx16(s, m_read16(s, m_abs(s, 0xC155))));
    m_write16(s, m_abs(s, 0xC153), m_ldx16(s, m_read16(s, m_abs(s, 0xC157))));
    m_push8(s, c->db);
    m_tdc(s);
    set_data_bank(s, (uint8_t)c->a);
    if (!jsr(s, 0xCD08F1, 0xCD0AEE))
        return;
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    if (!jsr(s, 0xCD08F5, 0xCD05B2) || !jsr(s, 0xCD08F8, 0xCD05B2))
        return;
    m_write8(s, 0x00420C, m_lda8(s, abs8(s, 0xCA1D)));

    if (m_lda8(s, abs8(s, 0xCD02)) != 0) {
        uint8_t brightness;
        if (m_lda8(s, m_read8(s, 0xCD3FFF)) != 0) {
            brightness = m_lda8(s, abs8(s, 0xBB00));
        } else {
            brightness = m_lsr8(s, m_lda8(s, abs8(s, 0xBB00)));
            m_set_a8(s, brightness);
        }
        m_write8(s, 0x002100, brightness);
        if (!jsl(s, 0xCD091A, 0xC28002) || !jsr(s, 0xCD091E, 0xCD0340))
            return;
        m_rts(s);
        return;
    }

    /* $CD:0922 */
    m_write8(s, 0x002100, m_lda8(s, abs8(s, 0xBB00)));
    if (!jsr(s, 0xCD0929, 0xCD06B4) || !jsr(s, 0xCD092C, 0xCD0B51))
        return;
    uint8_t frame = m_lda8(s, dp8(s, 0x7C));
    if (m_lda8(s, frame & 0x01) == 0 && m_lda8(s, abs8(s, 0xCA1E)) == 0) {
        uint8_t ec = m_lda8(s, dp8(s, 0xEC));
        uint8_t ca21 = abs8(s, 0xCA21);
        m_cmp8(s, ec, ca21);
        if (ec != ca21)
            toggle_ca21(s);
        /* $CD:094C */
        bool clear = true;
        if (m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0xCD27)) & 0x20)) == 0 &&
            m_lda8(s, (uint8_t)(m_lda8(s, dp8(s, 0xF8)) & 0x01)) != 0) {
            clear = false;
            if (m_lda8(s, abs8(s, 0xCA23)) == 0) {
                toggle_ca21(s);
                set_abs8(s, 0xCA23, m_lda8(s, 0x01));
            }
        }
        if (clear)
            set_abs8(s, 0xCA23, 0x00);
    }

    /* $CD:0973 */
    if (m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0x2A21)) & 0x01)) != 0) {
        if (!jsl(s, 0xCD097A, 0xCEE18E))
            return;
        uint8_t v = m_lda8(s, dp8(s, 0x7C));
        if (m_lda8(s, (uint8_t)(v & abs8(s, 0xCE13))) == 0 && !jsl(s, 0xCD0985, 0xD1EA4B))
            return;
    }
    if (m_lda8(s, abs8(s, 0xCD2D)) == 0 && m_lda8(s, abs8(s, 0xA028)) == 0 && !jsl(s, 0xCD0993, 0xCEF066))
        return;
    if (!jsl(s, 0xCD0997, 0xC1000C))
        return;
    if (m_lda8(s, abs8(s, 0xCD2E)) == 0 && m_lda8(s, abs8(s, 0xA013)) != 0 && m_lda8(s, abs8(s, 0xA028)) == 0) {
        m_lda8(s, 0x40);
        if (!jsr(s, 0xCD09AC, 0xCD3B82))
            return;
    }
    if (!jsr(s, 0xCD09AF, 0xCD1609))
        return;
    set_abs8(s, 0xA028, m_lda8(s, abs8(s, 0xCD29)));
    if (!jsr(s, 0xCD09B8, 0xCD0B1E) || !jsl(s, 0xCD09BB, 0xD1F426) || !jsl(s, 0xCD09BF, 0xFDFFF7) ||
        !jsr(s, 0xCD09C3, 0xCD0ADE) || !jsl(s, 0xCD09C6, 0xC28002) || !jsr(s, 0xCD09CA, 0xCD0340))
        return;
    m_rts(s);
}
