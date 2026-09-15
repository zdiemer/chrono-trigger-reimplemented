/* Scenes: scaled bitmap rows converted to 4-bit planar tile graphics. */
#include "scene/scene.h"

#include "port/machine.h"

static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }

/* DEC on a direct-page word, setting N and Z. */
static uint16_t dec_dp16(Snes *s, uint8_t cell)
{
    uint16_t v = (uint16_t)(dp16(s, cell) - 1);
    set_dp16(s, cell, v);
    m_set_nz16(s, v);
    return v;
}

/* Eight samples of the source row: each TXA : ADC $29 : TAX : XBA :
 * AND #$00FF : TAY : LDA ($31),Y : STA $3B+i steps the 8.8 fixed-point
 * position in X (the carry chaining from one addition to the next) and stores
 * the word at its integer part. Then LDY $2F : SEP #$20, and four bitplane
 * bytes at Y (LSR $3B+i : ROL A for each sample, lowest bit of each), then
 * REP #$20 : TYA : CLC : ADC #$0004 : STA $2F. */
static void convert_8_pixels(Snes *s)
{
    Cpu *c = &s->cpu;
    for (uint8_t i = 0; i < 8; i++) {
        m_txa(s);
        m_lda16(s, m_adc16(s, c->a, dp16(s, 0x29)));
        m_tax(s);
        m_xba(s);
        m_lda16(s, c->a & 0x00FF);
        m_set_index(s, &c->y, c->a);
        uint32_t ptr = m_abs(s, dp16(s, 0x31)) + c->y;
        m_lda16(s, m_read16(s, ptr));
        set_dp16(s, (uint8_t)(0x3B + i), c->a);
    }
    m_ldy(s, dp16(s, 0x2F));
    m_sep(s, CPU_FLAG_M);
    for (uint16_t plane = 0; plane < 4; plane++) {
        uint8_t a = (uint8_t)c->a;
        for (uint8_t i = 0; i < 8; i++) {
            uint32_t cell = m_dp(s, (uint8_t)(0x3B + i));
            uint8_t v = m_read8(s, cell);
            m_write8(s, cell, v >> 1);           /* LSR: C = bit 0 */
            uint8_t out = a & 0x80;              /* ROL A */
            a = (uint8_t)(a << 1 | (v & 1));
            m_set_flag(s, CPU_FLAG_C, out);
        }
        m_set_nz8(s, a);
        m_set_a8(s, a);
        m_write8(s, m_abs(s, plane) + c->y, a);
    }
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, c->y);
    m_set_flag(s, CPU_FLAG_C, false);
    m_lda16(s, m_adc16(s, c->a, 0x0004));
    set_dp16(s, 0x2F, c->a);
}

// $C3:0309
/* One row of a scaled bitmap rendered as 4-bit planar tile graphics in bank
 * $7F; the loop of the routine at $C3:0307, which starts it with dp $33 = 0.
 * Counting rows down in dp $25, a 16-bit accumulator (dp $33 plus the step in
 * dp $27) picks each output row's source row. When the rows are done, or that
 * row's position (the accumulator halved, rounded down to $40) is at or past
 * dp $2B, the rest of the output from dp $2D up to dp $37 is cleared, dp $37 is
 * set to dp $2D and the routine returns (RTL). Otherwise the row pointer dp
 * $31 becomes the position plus dp $23: if that's the previous pointer, the
 * previous 32 bytes are copied; otherwise dp $1F pairs of 8 samples are taken
 * with the 8.8 step in dp $29 and packed into four bitplane bytes each at dp
 * $2F. dp $2D and $2F then advance by 32 and the port stops at the loop head
 * for the next row, so each row is its own call. Entered with 16-bit A and X. */
void scene_scale_bitmap_row(Snes *s)
{
    Cpu *c = &s->cpu;
    bool done = dec_dp16(s, 0x25) == 0;
    if (!done) {
        m_lda16(s, dp16(s, 0x33));
        m_lda16(s, m_adc16(s, c->a, dp16(s, 0x27)));
        set_dp16(s, 0x33, c->a);
        c->a = m_lsr16(s, c->a);
        m_lda16(s, c->a & 0xFFC0);
        m_cmp16(s, c->a, dp16(s, 0x2B));
        done = !m_flag(s, CPU_FLAG_N);
    }
    if (done) {
        m_lda16(s, dp16(s, 0x37));
        m_lda16(s, m_sbc16(s, c->a, dp16(s, 0x2D)));
        if (!m_flag(s, CPU_FLAG_N) && !m_flag(s, CPU_FLAG_Z)) {
            m_ldx(s, dp16(s, 0x2D));
            m_write16(s, m_abs(s, 0x0000) + c->x, 0x0000);
            m_txy(s);
            m_iny(s);
            m_mvn(s, 0x7F, 0x7F);
        }
        m_lda16(s, dp16(s, 0x2D));
        set_dp16(s, 0x37, c->a);
        m_rtl(s);
        return;
    }

    m_set_flag(s, CPU_FLAG_C, false);
    m_lda16(s, m_adc16(s, c->a, dp16(s, 0x23)));
    m_cmp16(s, c->a, dp16(s, 0x31));
    if (m_flag(s, CPU_FLAG_Z)) {
        /* The same source row as the last: copy the previous output row. */
        m_lda16(s, dp16(s, 0x2D));
        m_set_index(s, &c->y, c->a);
        m_set_flag(s, CPU_FLAG_C, true);
        m_lda16(s, m_sbc16(s, c->a, 0x0020));
        m_tax(s);
        m_lda16(s, 0x001F);
        m_mvn(s, 0x7F, 0x7F);
    } else {
        set_dp16(s, 0x31, c->a);
        m_ldx(s, 0x0000);
        m_lda16(s, dp16(s, 0x1F));
        set_dp16(s, 0x35, c->a);
        do {
            convert_8_pixels(s);
            convert_8_pixels(s);
        } while (dec_dp16(s, 0x35) != 0);
    }

    /* $C3:051C: LDA $2D : CLC : ADC #$0020 : STA $2D : STA $2F : JMP $0309 */
    m_lda16(s, dp16(s, 0x2D));
    m_set_flag(s, CPU_FLAG_C, false);
    m_lda16(s, m_adc16(s, c->a, 0x0020));
    set_dp16(s, 0x2D, c->a);
    set_dp16(s, 0x2F, c->a);
    c->pc = 0x0309;
}
