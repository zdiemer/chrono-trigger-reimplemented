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

/* LDA #v (or a table value) : STA $00xxxx */
static void put8(Snes *s, uint16_t reg, uint8_t v) { m_write8(s, reg, m_lda8(s, v)); }

/* TDC : STA $00xxxx -- the direct page's low byte, 0 in battle. */
static void put_d8(Snes *s, uint16_t reg)
{
    m_tdc(s);
    m_write8(s, reg, (uint8_t)s->cpu.a);
}

/* LDA #$80 : STA $00420B -- run DMA channel 7. */
static void start_dma7(Snes *s) { put8(s, 0x420B, 0x80); }

/* One of the queued VRAM uploads from the tables at $CC:F957 (`slot` a
 * multiple of 2; X, or 0 through long addresses): two transfers from $7E, the
 * second only when its size is nonzero. */
static void upload_queued_vram(Snes *s, uint16_t slot)
{
    put8(s, 0x4370, 0x01);
    put8(s, 0x4371, 0x18);
    put8(s, 0x4372, m_read8(s, 0xCCF957 + slot));
    put8(s, 0x4373, m_read8(s, 0xCCF958 + slot));
    put8(s, 0x4374, 0x7E);
    m_rep(s, CPU_FLAG_M);
    m_write16(s, 0x4375, m_lda16(s, m_read16(s, 0xCCF977 + slot)));
    m_write16(s, 0x2116, m_lda16(s, m_read16(s, 0xCCF967 + slot)));
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    start_dma7(s);
    put8(s, 0x4372, m_read8(s, 0xCCF95F + slot));
    put8(s, 0x4373, m_read8(s, 0xCCF960 + slot));
    m_rep(s, CPU_FLAG_M);
    uint16_t size = m_lda16(s, m_read16(s, 0xCCF97F + slot));
    if (size != 0) {
        m_write16(s, 0x4375, size);
        m_write16(s, 0x2116, m_lda16(s, m_read16(s, 0xCCF96F + slot)));
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        start_dma7(s);
    } else {
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
    }
}

// $CF:E300
/* The battle's vblank uploads over DMA channel 7: the OAM buffer ($0700, 544
 * bytes); a pending transfer described at $9EEA ($A0 set); the $0B40 tile
 * block to VRAM $7A00 when $99E2 is set; the palette ($7E:2200); one of four
 * rotating queued VRAM uploads ($A4) while $E5 is set and $A013 clear; the
 * current object's two graphics uploads when $A012 is 1 (objects of graphics
 * kind below 3); and upload slot 0 again when the $A118 countdown ends.
 * Returns with RTL. */
void battle_upload_vram(Snes *s)
{
    Cpu *c = &s->cpu;
    put_d8(s, 0x4370);
    put8(s, 0x4371, 0x04);
    put8(s, 0x4372, 0x00);
    put8(s, 0x4373, 0x07);
    put8(s, 0x4374, 0x00);
    put8(s, 0x4375, 0x20);
    put8(s, 0x4376, 0x02);
    put_d8(s, 0x2102);
    start_dma7(s);

    if (m_lda8(s, dp8(s, 0xA0)) != 0) {
        static const uint16_t regs[9] = { 0x4370, 0x4371, 0x4372, 0x4373, 0x4374, 0x4375, 0x4376, 0x2116, 0x2117 };
        for (int i = 0; i < 9; i++)
            m_write8(s, regs[i], m_lda8(s, abs8(s, (uint16_t)(0x9EEA + 6 * i))));
        start_dma7(s);
        m_write8(s, m_dp(s, 0xA0), 0x00);
    }
    if (m_lda8(s, abs8(s, 0x99E2)) != 0) {
        static const uint8_t v[9] = { 0x01, 0x18, 0x40, 0x0B, 0x00, 0x80, 0x01, 0x00, 0x7A };
        static const uint16_t regs[9] = { 0x4370, 0x4371, 0x4372, 0x4373, 0x4374, 0x4375, 0x4376, 0x2116, 0x2117 };
        for (int i = 0; i < 9; i++)
            put8(s, regs[i], v[i]);
        start_dma7(s);
        set_abs8(s, 0x99E2, 0x00);
    }
    {
        static const uint8_t v[7] = { 0x00, 0x22, 0x00, 0x22, 0x7E, 0x00, 0x02 };
        for (int i = 0; i < 7; i++)
            put8(s, (uint16_t)(0x4370 + i), v[i]);
        put_d8(s, 0x2121);
        start_dma7(s);
    }

    /* $CF:E3F8 */
    if (m_lda8(s, abs8(s, 0xA013)) == 0 && m_lda8(s, dp8(s, 0xE5)) != 0) {
        m_set_a8(s, m_asl8(s, m_lda8(s, dp8(s, 0xA4))));
        m_tax(s);
        upload_queued_vram(s, c->x);
        uint8_t slot = (uint8_t)(dp8(s, 0xA4) + 1);
        m_write8(s, m_dp(s, 0xA4), slot);
        m_set_nz8(s, slot);
        slot = m_lda8(s, dp8(s, 0xA4));
        m_cmp8(s, slot, 0x04);
        if (slot == 0x04)
            m_write8(s, m_dp(s, 0xA4), 0x00);
    }

    /* $CF:E481 */
    uint8_t request = m_lda8(s, abs8(s, 0xA012));
    if (request != 0) {
        m_cmp8(s, request, 0x01);
        if (request == 0x01) {
            set_abs8(s, 0xA012, 0x00);
            m_lda8(s, abs8(s, 0x9679));
            m_tax(s);
            uint8_t kind = m_lda8(s, m_read8(s, m_abs(s, 0x967C) + c->x));
            m_cmp8(s, kind, 0x03);
            if (kind < 0x03) {
                m_txa(s);
                m_set_a8(s, m_asl8(s, (uint8_t)c->a));
                m_tax(s);
                /* source $A654/$A66A, size $A6AC/$A6C2, VRAM address $A680/$A696 */
                static const uint16_t regs[6] = { 0x4372, 0x4373, 0x4375, 0x4376, 0x2116, 0x2117 };
                static const uint16_t tables[2][6] = {
                    { 0xA654, 0xA655, 0xA6AC, 0xA6AD, 0xA680, 0xA681 },
                    { 0xA66A, 0xA66B, 0xA6C2, 0xA6C3, 0xA696, 0xA697 },
                };
                put8(s, 0x4370, 0x01);
                put8(s, 0x4371, 0x18);
                for (int t = 0; t < 2; t++) {
                    for (int i = 0; i < 6; i++) {
                        if (t == 0 && i == 2)
                            put8(s, 0x4374, 0x7E);
                        put8(s, regs[i], m_read8(s, m_abs(s, tables[t][i]) + c->x));
                    }
                    start_dma7(s);
                }
            }
        }
    }

    /* $CF:E510 */
    if (m_lda8(s, abs8(s, 0xA118)) != 0 && step_abs8(s, 0xA118, -1) == 0)
        upload_queued_vram(s, 0);
    m_rtl(s);
}

/* INC on a 16-bit absolute word, setting N and Z. */
static void inc_abs16(Snes *s, uint16_t addr)
{
    uint16_t v = (uint16_t)(m_read16(s, m_abs(s, addr)) + 1);
    m_write16(s, m_abs(s, addr), v);
    m_set_nz16(s, v);
}

/* DEC on the 16-bit direct-page word $C8; returns the new value. */
static uint16_t dec_c8(Snes *s)
{
    uint16_t v = (uint16_t)(m_read16(s, m_dp(s, 0xC8)) - 1);
    m_write16(s, m_dp(s, 0xC8), v);
    m_set_nz16(s, v);
    return v;
}

/* $CF:E653/$CF:E682: rotate colors of the working palette up one slot, from
 * X down, for $C8 + 1 colors (16-bit A). */
static void rotate_colors(Snes *s)
{
    Cpu *c = &s->cpu;
    m_write16(s, m_dp(s, 0xC6), m_lda16(s, m_read16(s, m_abs(s, 0x2200) + c->x)));
    m_ldx16(s, (uint16_t)(c->x - 2));
    do {
        m_write16(s, m_abs(s, 0x2202) + c->x, m_lda16(s, m_read16(s, m_abs(s, 0x2200) + c->x)));
        m_ldx16(s, (uint16_t)(c->x - 2));
    } while (dec_c8(s) != 0);
    m_write16(s, m_abs(s, 0x2202) + c->x, m_lda16(s, m_read16(s, m_dp(s, 0xC6))));
}

// $CF:E58A
/* The battle's per-frame update, run from the main loop's frame wait: the
 * blink timer ($A110 toggling every 12 frames), the party palette cycle, the
 * animated color at $233C or $23BC (by $9F46 bit 3, flipped by $A101), the
 * 32-bit frame counter $96F1, the twelve-color rotation at $2382 and the
 * background color rotation ($A08B, rows from $99D3), priority bits of the
 * first four OAM entries from $9F46, a demo exit on any button (resetting to
 * $C0:FF00 once $BB00 counts down), the L+R hold counter ($99CF), the effect
 * slots, object paths, anchors, trails and prepared sprites, $CF:F9D3 when
 * $AA63 is set, and the object sprites. Clears $9E, restores $A1 and returns
 * with RTL. */
void battle_frame_update(Snes *s)
{
    Cpu *c = &s->cpu;
    if (step_abs8(s, 0xA112, -1) == 0) {
        set_abs8(s, 0xA112, m_lda8(s, 0x0C));
        set_abs8(s, 0xA110, m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0xA110)) ^ 0x01)));
    }
    if (m_lda8(s, abs8(s, 0xA113)) != 0)
        set_abs8(s, 0x9945, m_lda8(s, abs8(s, 0x993F)));
    if (!jsr(s, 0xCFE5A7, 0xCFE7D7))
        return;
    step_abs8(s, 0xA4EF, +1);
    uint8_t phase = m_lda8(s, abs8(s, 0xA4EF));
    m_cmp8(s, phase, 0x3E);
    if (phase == 0x3E) {
        m_tdc(s);
        set_abs8(s, 0xA4EF, (uint8_t)c->a);
    }
    m_set_a8(s, m_asl8(s, (uint8_t)c->a));
    m_tax(s);
    uint8_t layer;
    if (m_lda8(s, abs8(s, 0xA101)) != 0)
        layer = m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0x9F46)) ^ 0x08));
    else
        layer = m_lda8(s, abs8(s, 0x9F46));
    uint16_t color = m_lda8(s, layer & 0x08) ? 0x23BC : 0x233C;
    set_abs8(s, color, m_lda8(s, m_read8(s, 0xD15980 + c->x)));
    set_abs8(s, (uint16_t)(color + 1), m_lda8(s, m_read8(s, 0xD15981 + c->x)));

    /* $CF:E5EB */
    m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
    uint16_t low = m_adc16(s, m_lda16(s, m_read16(s, m_abs(s, 0x96F1))), 0x0001);
    m_write16(s, m_abs(s, 0x96F1), low);
    uint16_t high = m_adc16(s, m_lda16(s, m_read16(s, m_abs(s, 0x96F3))), 0x0000);
    m_write16(s, m_abs(s, 0x96F3), high);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);

    if (m_lda8(s, abs8(s, 0xAB4E)) != 0 && step_abs8(s, 0xAB4A, -1) == 0) {
        set_abs8(s, 0xAB4A, m_lda8(s, abs8(s, 0xAB4B)));
        m_ldx16(s, 0x0016);
        m_rep(s, CPU_FLAG_M);
        m_write16(s, m_dp(s, 0xC6), m_lda16(s, m_read16(s, m_abs(s, 0x2398))));
        do {
            m_write16(s, m_abs(s, 0x2382) + c->x, m_lda16(s, m_read16(s, m_abs(s, 0x2380) + c->x)));
            m_ldx16(s, (uint16_t)(c->x - 2));
        } while (!(c->x & 0x8000));
        m_write16(s, m_abs(s, 0x2382), m_lda16(s, m_read16(s, m_dp(s, 0xC6))));
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);

        if (m_lda8(s, abs8(s, 0xA08B)) != 0) {
            uint8_t row = m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0x99D3)) & 0x0F));
            m_write8(s, m_dp(s, 0xC6), row);
            m_set_flag(s, CPU_FLAG_C, true);
            m_set_a8(s, m_sbc8(s, m_lda8(s, 0x0F), m_read8(s, m_dp(s, 0xC6))));
            m_tax(s);
            m_write16(s, m_dp(s, 0xC8), c->x);
            m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0x99D3)) | 0x0F));
            m_rep(s, CPU_FLAG_M);
            c->a = m_asl16(s, c->a);
            m_tax(s);
            m_write16(s, m_dp(s, 0xCA), c->x);
            rotate_colors(s);
            m_tdc(s);
            m_sep(s, CPU_FLAG_M);

            m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0x99D3)) | 0x0F));
            m_set_flag(s, CPU_FLAG_C, false);
            m_set_a8(s, m_adc8(s, (uint8_t)c->a, 0x10));
            m_rep(s, CPU_FLAG_M);
            c->a = m_asl16(s, c->a);
            m_write16(s, m_dp(s, 0xCA), c->a);
            do {
                m_write16(s, m_dp(s, 0xC8), m_lda16(s, 0x000E));
                m_ldx16(s, m_read16(s, m_dp(s, 0xCA)));
                rotate_colors(s);
                m_set_flag(s, CPU_FLAG_C, false);
                uint16_t next = m_adc16(s, m_lda16(s, m_read16(s, m_dp(s, 0xCA))), 0x0020);
                m_write16(s, m_dp(s, 0xCA), next);
                c->a = next;
            } while (m_lda16(s, c->a & 0xFF00) == 0);
            m_tdc(s);
            m_sep(s, CPU_FLAG_M);
        }
    }

    /* $CF:E6AA */
    for (uint16_t attr = 0x0703; attr <= 0x070F; attr += 4) {
        uint8_t v = m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, attr)) & 0xF1));
        set_abs8(s, attr, m_lda8(s, (uint8_t)(v | abs8(s, 0x9F46))));
    }
    m_push8(s, m_lda8(s, dp8(s, 0xA1)));
    if (m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0x2989)) & 0x20)) != 0) {
        uint8_t pad = m_lda8(s, m_read8(s, 0x004218));
        if (m_lda8(s, (uint8_t)(pad | m_read8(s, 0x004219))) != 0)
            set_abs8(s, 0xA097, m_lda8(s, 0x01));
        if (m_lda8(s, abs8(s, 0xA097)) != 0) {
            uint8_t fade = m_lda8(s, (uint8_t)(abs8(s, 0xBB00) - 1));
            set_abs8(s, 0xBB00, fade);
            if (fade == 0) {
                set_abs8(s, 0xBB80, m_lda8(s, 0x80));
                c->pb = 0xC0;
                c->pc = 0xFF00;
                return;
            }
        }
    }

    /* $CF:E706 */
    if (m_lda8(s, abs8(s, 0xA013)) == 0) {
        uint8_t lr = m_lda8(s, (uint8_t)(m_lda8(s, m_read8(s, 0x004218)) & 0x30));
        m_cmp8(s, lr, 0x30);
        if (lr == 0x30) {
            m_rep(s, CPU_FLAG_M);
            inc_abs16(s, 0x99CF);
            uint16_t held = m_lda16(s, m_read16(s, m_abs(s, 0x99CF)));
            m_cmp16(s, held, 0x0078);
            if (held >= 0x0078 &&
                m_lda16(s, (uint16_t)(m_lda16(s, m_read16(s, m_abs(s, 0x2989))) & 0x0080)) == 0)
                inc_abs16(s, 0x99CD);
            m_tdc(s);
            m_sep(s, CPU_FLAG_M);
        } else {
            set_abs8(s, 0x99D1, 0x00);
            set_abs8(s, 0x99CF, 0x00);
            set_abs8(s, 0x99D0, 0x00);
        }
    }

    /* $CF:E73B */
    step_abs8(s, 0x9931, +1);
    if (m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0x9931)) & 0x03)) == 0)
        step_abs8(s, 0x993A, +1);
    if (!jsr(s, 0xCFE748, 0xCFEFAF))
        return;
    if (m_lda8(s, abs8(s, 0xA013)) != 0 &&
        (!jsr(s, 0xCFE750, 0xCFEC78) || !jsr(s, 0xCFE753, 0xCFECC2) || !jsr(s, 0xCFE756, 0xCFED18) ||
         !jsr(s, 0xCFE759, 0xCFE781)))
        return;
    if (m_lda8(s, abs8(s, 0xAA63)) != 0 && !jsr(s, 0xCFE761, 0xCFF9D3))
        return;
    if (m_lda8(s, abs8(s, 0xA028)) == 0) {
        if (m_lda8(s, abs8(s, 0xA013)) != 0) {
            uint8_t request = m_lda8(s, abs8(s, 0xA012));
            m_cmp8(s, request, 0x02);
            if (request == 0x02)
                step_abs8(s, 0xA012, -1);
        }
        if (!jsr(s, 0xCFE778, 0xCFE83A))
            return;
    }
    m_write8(s, m_dp(s, 0x9E), 0x00);
    uint8_t a1 = m_pull8(s);
    m_lda8(s, a1);
    m_write8(s, m_dp(s, 0xA1), a1);
    m_rtl(s);
}

// $CD:3E8E
/* VRAM transfer over DMA channel 7: $45 bytes from A:X to VRAM address Y. */
void battle_vram_transfer(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_push8(s, (uint8_t)c->a);
    m_push8(s, m_lda8(s, 0x00));
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    uint8_t bank = m_pull8(s);
    m_lda8(s, bank);
    m_write16(s, 0x2116, c->y);
    m_write16(s, 0x4372, c->x);
    m_write8(s, 0x4374, bank);
    put8(s, 0x4370, 0x01);
    put8(s, 0x4371, 0x18);
    m_ldx16(s, m_read16(s, m_dp(s, 0x45)));
    m_write16(s, 0x4375, c->x);
    start_dma7(s);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rts(s);
}

// $CD:0607
/* Per-frame VRAM work: a requested block upload ($CA17: sixteen 32-byte rows
 * from $7E:$CA18 through $D1:E8D9, to the VRAM address for block $CA16 from
 * $CD:0601), skipped when block and source match the last upload
 * ($CD1F/$CD20); then $200 bytes of VRAM at $5800 or $5980 on
 * alternating frames ($CE14): cleared from the zero word at $CD:06B2, or, when
 * $CCEC is $10, copied from $7E:B400 or $7E:B600 through $CD:3E8E. */
void battle_upload_alternating_vram(Snes *s)
{
    Cpu *c = &s->cpu;
    if (m_lda8(s, abs8(s, 0xCA17)) != 0) {
        set_abs8(s, 0xCA17, 0x00);
        uint8_t row = m_asl8(s, m_lda8(s, abs8(s, 0xCA16)));
        m_set_a8(s, row);
        m_push8(s, row);
        set_abs8(s, 0xCA16, 0x00);
        m_tax(s);
        m_rep(s, CPU_FLAG_M);
        m_write16(s, m_dp(s, 0x45), m_lda16(s, m_read16(s, 0xCD05FF + c->x)));
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        row = m_pull8(s);
        m_lda8(s, row);
        uint8_t last = abs8(s, 0xCD1F);
        m_cmp8(s, row, last);
        bool upload = true;
        if (row == last) {
            uint16_t src = m_ldx16(s, m_read16(s, m_abs(s, 0xCA18)));
            uint16_t last_src = m_read16(s, m_abs(s, 0xCD20));
            m_cmp16(s, src, last_src);
            if (src == last_src)
                upload = false;
            else
                m_write16(s, m_abs(s, 0xCD20), src);
        } else {
            set_abs8(s, 0xCD1F, row);
            m_write16(s, m_abs(s, 0xCD20), m_ldx16(s, m_read16(s, m_abs(s, 0xCA18))));
        }
        if (upload) {
            m_ldx16(s, m_read16(s, m_abs(s, 0xCA18)));
            m_lda8(s, 0x7E);
            if (!jsl(s, 0xCD0644, 0xD1E8D9))
                return;
            step_abs8(s, 0xA01F, +1);
        }
    }

    /* $CD:064B */
    m_write16(s, m_dp(s, 0x45), m_ldx16(s, 0x0200));
    step_abs8(s, 0xCE14, +1);
    uint8_t mode = m_lda8(s, abs8(s, 0xCCEC));
    m_cmp8(s, mode, 0x10);
    if (mode == 0x10) {
        bool odd = m_lda8(s, (uint8_t)(m_lda8(s, m_read8(s, 0x7ECE14)) & 0x01)) != 0;
        m_ldx16(s, odd ? 0xB400 : 0xB600);
        m_set_index(s, &c->y, odd ? 0x5800 : 0x5980);
        m_lda8(s, 0x7E);
        c->pc = 0x3E8E;
        return;
    }
    m_push8(s, c->db);
    set_data_bank(s, m_lda8(s, 0x00));
    bool odd = m_lda8(s, (uint8_t)(m_lda8(s, m_read8(s, 0x7ECE14)) & 0x01)) != 0;
    m_write16(s, m_abs(s, 0x2116), m_ldx16(s, odd ? 0x5800 : 0x5980));
    set_abs8(s, 0x4370, m_lda8(s, 0x09));
    set_abs8(s, 0x4371, m_lda8(s, 0x18));
    m_write16(s, m_abs(s, 0x4372), m_ldx16(s, 0x06B2));
    set_abs8(s, 0x4374, m_lda8(s, 0xCD));
    m_write16(s, m_abs(s, 0x4375), m_ldx16(s, 0x0200));
    set_abs8(s, 0x420B, m_lda8(s, 0x80));
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rts(s);
}

/* JSR $0B16: ADC $7C : TAX : LDA $C0:FE00,X -- a pseudo-random byte from
 * the table at $C0:FE00, indexed by A plus the frame counter (and carry). */
static uint8_t shake_random(Snes *s, uint16_t site)
{
    m_push16(s, (uint16_t)(site + 2));
    uint8_t v = m_adc8(s, (uint8_t)s->cpu.a, dp8(s, 0x7C));
    m_set_a8(s, v);
    m_tax(s);
    v = m_lda8(s, m_read8(s, 0xC0FE00 + s->cpu.x));
    m_pull16(s);
    return v;
}

/* One axis of $CD:0B51: while enabled (`on`), pick a new random magnitude every
 * `period` frames and step the amplitude toward its target every `rate`
 * frames (disabling the axis when it decays to 0); the offset for this frame
 * is the high byte of magnitude * amplitude, stored in `out`. Disabled, the
 * three layers' offsets `offsets` are cleared instead. */
static void shake_axis(Snes *s, uint16_t base, const uint16_t offsets[3], uint8_t random_base, uint16_t site, uint8_t out)
{
    Cpu *c = &s->cpu;
    uint16_t on = base, magnitude = (uint16_t)(base + 1), count = (uint16_t)(base + 2),
             period = (uint16_t)(base + 3), rate_count = (uint16_t)(base + 4), rate = (uint16_t)(base + 5),
             amplitude = (uint16_t)(base + 6), target = (uint16_t)(base + 7);
    if (m_lda8(s, abs8(s, on)) == 0) {
        m_tax(s);
        for (int i = 0; i < 3; i++)
            m_write16(s, m_abs(s, offsets[i]), c->x);
        m_write8(s, m_dp(s, out), (uint8_t)c->a);
        return;
    }
    step_abs8(s, count, +1);
    uint8_t n = m_lda8(s, abs8(s, count));
    m_cmp8(s, n, abs8(s, period));
    if (m_flag(s, CPU_FLAG_C)) {
        set_abs8(s, count, 0x00);
        if (random_base != 0)
            m_lda8(s, random_base);
        set_abs8(s, magnitude, shake_random(s, site));
    }
    m_write8(s, 0x004202, m_lda8(s, abs8(s, magnitude)));
    step_abs8(s, rate_count, +1);
    n = m_lda8(s, abs8(s, rate_count));
    m_cmp8(s, n, abs8(s, rate));
    if (m_flag(s, CPU_FLAG_C)) {
        set_abs8(s, rate_count, 0x00);
        uint8_t amp = m_lda8(s, abs8(s, amplitude));
        uint8_t goal = abs8(s, target);
        m_cmp8(s, amp, goal);
        if (amp > goal) {
            if (step_abs8(s, amplitude, -1) == 0)
                set_abs8(s, on, 0x00);
        } else if (amp < goal) {
            step_abs8(s, amplitude, +1);
        }
    }
    m_write8(s, 0x004203, m_lda8(s, abs8(s, amplitude)));
    m_write8(s, m_dp(s, out), m_lda8(s, m_read8(s, 0x004217)));
}

// $CD:0B51
/* Battle screen shake: compute this frame's horizontal ($CCB0) and vertical
 * ($CCB8) shake offsets, apply them to the layers selected by $CCC0 (bits 7-2:
 * BG1 X/Y, BG2 X/Y, BG3 X/Y at $CCA4-$CCAE), then, when $CCE5 is set, copy
 * $7E:CCD9 (12 bytes, to $0598; bit 7) or $7E:CCC1 (24 bytes, to $0580) and
 * clear that request bit. */
void battle_update_screen_shake(Snes *s)
{
    Cpu *c = &s->cpu;
    static const uint16_t x_offsets[3] = { 0xCCA4, 0xCCA8, 0xCCAC }, y_offsets[3] = { 0xCCA6, 0xCCAA, 0xCCAE };
    shake_axis(s, 0xCCB0, x_offsets, 0x00, 0x0B72, 0x45);
    shake_axis(s, 0xCCB8, y_offsets, 0x40, 0x0BD7, 0x47);

    /* $CD:0C19 */
    m_lda8(s, dp8(s, 0x45));
    m_set_index(s, &c->y, c->a);
    m_lda8(s, dp8(s, 0x47));
    m_tax(s);
    uint8_t layers = m_lda8(s, abs8(s, 0xCCC0));
    if (layers != 0) {
        for (int i = 0; i < 6; i++) {
            layers = m_asl8(s, layers);
            if (m_flag(s, CPU_FLAG_C))
                m_write16(s, m_abs(s, (uint16_t)(0xCCA4 + 2 * i)), (i & 1) ? c->x : c->y);
        }
        m_set_a8(s, layers);
    }

    /* $CD:0C48 */
    uint8_t preset = m_lda8(s, abs8(s, 0xCCE5));
    if (preset != 0) {
        m_rep(s, CPU_FLAG_M);
        bool vertical = preset & 0x80;
        m_ldx16(s, vertical ? 0xCCD9 : 0xCCC1);
        m_set_index(s, &c->y, vertical ? 0x0598 : 0x0580);
        m_lda16(s, vertical ? 0x000B : 0x0017);
        m_mvn(s, 0x7E, 0x7E);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        uint8_t v = m_lda8(s, abs8(s, 0xCCE5));
        set_abs8(s, 0xCCE5, m_lda8(s, (uint8_t)(v & (vertical ? 0x7F : 0xFE))));
    }
    m_rts(s);
}

// $CD:0AEE
/* Set up HDMA channel 7 for the window: indirect mode 4 to $2126 ($4370 = $44,
 * $4371 = $26), tables in $7E, from $C9CF on odd frames ($7C bit 0) and $C9A2
 * on even ones. */
void battle_setup_window_hdma(Snes *s)
{
    set_abs8(s, 0x4370, m_lda8(s, 0x44));
    set_abs8(s, 0x4371, m_lda8(s, 0x26));
    set_abs8(s, 0x4377, m_lda8(s, 0x7E));
    set_abs8(s, 0x4374, m_lda8(s, 0x7E));
    uint8_t frame = m_lda8(s, dp8(s, 0x7C));
    uint16_t table = m_lda8(s, frame & 0x01) ? 0xC9CF : 0xC9A2;
    m_write16(s, m_abs(s, 0x4372), m_ldx16(s, table));
    m_rts(s);
}

// $CD:0C89
/* Call $C0:0002 preserving the data bank, direct page and flags; returns with
 * A = the direct page. */
void battle_call_c00002(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_push16(s, c->d);
    m_push8(s, c->p);
    m_jsl(s, 0xCD0C8C, 0xC00002);
    c->p = m_pull8(s);
    if (c->p & CPU_FLAG_X) {
        c->x &= 0xFF;
        c->y &= 0xFF;
    }
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_tdc(s);
    m_rtl(s);
}

/* TXA : CLC : ADC #$0C : TAX -- next palette animation slot. */
static void next_palette_slot(Snes *s)
{
    m_txa(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_a8(s, m_adc8(s, (uint8_t)s->cpu.a, 0x0C));
    m_tax(s);
}

/* $D1:F411: save the type bytes of palette animation slots 0-7 to $CD2F. */
static void save_palette_slot_types(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    m_set_index(s, &c->y, c->a);
    do {
        m_write8(s, m_abs(s, 0xCD2F) + c->y, m_lda8(s, m_read8(s, m_abs(s, 0x0520) + c->x)));
        next_palette_slot(s);
        m_iny(s);
        m_cmp16(s, c->y, 0x0008);
    } while (c->y != 0x0008);
}

// $D1:F426
/* While $CFFF is set, pause palette animation once ($CE12): save the slot
 * types and stop the slots of type 1 or with bit 7 set; once $CFFF is clear
 * again, restore the saved types. Returns with RTL. */
void battle_pause_palette_animation(Snes *s)
{
    Cpu *c = &s->cpu;
    if (m_lda8(s, abs8(s, 0xCFFF)) != 0) {
        if (m_lda8(s, abs8(s, 0xCE12)) != 0) {
            m_rtl(s);
            return;
        }
        step_abs8(s, 0xCE12, +1);
        m_push16(s, 0xF43B);
        save_palette_slot_types(s);
        m_pull16(s);
        m_tdc(s);
        m_tax(s);
        do {
            uint8_t type = m_lda8(s, m_read8(s, m_abs(s, 0x0520) + c->x));
            bool stop = type & 0x80;
            if (!stop) {
                type = m_lda8(s, type & 0xF0);
                m_cmp8(s, type, 0x10);
                stop = type == 0x10;
            }
            if (stop)
                m_write8(s, m_abs(s, 0x0520) + c->x, 0x00);
            next_palette_slot(s);
            m_cmp16(s, c->x, 0x0060);
        } while (c->x != 0x0060);
        m_rtl(s);
        return;
    }
    if (m_lda8(s, abs8(s, 0xCE12)) == 0) {
        m_rtl(s);
        return;
    }
    set_abs8(s, 0xCE12, 0x00);
    m_tdc(s);
    m_tax(s);
    m_set_index(s, &c->y, c->a);
    do {
        m_write8(s, m_abs(s, 0x0520) + c->x, m_lda8(s, m_read8(s, m_abs(s, 0xCD2F) + c->y)));
        next_palette_slot(s);
        m_iny(s);
        m_cmp16(s, c->y, 0x0008);
    } while (c->y != 0x0008);
    m_rtl(s);
}

// $CD:04AA
/* The tail of the battle frame wait: unless paused, $CD:3B5F; the sound
 * driver poll ($CD:3E44); then, unless $CE11 is set, $CD:0453 (with $CCEA set
 * and $A013 clear) and a pending $D1:ECF3 request ($CD44 set, $CD43 clear,
 * value $CD45); and finally $CD:04DF. Returns with RTL. */
void battle_frame_wait_tail(Snes *s)
{
    if (m_lda8(s, abs8(s, 0xCD02)) == 0 && !jsr(s, 0xCD04AF, 0xCD3B5F))
        return;
    if (!jsr(s, 0xCD04B2, 0xCD3E44))
        return;
    if (m_lda8(s, abs8(s, 0xCE11)) == 0) {
        if (m_lda8(s, abs8(s, 0xCCEA)) != 0 && m_lda8(s, abs8(s, 0xA013)) == 0 && !jsr(s, 0xCD04C4, 0xCD0453))
            return;
        if (m_lda8(s, abs8(s, 0xCD44)) != 0 && m_lda8(s, abs8(s, 0xCD43)) == 0) {
            m_lda8(s, abs8(s, 0xCD45));
            if (!jsl(s, 0xCD04D4, 0xD1ECF3))
                return;
            set_abs8(s, 0xCD44, 0x00);
        }
    }
    if (!jsr(s, 0xCD04DB, 0xCD04DF))
        return;
    m_rtl(s);
}
