/* Battle: the main loop in bank C1.
 *
 * The battle is started with a JML to $C1:8000, which jumps to the setup at
 * $C1:8006. The loop proper runs from $C1:8120 to the JMP $8120 at $C1:83B5,
 * once per pass of the battle's work routine ($C1:0003 with A = 3, which waits
 * for frames); it leaves through $C1:83BB or $C1:8423 to $C1:8443, which jumps
 * to $C1:0006. Its two timer stretches ($C1:8231-$C1:8344) are native entries
 * of their own (battle/timers.c), so the ports here stop where they begin.
 *
 * Every port runs at 8-bit A and 16-bit X; after each call-out the widths (and
 * binary mode) are checked, and if a callee changed them the port stops at the
 * return address and the original carries on. */
#include "battle/main_loop.h"

#include "port/machine.h"

static uint8_t abs8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr)); }
static void set_abs8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr), v); }
static uint8_t absx8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.x); }
static void set_absx8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.x, v); }
static uint8_t absy8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.y); }
static void set_absy8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.y, v); }
static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }

/* LDA abs (8-bit) */
static uint8_t lda_abs(Snes *s, uint16_t addr) { return m_lda8(s, abs8(s, addr)); }

/* LDA long,X (8-bit), the script bytes in bank CC */
static uint8_t lda_cc(Snes *s, uint16_t offset) { return m_lda8(s, m_read8(s, 0xCC0000u + offset + s->cpu.x)); }

/* INC/DEC on an 8-bit memory byte, setting N and Z. */
static uint8_t step8(Snes *s, uint32_t addr, int delta)
{
    uint8_t v = (uint8_t)(m_read8(s, addr) + delta);
    m_write8(s, addr, v);
    m_set_nz8(s, v);
    return v;
}

/* DEC A (8-bit) */
static uint8_t dec_a8(Snes *s) { return m_lda8(s, (uint8_t)(s->cpu.a - 1)); }

/* TDC : TAX */
static void clear_ax(Snes *s)
{
    m_tdc(s);
    m_tax(s);
}

/* SEC : SBC #$03 (8-bit), result in A */
static uint8_t minus3(Snes *s)
{
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t v = m_sbc8(s, (uint8_t)s->cpu.a, 0x03);
    m_set_a8(s, v);
    return v;
}

/* LDA $2989 : BIT #$20 -- true if bit 5 is set. */
static bool flag_2989_bit5(Snes *s)
{
    lda_abs(s, 0x2989);
    m_bit_imm8(s, 0x20);
    return s->cpu.a & 0x20;
}

/* REP #$20 : XBA : LSR A : TAX : TDC : SEP #$20 -- X = slot * $80 from the
 * slot in A (and whatever D's high byte left in B). */
static void slot_to_stat_block(Snes *s)
{
    m_rep(s, CPU_FLAG_M);
    m_xba(s);
    s->cpu.a = m_lsr16(s, s->cpu.a);
    m_tax(s);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
}

static bool widths_ok(const Snes *s)
{
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D)) == CPU_FLAG_M;
}

/* JSR/JSL out; false if the callee didn't come back with 8-bit A, 16-bit X and
 * binary mode (the port then stops at the return address). */
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

/* ---- the loop's exits ----------------------------------------------------------- */

/* REP #$20 : copy the words at $B316, $B318, $B31A to $5E32, $5EB2, $5F32 (the
 * three party stat blocks' $32) : TDC : SEP #$20 */
static void restore_party_5e32(Snes *s)
{
    m_rep(s, CPU_FLAG_M);
    for (uint16_t i = 0; i < 3; i++)
        m_write16(s, m_abs(s, (uint16_t)(0x5E32 + i * 0x80)),
                  m_lda16(s, m_read16(s, m_abs(s, (uint16_t)(0xB316 + i * 2)))));
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
}

/* $C1:8443: restore $29AD from $26, copy $1C48, $1C4D, $1C52 to the party stat
 * blocks' $56, call $FD:AAB0 and stop at the JMP $0006 that leaves the battle. */
static void leave_8443(Snes *s)
{
    set_abs8(s, 0x29AD, m_lda8(s, dp8(s, 0x26)));
    set_abs8(s, 0x5E56, lda_abs(s, 0x1C48));
    set_abs8(s, 0x5ED6, lda_abs(s, 0x1C4D));
    set_abs8(s, 0x5F56, lda_abs(s, 0x1C52));
    if (!jsl(s, 0xC1845A, 0xFDAAB0))
        return;
    s->cpu.pc = 0x0006;
}

/* $C1:8423 */
static void leave_8423(Snes *s)
{
    restore_party_5e32(s);
    if (!jsr(s, 0xC1843A, 0xC1EA9D) || !jsr(s, 0xC1843D, 0xC1EAE8) || !jsr(s, 0xC18440, 0xC1B442))
        return;
    leave_8443(s);
}

/* $C1:83BB */
static void leave_83bb(Snes *s)
{
    Cpu *c = &s->cpu;
    restore_party_5e32(s);
    if (flag_2989_bit5(s)) {
        leave_8423(s);
        return;
    }
    uint8_t v = lda_abs(s, 0x2880);
    m_cmp8(s, v, 0x00);
    if (v == 0x00) {
        if (!jsl(s, 0xC183E0, 0xFDB201))
            return;
        m_set_index(s, &c->y, m_read16(s, m_abs(s, 0xB2A5)));
        if (!jsr(s, 0xC183E7, 0xC1D0A2))
            return;
        set_abs8(s, 0xB2AF, m_lda8(s, (uint8_t)(lda_abs(s, 0xB2AF) | 0x40)));
        if (!jsr(s, 0xC183F2, 0xC1F93E))
            return;
    }
    if (!jsl(s, 0xC183F5, 0xFDAD17) || !jsr(s, 0xC183F9, 0xC1EAE8) || !jsr(s, 0xC183FC, 0xC1B442) ||
        !jsr(s, 0xC183FF, 0xC1B4E9))
        return;
    clear_ax(s);
    m_write16(s, m_dp(s, 0x10), c->x);
    uint8_t i;
    do {
        m_ldx16(s, dp16(s, 0x10));
        if (m_lda8(s, absx8(s, 0xB2A7)) != 0) {
            m_set_index(s, &c->y, c->a);
            if (!jsr(s, 0xC1840E, 0xC1D005))
                return;
            set_abs8(s, 0xB2AF, m_lda8(s, (uint8_t)(lda_abs(s, 0xB2AF) | 0x20)));
        }
        step8(s, m_dp(s, 0x10), +1);
        i = m_lda8(s, dp8(s, 0x10));
        m_cmp8(s, i, 0x06);
    } while (i < 0x06);
    leave_8443(s);
}

/* ---- entries ------------------------------------------------------------------ */

// $C1:8000
/* Battle start: JMP $8006, then set up the loop. Calls $FD:A982, saves $29AD in
 * $26 and $B3E6, calls $C1:FA8B, moves bit 5 of $2989 into $B31F, runs battle
 * work 0 ($C1:0003). Clears $B186 and fills $B163/$B16E (11 entries) with $FF.
 * Lists in $B16E the party slots whose $AEFF isn't negative ($B1BE + 1 each),
 * then $AEC6 + 3 counting down to 4; then draws $AEC7 entries into $B163 (each
 * minus 1) in the order of the indices $C1:AF22 returns, marking the drawn ones
 * $FF. Sets $B23A to $FF and copies $AF0D to $B16E (8 entries), and draws those
 * into $B23A the same way until none below $AEC6 is left. Fills $B242 (8)
 * with $FF, clears $B252, calls $C1:8C09, ORs $B31F back into $2989, and when
 * its bit 5 is set sets $AFAB-$AFAD to 1 and $AFAE-$AFB5 to $FF. Stops at the
 * loop head, $C1:8120. */
void battle_main_loop_start(Snes *s)
{
    Cpu *c = &s->cpu;
    if (!jsl(s, 0xC18006, 0xFDA982))
        return;
    uint8_t v = lda_abs(s, 0x29AD);
    m_write8(s, m_dp(s, 0x26), v);
    set_abs8(s, 0xB3E6, v);
    if (!jsr(s, 0xC18012, 0xC1FA8B))
        return;
    m_sep(s, CPU_FLAG_M);
    m_rep(s, CPU_FLAG_X);
    set_abs8(s, 0xB31F, 0x00);
    set_abs8(s, 0xB31F, m_lda8(s, (uint8_t)(lda_abs(s, 0x2989) & 0x20)));
    set_abs8(s, 0x2989, m_lda8(s, (uint8_t)(lda_abs(s, 0x2989) & 0xDF)));
    m_tdc(s);
    if (!jsr(s, 0xC1802D, 0xC10003))
        return;
    clear_ax(s);
    m_write16(s, m_abs(s, 0xB186), c->x);
    m_lda8(s, 0xFF);
    do {
        set_absx8(s, 0xB163, (uint8_t)c->a);
        set_absx8(s, 0xB16E, (uint8_t)c->a);
        m_inx(s);
        m_cmp16(s, c->x, 0x000B);
    } while (c->x != 0x000B);

    /* $C1:8043: party slots present */
    clear_ax(s);
    m_set_index(s, &c->y, c->a);
    m_write16(s, m_dp(s, 0x00), c->x);
    do {
        m_ldx16(s, dp16(s, 0x00));
        if (!(m_lda8(s, absx8(s, 0xAEFF)) & 0x80)) {
            m_tax(s);
            m_lda8(s, (uint8_t)(m_lda8(s, absx8(s, 0xB1BE)) + 1));
            m_ldx16(s, dp16(s, 0x00));
            set_absy8(s, 0xB16E, (uint8_t)c->a);
            m_iny(s);
        }
        step8(s, m_dp(s, 0x00), +1);
        v = m_lda8(s, dp8(s, 0x00));
        m_cmp8(s, v, 0x03);
    } while (v < 0x03);

    /* $C1:8062 */
    m_tyx(s);
    lda_abs(s, 0xAEC6);
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_a8(s, m_adc8(s, (uint8_t)c->a, 0x03));
    do {
        set_absx8(s, 0xB16E, (uint8_t)c->a);
        m_inx(s);
        v = dec_a8(s);
        m_cmp8(s, v, 0x03);
    } while (v != 0x03);

    /* $C1:8072: draw $AEC7 entries */
    clear_ax(s);
    m_set_index(s, &c->y, c->a);
    for (;;) {
        m_push16(s, c->x);
        clear_ax(s);
        lda_abs(s, 0xAEC7);
        if (!jsr(s, 0xC1807B, 0xC1AF22))
            return;
        c->x = m_pull16(s);
        m_set_nz16(s, c->x);
        m_set_index(s, &c->y, c->a);
        v = m_lda8(s, absy8(s, 0xB16E));
        m_cmp8(s, v, 0xFF);
        if (v == 0xFF)
            continue;
        set_absx8(s, 0xB163, dec_a8(s));
        set_absy8(s, 0xB16E, m_lda8(s, 0xFF));
        m_inx(s);
        m_txa(s);
        uint8_t count = abs8(s, 0xAEC7);
        m_cmp8(s, (uint8_t)c->a, count);
        if ((uint8_t)c->a >= count)
            break;
    }

    /* $C1:8097 */
    clear_ax(s);
    do {
        set_absx8(s, 0xB23A, m_lda8(s, 0xFF));
        set_absx8(s, 0xB16E, m_lda8(s, absx8(s, 0xAF0D)));
        m_inx(s);
        m_cmp16(s, c->x, 0x0008);
    } while (c->x < 0x0008);

    /* $C1:80AA: draw them into $B23A */
    clear_ax(s);
    m_set_index(s, &c->y, c->a);
    for (;;) {
        clear_ax(s);
        m_lda8(s, 0x08);
        if (!jsr(s, 0xC180B1, 0xC1AF22))
            return;
        m_tax(s);
        v = m_lda8(s, absx8(s, 0xB16E));
        m_cmp8(s, v, 0xFF);
        if (v == 0xFF)
            continue;   /* BEQ $80AD: draw again */
        set_absx8(s, 0xB16E, m_lda8(s, 0xFF));
        m_txa(s);
        set_absy8(s, 0xB23A, (uint8_t)c->a);
        m_iny(s);
        clear_ax(s);
        for (;;) {
            v = m_lda8(s, absx8(s, 0xB16E));
            m_cmp8(s, v, 0xFF);
            if (v != 0xFF)
                break;   /* carry clear */
            m_inx(s);
            m_txa(s);
            uint8_t limit = abs8(s, 0xAEC6);
            m_cmp8(s, (uint8_t)c->a, limit);
            if ((uint8_t)c->a >= limit)
                break;   /* carry set */
        }
        if (m_flag(s, CPU_FLAG_C))
            break;   /* $C1:80D6 BCC $80AD */
    }

    /* $C1:80D8 */
    clear_ax(s);
    m_lda8(s, 0xFF);
    do {
        set_absx8(s, 0xB242, (uint8_t)c->a);
        m_inx(s);
        m_cmp16(s, c->x, 0x0008);
    } while (c->x < 0x0008);
    set_abs8(s, 0xB252, 0x00);
    if (!jsr(s, 0xC180E8, 0xC18C09))
        return;
    set_abs8(s, 0x2989, m_lda8(s, (uint8_t)(lda_abs(s, 0x2989) | abs8(s, 0xB31F))));
    if (flag_2989_bit5(s)) {
        m_lda8(s, 0x01);
        for (uint16_t i = 0; i < 3; i++)
            set_abs8(s, (uint16_t)(0xAFAB + i), 0x01);
        m_lda8(s, 0xFF);
        for (uint16_t i = 0; i < 8; i++)
            set_abs8(s, (uint16_t)(0xAFAE + i), 0xFF);
    }
    c->pc = 0x8120;
}

// $C1:8120
/* Head of the battle main loop: count $0027 up, clear $B2C7 and run battle
 * work 3 ($C1:0003, which waits for frames). Stops at the return, $C1:812C,
 * where battle_main_loop_body takes over. */
void battle_main_loop(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    step8(s, m_abs(s, 0x0027), +1);
    set_abs8(s, 0xB2C7, (uint8_t)c->a);
    m_lda8(s, 0x03);
    m_jsr(s, 0xC18129, 0xC10003);
}

/* $C1:81CF / $C1:883D: for each party slot ($AE5A) whose $AF0A isn't $FF, call
 * $C1:B3F9 with the slot in X (with `with_b575`, first $C1:B575 with the slot
 * in $14). */
static bool update_party_slots(Snes *s, bool with_b575)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    set_abs8(s, 0xAE5A, 0x00);
    uint8_t n;
    do {
        m_tax(s);
        uint8_t v = m_lda8(s, absx8(s, 0xAF0A));
        m_cmp8(s, v, 0xFF);
        if (v != 0xFF) {
            if (with_b575) {
                m_write16(s, m_dp(s, 0x14), c->x);
                if (!jsr(s, 0xC1884B, 0xC1B575))
                    return false;
                m_tdc(s);
                lda_abs(s, 0xAE5A);
                m_tax(s);
                if (!jsr(s, 0xC18853, 0xC1B3F9))
                    return false;
            } else if (!jsr(s, 0xC181DB, 0xC1B3F9)) {
                return false;
            }
        }
        m_tdc(s);
        step8(s, m_abs(s, 0xAE5A), +1);
        n = lda_abs(s, 0xAE5A);
        m_cmp8(s, n, 0x03);
    } while (n < 0x03);
    return true;
}

// $C1:812C
/* One pass of the battle main loop after the work routine: clears the $B328
 * table ($FD:ACFD) and updates all combatants ($C1:B223). If $24 is set ($C1:B223
 * sets it when all three party slots' $AEFF are negative), one exit: $C1:EA9D,
 * $C1:B3BB, $7F:01EC = 1, $C1:B7F2, eight passes of battle work 3 with $A10E
 * set, battle work 9, then $C1:8423. If each of the 8 enemy slots ($AF02) is
 * $FF or has $AF15 bit 6 set, another: $C1:B3BB, $7F:01EC = 0, $A5A9 = $FF,
 * $C1:EA9D, battle work 8, then $C1:83BB. Otherwise the combatants' status flags ($C1:B093 for the
 * party, $C1:B0B6 for all 11). With $99E0 set, $C1:B3F9 for each party slot and
 * on to $C1:8345. With $2989 bit 5 clear and $99CD set, a third exit through
 * $FD:AA98, $CD:0021 and seven routines to $C1:8423. Otherwise, with $AF25 set
 * it calls $C1:8461 and stops at $C1:8345; with $AF25 clear it stops at the
 * timers, $C1:8231 (16-bit A). */
void battle_main_loop_body(Snes *s)
{
    Cpu *c = &s->cpu;
    if (!jsl(s, 0xC1812C, 0xFDACFD) || !jsr(s, 0xC18130, 0xC1B223))
        return;
    uint8_t v;
    if (m_lda8(s, dp8(s, 0x24)) != 0) {
        /* $C1:8137 */
        if (!jsr(s, 0xC18137, 0xC1EA9D) || !jsr(s, 0xC1813A, 0xC1B3BB))
            return;
        m_write8(s, 0x7F01EC, m_lda8(s, 0x01));
        if (!jsr(s, 0xC18143, 0xC1B7F2))
            return;
        set_abs8(s, 0xA10E, m_lda8(s, 0x01));
        clear_ax(s);
        m_write16(s, m_dp(s, 0x00), c->x);
        do {
            m_lda8(s, 0x03);
            if (!jsr(s, 0xC18151, 0xC10003))
                return;
            step8(s, m_dp(s, 0x00), +1);
            v = m_lda8(s, dp8(s, 0x00));
            m_cmp8(s, v, 0x08);
        } while (v < 0x08);
        set_abs8(s, 0xA10E, 0x00);
        m_lda8(s, 0x09);
        if (!jsr(s, 0xC18161, 0xC10003))
            return;
        leave_8423(s);
        return;
    }

    /* $C1:8167 */
    clear_ax(s);
    bool enemy_left = false;
    do {
        v = m_lda8(s, absx8(s, 0xAF02));
        m_cmp8(s, v, 0xFF);
        if (v != 0xFF) {
            m_lda8(s, absx8(s, 0xAF15));
            m_bit_imm8(s, 0x40);
            if (!(c->a & 0x40)) {
                enemy_left = true;
                break;
            }
        }
        m_inx(s);
        m_cmp16(s, c->x, 0x0008);
    } while (c->x < 0x0008);
    if (!enemy_left) {
        /* $C1:8176 */
        if (!jsr(s, 0xC18176, 0xC1B3BB))
            return;
        m_tdc(s);
        m_write8(s, 0x7F01EC, (uint8_t)c->a);
        set_abs8(s, 0xA5A9, m_lda8(s, 0xFF));
        if (!jsr(s, 0xC18183, 0xC1EA9D))
            return;
        m_lda8(s, 0x08);
        if (!jsr(s, 0xC18188, 0xC10003))
            return;
        leave_83bb(s);
        return;
    }

    /* $C1:8195 */
    clear_ax(s);
    m_set_index(s, &c->y, c->a);
    static const uint32_t sites[3][2] = {
        { 0xC18198, 0xC1819B }, { 0xC181A4, 0xC181A7 }, { 0xC181B0, 0xC181B3 },
    };
    for (uint16_t i = 0; i < 3; i++) {
        if (i > 0) {
            m_ldx16(s, (uint16_t)(i * 0x80));
            m_set_index(s, &c->y, i);
        }
        if (!jsr(s, sites[i][0], 0xC1B093) || !jsr(s, sites[i][1], 0xC1B0B6))
            return;
    }
    do {
        /* $C1:81B6 */
        m_rep(s, CPU_FLAG_M);
        m_txa(s);
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, 0x0080);
        m_tax(s);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        m_iny(s);
        if (!jsr(s, 0xC181C2, 0xC1B0B6))
            return;
        m_cmp16(s, c->y, 0x000A);
    } while (c->y < 0x000A);

    /* $C1:81CA */
    if (lda_abs(s, 0x99E0) != 0) {
        if (update_party_slots(s, false))
            c->pc = 0x8345;
        return;
    }
    if (!flag_2989_bit5(s) && lda_abs(s, 0x99CD) != 0) {
        /* $C1:81F8 */
        m_tdc(s);
        m_write8(s, 0x7F01EC, (uint8_t)c->a);
        if (!jsl(s, 0xC181FD, 0xFDAA98))
            return;
        m_lda8(s, 0x08);
        if (!jsl(s, 0xC18203, 0xCD0021) || !jsr(s, 0xC18207, 0xC1EA9D) || !jsr(s, 0xC1820A, 0xC1B7F2))
            return;
        m_lda8(s, 0x79);
        if (!jsr(s, 0xC1820F, 0xC1895B) || !jsr(s, 0xC18212, 0xC1EAE8) || !jsr(s, 0xC18215, 0xC1B442) ||
            !jsr(s, 0xC18218, 0xC1B4E9) || !jsr(s, 0xC1821B, 0xC1B3BB))
            return;
        leave_8423(s);
        return;
    }

    /* $C1:8221 */
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    if (m_lda16(s, m_read16(s, m_abs(s, 0xAF25))) == 0) {
        c->pc = 0x8231;
        return;
    }
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    if (!jsr(s, 0xC1822B, 0xC18461))
        return;
    c->pc = 0x8345;
}

// $C1:8345
/* The end of a main loop pass, after the timers. When the ROM byte at
 * $C1:FFFF is set and the controller's $421B bit 5 is pressed, a debugging
 * path at $C1:8356 (left to the original; the byte is 0 in this ROM). With
 * $2989 bit 5 clear, stops at the loop head $C1:8120; otherwise calls $C1:B3D2
 * and leaves through $C1:83BB: restores the party's $5E32 words, and unless
 * $2989 bit 5 is set runs the closing routines ($FD:B201 and $C1:D0A2 when
 * $2880 is 0, $C1:D005 for each nonzero $B2A7 of 6), then $C1:8443 stops at
 * the JMP $0006. */
void battle_main_loop_tail(Snes *s)
{
    Cpu *c = &s->cpu;
    if (m_lda8(s, m_read8(s, 0xC1FFFF)) != 0) {
        uint8_t pad = m_lda8(s, m_read8(s, 0x00421B));
        if (m_lda8(s, pad & 0x20) != 0) {
            c->pc = 0x8356;
            return;
        }
    }
    if (!flag_2989_bit5(s)) {
        c->pc = 0x8120;
        return;
    }
    if (!jsr(s, 0xC183B8, 0xC1B3D2))
        return;
    leave_83bb(s);
}

/* $C1:8862: clear the four bytes of $B192 + $B18B * 4. */
static void clear_b192_entry(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_set_index(s, &c->y, c->a);
    uint8_t v = m_asl8(s, m_asl8(s, lda_abs(s, 0xB18B)));
    m_set_a8(s, v);
    m_tax(s);
    m_tdc(s);
    do {
        set_absx8(s, 0xB192, (uint8_t)c->a);
        m_inx(s);
        m_iny(s);
        m_cmp16(s, c->y, 0x0004);
    } while (c->y < 0x0004);
}

/* JSR $8862 from `site`, inlined with its stack bytes. */
static void call_clear_b192_entry(Snes *s, uint16_t site)
{
    m_push16(s, (uint16_t)(site + 2));
    clear_b192_entry(s);
    m_pull16(s);
}

// $C1:8461
/* The handler of the thirteenth timer kind ($B186 bit 0), also called from
 * the main loop while $AF25 is set. Clears $B2C0 and $B3B9. When $0027 bit 0
 * is set: clears $AEB2-$AEBA and takes the slot in $B163 before the kind's
 * cursor ($B179 + 12). An enemy slot (3 and up) that is present ($AEFF),
 * has no $AFAB and passes the $5E0A/$9826/$B247 checks gets $AEC8 = slot - 3,
 * $C1:8CF9, $AFAB from $B158, $C1:BD6F and $B03A = 1; otherwise, when one of
 * $B188-$B18A is negative, $B3BE cycles 2, 1, 0 into $14. A party slot whose
 * $B03A is negative calls $FD:AB30, then either $C1:B575 and $C1:BCE1 (with
 * $AF23 set) or, unless its $93EE + slot * 7 flag is set, sets that flag
 * (through $C1:B70E instead when $2989 bit 5 is set) and runs battle work 1.
 * Negative $93EE/$93F5/$93FC are copied to $B188-$B18A; the queue $99D4-$99D6
 * is shifted into $B18B and, when that member's flag differs in sign from its
 * copy, $C1:B967 runs and both are cleared. Then $C1:883D ($C1:B575 and
 * $C1:B3F9 per party slot); $B186 bit 0 is cleared when $B188-$B18A are all
 * 0. Then, always: $C1:B223, $B3AC = $AD8E (11 bytes), $B3B7 = $B18B,
 * $C1:B762. Unless $2989 bit 5 is set, $B2C0 is 0 ($FD:AC6E instead) or a
 * status bit of $B3B7's stat block is set, for each of the 8 entries of $B3AC
 * ($B315) naming an enemy: $C1:B488 finds its script position ($B1D2) in bank
 * CC, and command by command (script byte 0 through the handler table at
 * $C1:B80D, and byte 4 unless it is $FE) until $AF24 says the script moved on
 * to an $FF: $C1:8CF9, $C1:B223, $B1D4 + entry * 2 = $B3CA, $B24A = $AF24.
 * Each entry ends with $C1:B575 and clearing its $B192 entry. Finishes with
 * $C1:AC5E, $C1:8C09 (when $24 is clear), $C1:AC46 and $FD:ACEE. */
void battle_run_queued_scripts(Snes *s)
{
    Cpu *c = &s->cpu;
    uint8_t v;
    m_tdc(s);
    set_abs8(s, 0xB2C0, (uint8_t)c->a);
    set_abs8(s, 0xB3B9, (uint8_t)c->a);
    lda_abs(s, 0x0027);
    m_bit_imm8(s, 0x01);
    if (!(c->a & 0x01))
        goto l8626;

    /* $C1:8472 */
    clear_ax(s);
    do {
        set_absx8(s, 0xAEB2, (uint8_t)c->a);
        m_inx(s);
        m_cmp16(s, c->x, 0x0009);
    } while (c->x < 0x0009);
    m_tdc(s);
    m_ldx16(s, 0x000C);
    if (m_lda8(s, absx8(s, 0xB179)) == 0)
        lda_abs(s, 0xAEC7);
    dec_a8(s);
    m_tax(s);
    m_lda8(s, absx8(s, 0xB163));
    m_tax(s);
    m_write16(s, m_dp(s, 0x22), c->x);
    v = (uint8_t)c->a;
    m_cmp8(s, v, 0x03);
    if (v < 0x03)
        goto l8511;

    /* $C1:8495: an enemy slot */
    m_ldx16(s, dp16(s, 0x22));
    v = m_lda8(s, absx8(s, 0xAEFF));
    m_cmp8(s, v, 0xFF);
    if (v == 0xFF || m_lda8(s, absx8(s, 0xAFAB)) != 0)
        goto l84ec;
    v = m_lda8(s, absx8(s, 0x5E0A));
    if (v == 0)
        goto l84bd;
    m_cmp8(s, v, 0x04);
    if (v == 0x04)
        goto l84bd;
    m_cmp8(s, v, 0x06);
    if (v == 0x06)
        goto l84bd;
    m_cmp8(s, v, 0x0A);
    if (v >= 0x0A) {
        m_cmp8(s, v, 0x11);
        if (v < 0x11)
            goto l84bd;
    }
    if (m_lda8(s, absx8(s, 0x9826)) == 0)
        goto l84ec;
l84bd:
    set_absx8(s, 0x9826, 0x00);
    if (m_lda8(s, absx8(s, 0xB247)) != 0)
        goto l84ec;
    m_ldx16(s, dp16(s, 0x22));
    v = m_lda8(s, absx8(s, 0xAEFF));
    m_cmp8(s, v, 0xFF);
    if (v == 0xFF)
        goto l84ec;
    m_lda8(s, dp8(s, 0x22));
    set_abs8(s, 0xAEC8, minus3(s));
    if (!jsr(s, 0xC184D6, 0xC18CF9))
        return;
    m_ldx16(s, dp16(s, 0x22));
    set_absx8(s, 0xAFAB, m_lda8(s, absx8(s, 0xB158)));
    if (!jsr(s, 0xC184E1, 0xC1BD6F))
        return;
    set_absx8(s, 0xB03A, m_lda8(s, 0x01));
    goto l860a;

l84ec:
    m_tdc(s);
    v = lda_abs(s, 0xB188);
    v = m_lda8(s, (uint8_t)(v | abs8(s, 0xB189)));
    v = m_lda8(s, (uint8_t)(v | abs8(s, 0xB18A)));
    if (!(v & 0x80))
        goto l881c;
    step8(s, m_abs(s, 0xB3BE), -1);
    if (lda_abs(s, 0xB3BE) & 0x80)
        set_abs8(s, 0xB3BE, m_lda8(s, 0x02));
    lda_abs(s, 0xB3BE);
    m_tax(s);
    m_write16(s, m_dp(s, 0x14), c->x);
    goto l8570;

l8511:
    /* a party slot */
    m_ldx16(s, dp16(s, 0x22));
    if (!(m_lda8(s, absx8(s, 0xB03A)) & 0x80))
        goto l860a;
    m_lda8(s, dp8(s, 0x22));
    m_set_index(s, &c->y, c->a);
    slot_to_stat_block(s);
    if (!jsl(s, 0xC18526, 0xFDAB30))
        return;
    if (lda_abs(s, 0xAF23) != 0) {
        v = m_lda8(s, dp8(s, 0x22));
        set_abs8(s, 0xB3F4, v);
        m_write8(s, m_dp(s, 0x14), v);
        m_write8(s, m_dp(s, 0x15), 0x00);
        if (!jsr(s, 0xC18538, 0xC1B575) || !jsr(s, 0xC1853B, 0xC1BCE1))
            return;
        goto l860a;
    }
    m_tdc(s);
    m_ldx16(s, dp16(s, 0x22));
    m_write16(s, m_dp(s, 0x14), c->x);
    v = m_asl8(s, m_asl8(s, m_asl8(s, m_lda8(s, dp8(s, 0x14)))));
    m_set_flag(s, CPU_FLAG_C, true);
    m_set_a8(s, m_sbc8(s, v, dp8(s, 0x14)));
    m_tax(s);
    if (m_lda8(s, absx8(s, 0x93EE)) != 0)
        goto l8570;
    m_tdc(s);
    if (flag_2989_bit5(s)) {
        if (!jsr(s, 0xC1855C, 0xC1B70E))
            return;
        goto l8588;
    }
    set_absx8(s, 0x93EE, m_lda8(s, 0x01));
    m_write8(s, m_dp(s, 0xA1), m_lda8(s, dp8(s, 0x14)));
    m_lda8(s, 0x01);
    if (!jsr(s, 0xC1856D, 0xC10003))
        return;

l8570:
    if (lda_abs(s, 0x93EE) & 0x80)
        set_abs8(s, 0xB188, (uint8_t)c->a);
    if (lda_abs(s, 0x93F5) & 0x80)
        set_abs8(s, 0xB189, (uint8_t)c->a);
    if (lda_abs(s, 0x93FC) & 0x80)
        set_abs8(s, 0xB18A, (uint8_t)c->a);
l8588:
    if (flag_2989_bit5(s) && !jsr(s, 0xC1858F, 0xC1B725))
        return;
    if (lda_abs(s, 0x99D4) & 0x80)
        goto l860a;
    set_abs8(s, 0xB18B, lda_abs(s, 0x99D4));
    set_abs8(s, 0x99D4, lda_abs(s, 0x99D5));
    set_abs8(s, 0x99D5, lda_abs(s, 0x99D6));
    set_abs8(s, 0x99D6, m_lda8(s, 0xFF));
    step8(s, m_abs(s, 0x99D8), -1);
    {
        static const struct { uint16_t flag, copy; uint32_t site; } members[3] = {
            { 0x93EE, 0xB188, 0xC185CE }, { 0x93F5, 0xB189, 0xC185E7 }, { 0x93FC, 0xB18A, 0xC18600 },
        };
        uint8_t member = lda_abs(s, 0xB18B);
        int k;
        for (k = 0; k < 3; k++) {
            m_cmp8(s, member, (uint8_t)k);
            if (member == k)
                break;
        }
        if (k == 3)
            goto l860a;
        v = lda_abs(s, members[k].flag);
        v = m_lda8(s, (uint8_t)(v ^ abs8(s, members[k].copy)));
        if (!(v & 0x80))
            goto l860d;
        if (k == 0)
            m_tdc(s);
        else
            m_lda8(s, (uint8_t)k);
        set_abs8(s, 0xB18B, (uint8_t)c->a);
        if (!jsr(s, members[k].site, 0xC1B967))
            return;
        m_tdc(s);
        set_abs8(s, members[k].copy, (uint8_t)c->a);
        set_abs8(s, members[k].flag, (uint8_t)c->a);
        if (k != 2)
            goto l860d;   /* the third falls through into the JSR $883D */
    }

l860a:
    /* JSR $883D, inlined */
    m_push16(s, 0x860C);
    if (!update_party_slots(s, true))
        return;
    m_pull16(s);
l860d:
    v = lda_abs(s, 0xB188);
    v = m_lda8(s, (uint8_t)(v | abs8(s, 0xB189)));
    v = m_lda8(s, (uint8_t)(v | abs8(s, 0xB18A)));
    if (v == 0) {
        m_rep(s, CPU_FLAG_M);
        m_write16(s, m_abs(s, 0xB186), m_lda16(s, (uint16_t)(m_read16(s, m_abs(s, 0xB186)) & 0xFFFE)));
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
    }

l8626:
    if (!jsr(s, 0xC18626, 0xC1B223))
        return;
    clear_ax(s);
    do {
        set_absx8(s, 0xB3AC, m_lda8(s, absx8(s, 0xAD8E)));
        m_inx(s);
        m_cmp16(s, c->x, 0x000B);
    } while (c->x < 0x000B);
    set_abs8(s, 0xB3B7, lda_abs(s, 0xB18B));
    if (!jsr(s, 0xC1863D, 0xC1B762))
        return;
    if (flag_2989_bit5(s))
        goto l881c;
    if (lda_abs(s, 0xB2C0) == 0) {
        if (!jsl(s, 0xC18650, 0xFDAC6E))
            return;
        goto l881c;
    }
    m_tdc(s);
    v = lda_abs(s, 0xB3B7);
    m_cmp8(s, v, 0x03);
    if (v >= 0x03) {
        slot_to_stat_block(s);
        m_lda8(s, absx8(s, 0x5E4B));
        m_bit_imm8(s, 0x04);
        if (c->a & 0x04)
            goto l881c;
    }
    m_tdc(s);
    set_abs8(s, 0xB252, (uint8_t)c->a);
    set_abs8(s, 0xB315, (uint8_t)c->a);
    set_abs8(s, 0xB1CF, (uint8_t)c->a);
    set_abs8(s, 0xAF24, (uint8_t)c->a);

l867e:
    /* each queue entry $B315 */
    lda_abs(s, 0xB315);
    m_tax(s);
    v = m_lda8(s, absx8(s, 0xB3AC));
    m_cmp8(s, v, 0x03);
    if (v < 0x03)
        goto l8809;
    m_cmp8(s, v, 0xFF);
    if (v == 0xFF)
        goto l8809;
    m_tax(s);
    v = m_lda8(s, absx8(s, 0xAEFF));
    m_cmp8(s, v, 0xFF);
    {
        bool gone = v == 0xFF;
        m_txa(s);
        v = minus3(s);
        set_abs8(s, 0xB252, v);
        set_abs8(s, 0xAEC8, v);
        m_lda8(s, absx8(s, gone ? 0xAF0A : 0xAEFF));
        if (!jsr(s, gone ? 0xC186A8 : 0xC186D8, 0xC1B488))
            return;
        m_ldx16(s, m_read16(s, m_abs(s, 0xB1D2)));
        m_write16(s, m_abs(s, 0xB273), c->x);
        m_write16(s, m_abs(s, 0xB1D0), c->x);
        if (gone) {
            v = lda_cc(s, 0x0000);
            m_cmp8(s, v, 0x20);
            if (v != 0x20)
                goto l8809;
            lda_abs(s, 0xAEC8);
            m_tax(s);
            set_absx8(s, 0xAF02, m_lda8(s, absx8(s, 0xAF0D)));
        }
    }
    /* $C1:86E4 */
    m_tdc(s);
    lda_abs(s, 0xAEC8);
    m_tax(s);
    set_abs8(s, 0xB2D5, m_lda8(s, absx8(s, 0x5E0D)));
    m_tdc(s);
    lda_abs(s, 0xAEC8);
    m_rep(s, CPU_FLAG_M);
    m_xba(s);
    c->a = m_lsr16(s, c->a);
    m_set_flag(s, CPU_FLAG_C, false);
    c->a = m_adc16(s, c->a, 0x0180);
    m_tax(s);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, absx8(s, 0x5E4B));
    m_bit_imm8(s, 0x8F);
    if (c->a & 0x8F)
        goto l8809;

l8706:
    /* one script command */
    m_tdc(s);
    set_abs8(s, 0xB1CF, (uint8_t)c->a);
    set_abs8(s, 0xAF24, (uint8_t)c->a);
    lda_abs(s, 0xB252);
    m_tax(s);
    set_abs8(s, 0xAEBB, m_lda8(s, absx8(s, 0xB16E)));
    set_absx8(s, 0xB16E, lda_abs(s, 0xB3B7));
    m_tdc(s);
    m_ldx16(s, m_read16(s, m_abs(s, 0xB1D2)));
    set_abs8(s, 0xB239, lda_cc(s, 0x0000));
    set_abs8(s, 0xAF24, 0x00);
    v = lda_cc(s, 0x0004);
    m_cmp8(s, v, 0xFE);
    if (v != 0xFE)
        set_abs8(s, 0xB1CF, m_lda8(s, 0x01));
    m_tdc(s);
    m_set_a8(s, m_asl8(s, lda_abs(s, 0xB315)));
    m_tax(s);
    m_rep(s, CPU_FLAG_M);
    m_write16(s, m_abs(s, 0xB3CA), m_lda16(s, m_read16(s, m_abs(s, 0xB1D4) + c->x)));
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_set_a8(s, m_asl8(s, lda_abs(s, 0xB239)));
    m_tax(s);
    if (!jsr(s, 0xC1874E, 0xC10000 | m_jump_table(s, 0xC1, 0xB80D, c->x)))
        return;
    if (lda_abs(s, 0xB1CF) != 0) {
        m_ldx16(s, m_read16(s, m_abs(s, 0xB1D0)));
        for (int i = 0; i < 4; i++)
            m_inx(s);
        m_write16(s, m_abs(s, 0xB1D2), c->x);
        v = lda_cc(s, 0x0000);
        set_abs8(s, 0xB239, v);
        m_set_a8(s, m_asl8(s, v));
        m_tax(s);
        if (!jsr(s, 0xC18769, 0xC10000 | m_jump_table(s, 0xC1, 0xB80D, c->x)))
            return;
    }
    if (lda_abs(s, 0xAF24) == 0) {
        /* $C1:8771 */
        m_tdc(s);
        lda_abs(s, 0xB252);
        m_tax(s);
        set_absx8(s, 0xB16E, lda_abs(s, 0xB3B7));
        m_push8(s, lda_abs(s, 0xAEC8));
        if (!jsr(s, 0xC18780, 0xC18CF9))
            return;
        v = m_lda8(s, m_pull8(s));
        set_abs8(s, 0xAEC8, v);
        if (!jsr(s, 0xC18787, 0xC1B223))
            return;
        m_tdc(s);
        m_set_a8(s, m_asl8(s, lda_abs(s, 0xB315)));
        m_tax(s);
        m_rep(s, CPU_FLAG_M);
        m_write16(s, m_abs(s, 0xB1D4) + c->x, m_lda16(s, m_read16(s, m_abs(s, 0xB3CA))));
        m_sep(s, CPU_FLAG_M);
        m_tdc(s);
        lda_abs(s, 0xB315);
        m_tax(s);
        v = lda_abs(s, 0xAF24);
        set_absx8(s, 0xB24A, v);
        m_cmp8(s, v, 0x02);
        if (v == 0x02)
            set_absx8(s, 0xB2B6, m_lda8(s, 0x00));
    }
    /* $C1:87AE */
    if (lda_abs(s, 0xAF24) != 0) {
        v = lda_abs(s, 0xAF24);
        m_cmp8(s, v, 0x02);
        if (v != 0x02) {
            set_abs8(s, 0xB1CF, 0x00);
            m_ldx16(s, m_read16(s, m_abs(s, 0xB1D2)));
            do {
                m_inx(s);
                v = lda_cc(s, 0x0000);
                m_cmp8(s, v, 0xFE);
            } while (v != 0xFE);
            m_write16(s, m_abs(s, 0xB1D2), c->x);
        }
        /* $C1:87CC */
        m_ldx16(s, m_read16(s, m_abs(s, 0xB1D2)));
        do {
            m_inx(s);
            v = lda_cc(s, 0x0000);
            m_cmp8(s, v, 0xFE);
        } while (v != 0xFE);
        m_inx(s);
        m_write16(s, m_abs(s, 0xB1D2), c->x);
        v = lda_cc(s, 0x0000);
        m_cmp8(s, v, 0xFF);
        if (v != 0xFF) {
            m_write16(s, m_abs(s, 0xB1D0), c->x);
            lda_abs(s, 0xB315);
            m_tax(s);
            step8(s, m_abs(s, 0xB263) + c->x, +1);
            m_ldx16(s, m_read16(s, m_abs(s, 0xB1D2)));
            lda_abs(s, 0xB252);
            m_tax(s);
            set_absx8(s, 0xB16E, lda_abs(s, 0xAEBB));
            goto l8706;
        }
    }
    /* $C1:87FE */
    m_tdc(s);
    lda_abs(s, 0xAEC8);
    m_tax(s);
    set_absx8(s, 0x5E0D, lda_abs(s, 0xB2D5));

l8809:
    if (!jsr(s, 0xC18809, 0xC1B575))
        return;
    step8(s, m_abs(s, 0xB315), +1);
    v = lda_abs(s, 0xB315);
    m_cmp8(s, v, 0x08);
    if (v < 0x08) {
        call_clear_b192_entry(s, 0x8816);
        goto l867e;
    }

l881c:
    if (!jsr(s, 0xC1881C, 0xC1AC5E))
        return;
    set_abs8(s, 0xB2C0, 0x00);
    if (m_lda8(s, dp8(s, 0x24)) == 0 && !jsr(s, 0xC18826, 0xC18C09))
        return;
    set_abs8(s, 0xB18B, lda_abs(s, 0xB3B7));
    call_clear_b192_entry(s, 0x882F);
    if (!jsr(s, 0xC18832, 0xC1AC46) || !jsl(s, 0xC18835, 0xFDACEE))
        return;
    set_abs8(s, 0xB2C0, 0x00);
    m_rts(s);
}
