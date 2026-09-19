/* Controller input shared by the menus and battle. */
#include "core/input.h"

#include "platform/input.h"
#include "platform/timing.h"
#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }

/* PEA #v (the pulls follow separately) */
static void pea(Snes *s, uint16_t v) { m_push16(s, v); }

/* TSB on a 16-bit direct-page word with the 16-bit accumulator. */
static void tsb_dp16(Snes *s, uint8_t cell)
{
    uint16_t v = dp16(s, cell);
    m_set_flag(s, CPU_FLAG_Z, (v & s->cpu.a) == 0);
    set_dp16(s, cell, (uint16_t)(v | s->cpu.a));
}

// $C2:84EC
/* Read the controller: wait for the automatic joypad read to finish (the
 * port hands the wait back to the original at $C2:84F9), keep the previous
 * buttons in $FE, and store the held buttons in $F2 in the game's order
 * (A X L R B Y Select Start, then the direction pad in $F3). $F0 and $F4 get
 * the newly pressed ones; $F4 also gets held ones repeating, after $FD frames
 * (reloaded from $FC). L+R+Select+Start resets to $C0:FF00. Returns with 16-bit
 * A and 8-bit X, the direct page and data bank 0.
 *
 * Its callers don't read Y or the overflow (the entry's set), but the wait and
 * the reset do: its two stops declare their own sets. */
enum { STOP_DEAD = DC_X | DC_N | DC_Z | DC_C };
void input_read_joypad(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    pea(s, 0x0000);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    pea(s, 0x0000);
    c->db = m_pull8(s);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, 0x01);
    uint8_t status = tm_video_status(pf, PF_AT(0xC284F9));   /* BIT with A = $01 */
    m_set_flag(s, CPU_FLAG_N, status & 0x80);
    m_set_flag(s, CPU_FLAG_V, status & 0x40);
    m_set_flag(s, CPU_FLAG_Z, (status & TM_STATUS_JOYPAD_BUSY) == 0);
    if (status & TM_STATUS_JOYPAD_BUSY) {
        m_stop_at_own(s, 0xC284F9, STOP_DEAD);   /* BIT $4212 : BNE -- still reading */
        return;
    }
    m_ldx16(s, dp16(s, 0xF2));
    set_dp16(s, 0xFE, c->x);
    set_dp8(s, 0xF2, m_lda8(s, (uint8_t)(m_lda8(s, in_read_low(pf, PF_AT(0xC28502), 0)) & 0xF0)));
    set_dp8(s, 0xF3, m_lda8(s, (uint8_t)(m_lda8(s, in_read_high(pf, PF_AT(0xC28509), 0)) & 0x0F)));
    uint8_t high = m_lda8(s, in_read_high(pf, PF_AT(0xC28510), 0));
    for (int i = 0; i < 4; i++)
        high = m_lsr8(s, high);
    m_set_a8(s, high);
    m_tsb8(s, m_dp(s, 0xF2));
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    uint16_t pad = m_lda16(s, in_read(pf, PF_AT(0xC2851B), 0));
    m_cmp16(s, pad, 0x3030);
    if (pad == 0x3030) {
        m_stop_at_own(s, 0xC0FF00, STOP_DEAD);
        return;
    }
    uint16_t held = m_lda16(s, dp16(s, 0xF2));
    uint16_t pressed = m_lda16(s, (uint16_t)(held ^ dp16(s, 0xFE)));
    pressed = m_lda16(s, (uint16_t)(pressed & dp16(s, 0xF2)));
    set_dp16(s, 0xF0, pressed);
    set_dp16(s, 0xF4, pressed);
    m_sep(s, CPU_FLAG_X);
    m_set_index(s, &c->x, dp8(s, 0xFD));
    m_lda16(s, dp16(s, 0xF2));
    if (m_lda16(s, (uint16_t)(c->a & dp16(s, 0xFE))) != 0) {
        m_set_index(s, &c->x, (uint16_t)(c->x - 1));
        if (c->x == 0) {
            tsb_dp16(s, 0xF4);
            m_set_index(s, &c->x, dp8(s, 0xFC));
        }
    } else {
        m_set_index(s, &c->x, dp8(s, 0xFC));
    }
    set_dp8(s, 0xFD, (uint8_t)c->x);
    /* V is $4212's hblank bit from the BIT above, which native time doesn't
     * reproduce; nothing reads it after the return (the entry's set). */
    m_rts(s);
}

/* $C2:8555: remap one pair of button bytes ($F0,X; then X += 2). Each bit of
 * $F6,X is set when all the buttons of one assignment are down: bit 0 from
 * $2998, then $2996, $2997, $2994, $2999, $299A, $2995 and bit 7 from $2993
 * (shifted in with ROR). The direction byte is copied as is. */
static void map_button_byte(Snes *s)
{
    Cpu *c = &s->cpu;
    static const uint16_t order[8] = { 0x2998, 0x2996, 0x2997, 0x2994, 0x2999, 0x299A, 0x2995, 0x2993 };
    uint32_t out = m_dp(s, (uint8_t)(0xF6 + c->x));
    m_write8(s, out, 0x00);
    for (int i = 0; i < 8; i++) {
        uint8_t mask = m_read8(s, m_abs(s, order[i]) + c->y);
        uint8_t v = m_lda8(s, (uint8_t)(m_lda8(s, m_read8(s, m_dp(s, (uint8_t)(0xF0 + c->x)))) & mask));
        m_cmp8(s, v, mask);
        uint8_t o = m_read8(s, out);
        bool carry = o & 0x01;
        o = (uint8_t)(o >> 1 | (m_flag(s, CPU_FLAG_C) ? 0x80 : 0));
        m_write8(s, out, o);
        m_set_flag(s, CPU_FLAG_C, carry);
        m_set_nz8(s, o);
    }
    m_write8(s, m_dp(s, (uint8_t)(0xF7 + c->x)), m_lda8(s, m_read8(s, m_dp(s, (uint8_t)(0xF1 + c->x)))));
    m_set_index(s, &c->x, (uint16_t)(c->x + 1));
    m_set_index(s, &c->x, (uint16_t)(c->x + 1));
}

// $C2:8545
/* Remap the new ($F0), held ($F2) and repeating ($F4) buttons through the
 * player's button assignments into $F6, $F8 and $FA. */
void input_map_buttons(Snes *s)
{
    Cpu *c = &s->cpu;
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    pea(s, 0x7E7E);
    c->db = m_pull8(s);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_set_index(s, &c->y, 0x00);
    m_tyx(s);
    m_jsr_stack_bytes(s, 0x8551);
    map_button_byte(s);
    m_jsr_stack_bytes(s, 0x8554);
    map_button_byte(s);
    map_button_byte(s);
    m_rts(s);
}

// $C2:85AE
/* Advance the play time clock at $0400-$0405 (frames, seconds, minutes, ten
 * minutes, hours, ten hours; limits from $C2:85D0) unless it's stopped ($0407);
 * set $0406 when it overflows. Leaves the direct page at $0400. */
void play_time_tick(Snes *s)
{
    Cpu *c = &s->cpu;
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_push16(s, 0x0400);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    if (m_lda8(s, dp8(s, 0x07)) != 0) {
        m_rts(s);
        return;
    }
    m_set_index(s, &c->x, 0x00);
    do {
        uint8_t v = (uint8_t)(dp8(s, (uint8_t)c->x) + 1);
        set_dp8(s, (uint8_t)c->x, v);
        m_set_nz8(s, v);
        v = m_lda8(s, dp8(s, (uint8_t)c->x));
        uint8_t limit = m_read8(s, 0xC285D0 + c->x);
        m_cmp8(s, v, limit);
        if (v < limit) {
            m_rts(s);
            return;
        }
        set_dp8(s, (uint8_t)c->x, 0x00);
        m_set_index(s, &c->x, (uint16_t)(c->x + 1));
        m_cmp8(s, (uint8_t)c->x, 0x06);
    } while (c->x < 0x06);
    set_dp8(s, 0x06, m_lda8(s, 0x01));
    m_rts(s);
}

// $C2:84D2
/* The per-frame input work: read the controller ($C2:84EC), map the buttons
 * ($C2:8545) and advance the play time ($C2:85AE), preserving every register. */
void input_update(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_push16(s, c->d);
    if (m_x8(s)) {
        m_push8(s, (uint8_t)c->x);
        m_push8(s, (uint8_t)c->y);
    } else {
        m_push16(s, c->x);
        m_push16(s, c->y);
    }
    m_push8(s, c->p);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_push16(s, c->a);
    m_push8(s, c->p);
    m_jsr(s, 0xC284DB, 0xC284EC);
    m_jsr(s, 0xC284DE, 0xC28545);
    m_jsr(s, 0xC284E1, 0xC285AE);
    c->p = m_pull8(s);
    c->a = m_pull16(s);   /* PLA with the 16-bit width the PLP just restored */
    m_set_nz16(s, c->a);
    c->p = m_pull8(s);
    if (m_x8(s)) {
        c->y = m_pull8(s);
        m_set_nz8(s, (uint8_t)c->y);
        c->x = m_pull8(s);
        m_set_nz8(s, (uint8_t)c->x);
        c->y &= 0xFF;
        c->x &= 0xFF;
    } else {
        c->y = m_pull16(s);
        m_set_nz16(s, c->y);
        c->x = m_pull16(s);
        m_set_nz16(s, c->x);
    }
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rts(s);
}
