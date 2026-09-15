/* Battle: arithmetic helpers on the hardware multiplier. */
#include "battle/battle.h"

#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }

// $C1:0089
/* $AF (16-bit) = $AD * $AE, with the operands kept in $77/$78 (for the
 * interrupt handlers, which restore the multiplier from them). */
void battle_multiply_8x8(Snes *s)
{
    uint8_t a = m_lda8(s, dp8(s, 0xAD));
    set_dp8(s, 0x77, a);
    m_write8(s, 0x004202, a);
    uint8_t b = m_lda8(s, dp8(s, 0xAE));
    set_dp8(s, 0x78, b);
    m_write8(s, 0x004203, b);
    m_rep(s, CPU_FLAG_M);
    m_write16(s, m_dp(s, 0xAF), m_lda16(s, m_read16(s, 0x004216)));
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C1:00A7
/* $A9-$AB (24-bit) = $A5 * $A7-$A8 (16-bit), in two hardware multiplications,
 * with the last operands kept in $77/$78. */
void battle_multiply_8x16(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_tdc(s);
    m_push8(s, (uint8_t)c->a);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    uint8_t a = m_lda8(s, dp8(s, 0xA5));
    set_dp8(s, 0x77, a);
    m_write8(s, m_abs(s, 0x4202), a);
    uint8_t low = m_lda8(s, dp8(s, 0xA7));
    set_dp8(s, 0x78, low);
    m_write8(s, m_abs(s, 0x4203), low);
    m_push8(s, c->p);
    uint8_t high = m_lda8(s, dp8(s, 0xA8));
    set_dp8(s, 0xAB, 0x00);
    uint16_t product = m_ldx16(s, m_read16(s, m_abs(s, 0x4216)));
    set_dp8(s, 0x78, high);
    m_write8(s, m_abs(s, 0x4203), high);
    m_write16(s, m_dp(s, 0xA9), product);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
    uint16_t sum = m_lda16(s, m_read16(s, m_dp(s, 0xAA)));
    sum = m_adc16(s, sum, m_read16(s, m_abs(s, 0x4216)));
    m_write16(s, m_dp(s, 0xAA), sum);
    m_tdc(s);
    c->p = m_pull8(s);
    if (c->p & CPU_FLAG_X) {
        c->x &= 0xFF;
        c->y &= 0xFF;
    }
    m_sep(s, CPU_FLAG_M);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rts(s);
}

// $C1:C92A
/* 32 rounds of shift-and-subtract division on 16-bit direct-page values: the
 * bits of $28 (then $2E) shift into a remainder at $32 compared with the
 * divisor $2A, and the quotient bits collect in $2C. Nothing happens (both
 * results 0) when $28 or $2A is 0. Preserves P. */
void battle_divide(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_write16(s, m_dp(s, 0x2C), 0x0000);
    m_write16(s, m_dp(s, 0x32), 0x0000);
    if (m_lda16(s, m_read16(s, m_dp(s, 0x28))) != 0 && m_lda16(s, m_read16(s, m_dp(s, 0x2A))) != 0) {
        m_ldx16(s, 0x0020);
        do {
            static const uint8_t cells[3] = { 0x28, 0x2E, 0x32 };
            for (int i = 0; i < 3; i++) {
                uint32_t addr = m_dp(s, cells[i]);
                uint16_t v = m_read16(s, addr);
                bool carry = v & 0x8000;
                v = (uint16_t)(v << 1 | (m_flag(s, CPU_FLAG_C) ? 1 : 0));
                m_write16(s, addr, v);
                m_set_flag(s, CPU_FLAG_C, carry);
                m_set_nz16(s, v);
            }
            m_set_flag(s, CPU_FLAG_C, true);
            uint16_t r = m_sbc16(s, m_lda16(s, m_read16(s, m_dp(s, 0x32))), m_read16(s, m_dp(s, 0x2A)));
            m_write16(s, m_dp(s, 0x32), r);
            if (!m_flag(s, CPU_FLAG_C)) {
                r = m_adc16(s, m_lda16(s, m_read16(s, m_dp(s, 0x32))), m_read16(s, m_dp(s, 0x2A)));
                m_write16(s, m_dp(s, 0x32), r);
                m_set_flag(s, CPU_FLAG_C, false);
            }
            c->a = r;
            uint32_t q = m_dp(s, 0x2C);
            uint16_t v = m_read16(s, q);
            bool carry = v & 0x8000;
            v = (uint16_t)(v << 1 | (m_flag(s, CPU_FLAG_C) ? 1 : 0));
            m_write16(s, q, v);
            m_set_flag(s, CPU_FLAG_C, carry);
            m_set_nz16(s, v);
            m_ldx16(s, (uint16_t)(c->x - 1));
        } while (c->x != 0);
    }
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    c->p = m_pull8(s);
    if (c->p & CPU_FLAG_X) {
        c->x &= 0xFF;
        c->y &= 0xFF;
    }
    m_rts(s);
}

// $CD:0340
/* Battle button repeat: $CC97 gets the buttons ($F8) newly pressed this frame,
 * or all held buttons again after a delay ($CC9B: 15 frames at first, then
 * every frame; 24 while bit 15 is set), ORed into $EE. */
void battle_update_button_repeat(Snes *s)
{
    m_rep(s, CPU_FLAG_M);
    uint16_t last = m_lda16(s, m_read16(s, m_abs(s, 0xCC99)));
    m_write16(s, m_abs(s, 0xCC97), last);
    uint16_t held = m_lda16(s, m_read16(s, m_dp(s, 0xF8)));
    m_write16(s, m_abs(s, 0xCC99), held);
    uint16_t any = m_lda16(s, (uint16_t)(held | m_read16(s, m_abs(s, 0xCC97))));
    if (any == 0) {
        m_write16(s, m_abs(s, 0xCC97), any);
        m_write16(s, m_abs(s, 0xCC9B), m_lda16(s, 0x000F));
    } else {
        bool fresh = true;
        uint16_t both = m_lda16(s, (uint16_t)(m_lda16(s, m_read16(s, m_abs(s, 0xCC97))) & m_read16(s, m_abs(s, 0xCC99))));
        if (both != 0) {
            uint16_t count = (uint16_t)(m_read16(s, m_abs(s, 0xCC9B)) - 1);
            m_write16(s, m_abs(s, 0xCC9B), count);
            m_set_nz16(s, count);
            if (count == 0) {
                fresh = false;
                m_write16(s, m_abs(s, 0xCC9B), m_lda16(s, 0x0001));
                m_write16(s, m_abs(s, 0xCC97), m_lda16(s, m_read16(s, m_abs(s, 0xCC99))));
            }
        }
        if (fresh) {
            uint16_t v = m_lda16(s, (uint16_t)(m_lda16(s, m_read16(s, m_abs(s, 0xCC97))) ^ 0xFFFF));
            m_write16(s, m_abs(s, 0xCC97), m_lda16(s, (uint16_t)(v & m_read16(s, m_abs(s, 0xCC99)))));
        }
    }
    uint16_t out = m_lda16(s, (uint16_t)(m_lda16(s, m_read16(s, m_abs(s, 0xCC97))) | m_read16(s, m_dp(s, 0xEE))));
    m_write16(s, m_dp(s, 0xEE), out);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    if (m_lda8(s, m_read8(s, m_abs(s, 0xCC97))) & 0x80)
        m_write8(s, m_abs(s, 0xCC9B), m_lda8(s, 0x18));
    m_rts(s);
}

/* ROR on a 16-bit direct-page word; returns the bit shifted out. */
static bool ror_dp16(Snes *s, uint8_t cell)
{
    uint32_t addr = m_dp(s, cell);
    uint16_t v = m_read16(s, addr);
    bool out = v & 1;
    v = (uint16_t)(v >> 1 | (m_flag(s, CPU_FLAG_C) ? 0x8000 : 0));
    m_write16(s, addr, v);
    m_set_flag(s, CPU_FLAG_C, out);
    m_set_nz16(s, v);
    return out;
}

// $C1:C90B
/* 16-bit shift-and-add multiply: $2C-$2F (32-bit) = $28 * $2A, shifting $2A
 * out as it goes. */
void battle_multiply_16x16(Snes *s)
{
    Cpu *c = &s->cpu;
    m_rep(s, CPU_FLAG_M);
    m_ldx16(s, 0x0010);
    m_write16(s, m_dp(s, 0x2C), 0x0000);
    m_write16(s, m_dp(s, 0x2E), 0x0000);
    do {
        if (ror_dp16(s, 0x2A)) {
            m_set_flag(s, CPU_FLAG_C, false);
            uint16_t sum = m_adc16(s, m_lda16(s, m_read16(s, m_dp(s, 0x28))), m_read16(s, m_dp(s, 0x2E)));
            m_write16(s, m_dp(s, 0x2E), sum);
        }
        ror_dp16(s, 0x2E);
        ror_dp16(s, 0x2C);
        m_ldx16(s, (uint16_t)(c->x - 1));
    } while (c->x != 0);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}
