/* Battle: stepping the combatants' animation frames.
 *
 * Each frame the battle steps the animations of three combatants: the party
 * (slots 0-2) while $A4 is 0, otherwise one of three groups of enemies (slots
 * 3-5, 6-8, 9-10), chosen by $A4. The order within the three is kept in
 * $992D-$992F and rotated to start after a slot whose flag in $9922 + slot
 * is set. Each has a frame delay ($9689 for the party, $9694 + enemy for
 * enemies) that counts down before its next frame. */
#include "battle/animation.h"

#include "port/machine.h"

static uint8_t abs8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr)); }
static void set_abs8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr), v); }
static uint16_t abs16(Snes *s, uint16_t addr) { return m_read16(s, m_abs(s, addr)); }
static uint8_t absx8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.x); }
static void set_absx8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.x, v); }
static uint8_t absy8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.y); }
static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }

/* INC/DEC on a direct-page byte, setting N and Z. */
static uint8_t step_dp8(Snes *s, uint8_t cell, int delta)
{
    uint8_t v = (uint8_t)(dp8(s, cell) + delta);
    set_dp8(s, cell, v);
    m_set_nz8(s, v);
    return v;
}

/* INC/DEC on an absolute byte (8-bit), setting N and Z. */
static uint8_t step_abs8(Snes *s, uint16_t addr, int delta)
{
    uint8_t v = (uint8_t)(abs8(s, addr) + delta);
    set_abs8(s, addr, v);
    m_set_nz8(s, v);
    return v;
}

/* INC/DEC abs,X (8-bit), setting N and Z. */
static uint8_t step_absx8(Snes *s, uint16_t addr, int delta)
{
    uint8_t v = (uint8_t)(absx8(s, addr) + delta);
    set_absx8(s, addr, v);
    m_set_nz8(s, v);
    return v;
}

/* INC A / DEC A (8-bit). */
static uint8_t step_a8(Snes *s, int delta)
{
    uint8_t v = (uint8_t)(s->cpu.a + delta);
    m_set_a8(s, v);
    m_set_nz8(s, v);
    return v;
}

/* ASL A (8-bit). */
static uint8_t asl_a8(Snes *s)
{
    uint8_t v = m_asl8(s, (uint8_t)s->cpu.a);
    m_set_a8(s, v);
    return v;
}

/* The 24-bit address a long pointer on the direct page holds. */
static uint32_t long_ptr(Snes *s, uint8_t cell)
{
    return m_read16(s, m_dp(s, cell)) | (uint32_t)m_read8(s, m_dp(s, (uint8_t)(cell + 2))) << 16;
}

static bool widths_ok(const Snes *s) { return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M; }

/* JSR out; false if the callee didn't come back with 8-bit A and 16-bit X. */
static bool jsr(Snes *s, uint32_t site, uint32_t target)
{
    m_jsr(s, site, target);
    return widths_ok(s);
}

/* Operations that load the order entries: LDA #n (n >= 0), TDC, INC A, DEC A. */
enum { OP_TDC = -1, OP_INC = -2, OP_DEC = -3 };

/* LDA flag : BEQ : STZ flag : ... for each flag in turn; the first one set
 * (or none) picks the three operations whose results go to $992D-$992F. */
static void choose_order(Snes *s, const uint16_t *flags, int count, const int16_t (*ops)[3])
{
    int pick = count;
    for (int i = 0; i < count; i++) {
        if (m_lda8(s, abs8(s, flags[i])) != 0) {
            set_abs8(s, flags[i], 0x00);
            pick = i;
            break;
        }
    }
    for (uint16_t k = 0; k < 3; k++) {
        int16_t op = ops[pick][k];
        if (op == OP_TDC)
            m_tdc(s);
        else if (op == OP_INC || op == OP_DEC)
            step_a8(s, op == OP_INC ? 1 : -1);
        else
            m_lda8(s, (uint8_t)op);
        set_abs8(s, (uint16_t)(0x992D + k), (uint8_t)s->cpu.a);
    }
}

// $C1:2F22
/* Turn party member $94 toward its target: $A098 holds the member (bit 7:
 * already handled). For another member, set bit 7 of $A098 with the member
 * number when bit 7 is clear. For this member, clear bit 7 and, unless the
 * stat block ($A2) has $5E4A bit 7 or $5E4B bit 1 set, run $C1:0045 action
 * 5 with $986E = 2 and $986F = the member, record the target ($9873) in
 * $99DA, and set the facing $96DB from the direction ($C1:0222, table
 * $C0:F700) between the positions ($1D0C/$1D23) of the member and the
 * target; $80 = 1 when the facing changed, 0 otherwise. */
static bool face_target(Snes *s)
{
    Cpu *c = &s->cpu;
    set_dp8(s, 0x80, 0x00);
    uint8_t current = m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0xA098)) & 0x7F));
    set_dp8(s, 0x81, current);
    uint8_t member = m_lda8(s, dp8(s, 0x94));
    m_cmp8(s, member, dp8(s, 0x81));
    if (member != current) {
        if (!(m_lda8(s, abs8(s, 0xA098)) & 0x80)) {
            uint8_t v = m_lda8(s, dp8(s, 0x94));
            set_abs8(s, 0xA098, m_lda8(s, (uint8_t)(v | 0x80)));
        }
        return true;
    }
    set_abs8(s, 0xA098, m_lda8(s, dp8(s, 0x81)));
    m_set_index(s, &c->y, dp16(s, 0xA2));
    if (m_lda8(s, absy8(s, 0x5E4A)) & 0x80)
        return true;
    if (m_lda8(s, (uint8_t)(m_lda8(s, absy8(s, 0x5E4B)) & 0x02)) != 0)
        return true;
    set_abs8(s, 0x986E, m_lda8(s, 0x02));
    set_abs8(s, 0x986F, m_lda8(s, dp8(s, 0x94)));
    m_lda8(s, 0x05);
    if (!jsr(s, 0xC12F60, 0xC10045))
        return false;
    m_ldx16(s, dp16(s, 0x94));
    set_dp8(s, 0xD3, m_lda8(s, absx8(s, 0x1D0C)));
    set_dp8(s, 0xD4, m_lda8(s, absx8(s, 0x1D23)));
    set_absx8(s, 0x99DA, m_lda8(s, abs8(s, 0x9873)));
    m_tax(s);
    set_dp8(s, 0xD5, m_lda8(s, absx8(s, 0x1D0C)));
    set_dp8(s, 0xD6, m_lda8(s, absx8(s, 0x1D23)));
    set_dp8(s, 0x80, 0x00);
    if (!jsr(s, 0xC12F82, 0xC10222))
        return false;
    m_tax(s);
    uint8_t facing = m_lda8(s, m_read8(s, 0xC0F700 + c->x));
    m_ldx16(s, dp16(s, 0x94));
    uint8_t old = absx8(s, 0x96DB);
    m_cmp8(s, facing, old);
    if (facing != old) {
        set_absx8(s, 0x96DB, facing);
        step_dp8(s, 0x80, +1);
    }
    return true;
}

// $C1:2E28
/* Party member $94's next frame: choose its pose ($C1:2F97), face its target
 * (inline $C1:2F22), apply the pose ($C1:308C), use pose $2B instead of 3
 * while $99CF or $99D0 is set, then advance its frame $968C through the
 * pose's frame list in bank $E4 (a 0 entry restarts it; list offsets from
 * the member's $1C99/$1CBA words plus pose * 4) and set its delay $9689 to a
 * fifth of the list's delay byte (at least 1), with the frame's graphics in
 * $967B. Rebuilds the graphics ($C1:1C4A) unless the pose is unchanged
 * ($A103), 3, $80 clear and $95D5 isn't this member. */
static bool step_party_member(Snes *s)
{
    Cpu *c = &s->cpu;
    if (!jsr(s, 0xC12E28, 0xC12F97))
        return false;
    m_push16(s, 0x2E2D);
    if (!face_target(s))
        return false;
    m_pull16(s);
    if (!jsr(s, 0xC12E2E, 0xC1308C))
        return false;
    uint8_t pose = m_lda8(s, abs8(s, 0xA440));
    m_cmp8(s, pose, 0x03);
    if (pose == 0x03) {
        uint8_t v = m_lda8(s, abs8(s, 0x99CF));
        if (m_lda8(s, (uint8_t)(v | abs8(s, 0x99D0))) != 0) {
            set_abs8(s, 0xA440, m_lda8(s, 0x2B));
            m_ldx16(s, dp16(s, 0x94));
            set_absx8(s, 0x968C, m_lda8(s, 0xFF));
        }
    }

    /* $C1:2E4C */
    m_lda8(s, dp8(s, 0x94));
    asl_a8(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_a8(s, m_adc8(s, (uint8_t)c->a, dp8(s, 0x94)));
    m_tax(s);
    set_abs8(s, 0xA1E8, m_lda8(s, absx8(s, 0x1C99)));
    set_abs8(s, 0xA1E9, m_lda8(s, absx8(s, 0x1C9A)));
    set_abs8(s, 0xA1FE, m_lda8(s, absx8(s, 0x1CBA)));
    set_abs8(s, 0xA1FF, m_lda8(s, absx8(s, 0x1CBB)));
    m_lda8(s, abs8(s, 0xA440));
    m_rep(s, CPU_FLAG_M);
    c->a = m_asl16(s, c->a);
    c->a = m_asl16(s, c->a);
    set_dp16(s, 0x82, c->a);
    m_set_flag(s, CPU_FLAG_C, false);
    c->a = m_adc16(s, c->a, abs16(s, 0xA1FE));
    m_write16(s, m_abs(s, 0xA1FE), c->a);
    m_set_flag(s, CPU_FLAG_C, false);
    m_lda16(s, dp16(s, 0x82));
    c->a = m_adc16(s, c->a, abs16(s, 0xA1E8));
    m_write16(s, m_abs(s, 0xA1E8), c->a);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_ldx16(s, dp16(s, 0x94));
    set_dp8(s, 0xA5, m_lda8(s, absx8(s, 0x96DB)));
    set_dp8(s, 0xA7, m_lda8(s, absx8(s, 0x966E)));
    set_dp8(s, 0xA8, m_lda8(s, absx8(s, 0xA2C8)));
    if (!jsr(s, 0xC12E98, 0xC100A7))
        return false;
    set_dp16(s, 0x82, m_ldx16(s, dp16(s, 0xA9)));
    set_abs8(s, 0x9679, m_lda8(s, dp8(s, 0x94)));
    asl_a8(s);
    m_set_index(s, &c->y, c->a);
    m_ldx16(s, dp16(s, 0x94));
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_a8(s, m_adc8(s, m_lda8(s, absx8(s, 0x968C)), 0x01));
    uint8_t delay;
    for (;;) {
        /* $C1:2EAE */
        set_absx8(s, 0x968C, (uint8_t)c->a);
        m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
        c->a = m_adc16(s, c->a, abs16(s, 0xA1FE));
        m_tax(s);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        delay = m_lda8(s, m_read8(s, 0xE40000 + c->x));
        if (delay != 0)
            break;
        m_tdc(s);
        m_ldx16(s, dp16(s, 0x94));
    }
    m_ldx16(s, dp16(s, 0x94));
    set_dp8(s, 0xB1, delay);
    set_dp8(s, 0xB2, 0x00);
    set_dp8(s, 0xB3, m_lda8(s, 0x05));
    if (!jsr(s, 0xC12ECF, 0xC100D7))
        return false;
    if (m_lda8(s, dp8(s, 0xB5)) == 0)
        step_a8(s, +1);
    set_absx8(s, 0x9689, (uint8_t)c->a);
    m_ldx16(s, dp16(s, 0x94));
    m_lda8(s, absx8(s, 0x968C));
    m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
    c->a = m_adc16(s, c->a, dp16(s, 0x82));
    m_set_flag(s, CPU_FLAG_C, false);
    c->a = m_adc16(s, c->a, abs16(s, 0xA1E8));
    m_tax(s);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    set_abs8(s, 0x967B, m_lda8(s, m_read8(s, 0xE40000 + c->x)));

    /* $C1:2EF2 */
    m_ldx16(s, dp16(s, 0x94));
    pose = m_lda8(s, abs8(s, 0xA440));
    uint8_t shown = absx8(s, 0xA103);
    m_cmp8(s, pose, shown);
    bool rebuild = true;
    if (pose != shown) {
        set_absx8(s, 0xA103, pose);
    } else {
        m_cmp8(s, pose, 0x03);
        if (pose == 0x03 && m_lda8(s, dp8(s, 0x80)) == 0) {
            uint8_t focus = m_lda8(s, abs8(s, 0x95D5));
            uint8_t member = dp8(s, 0x94);
            m_cmp8(s, focus, member);
            rebuild = focus == member;
        }
    }
    return !rebuild || jsr(s, 0xC12F10, 0xC11C4A);
}

/* JSR $0112: ASL x3 (8-bit), the JSR at `site`. */
static void times_8(Snes *s, uint16_t site)
{
    m_push16(s, (uint16_t)(site + 2));
    for (int i = 0; i < 3; i++)
        asl_a8(s);
    m_pull16(s);
}

// $C1:3364
/* Enemy $9C's animation changed ($5E0D or $96DE differ from the copies in
 * $974A and $96E9): take the copies, load the long pointers $C0 and $C3 from
 * the enemy's $1CA2 and $1CC3 entries, and copy its frame list for the
 * animation (offset $96DE * $9671-$A2CB via $C1:00A7, plus 4, 12 or 24 by
 * $CC:FB91; kind 1 also clears $9829) into $97D9 + $CC:F82F entry: 8 graphics
 * bytes from [$C0] and 8 delays, mapped through $CC:FBCE, from [$C3] into
 * $9799. */
static bool load_enemy_animation(Snes *s)
{
    Cpu *c = &s->cpu;
    set_absx8(s, 0x974A, m_lda8(s, absx8(s, 0x5E0D)));
    set_absx8(s, 0x96E9, m_lda8(s, absx8(s, 0x96DE)));
    m_txa(s);
    asl_a8(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_a8(s, m_adc8(s, (uint8_t)c->a, dp8(s, 0x9C)));
    m_tax(s);
    static const struct { uint16_t table; uint8_t cell; } ptrs[6] = {
        { 0x1CC3, 0xC3 }, { 0x1CC4, 0xC4 }, { 0x1CC5, 0xC5 }, { 0x1CA2, 0xC0 }, { 0x1CA3, 0xC1 }, { 0x1CA4, 0xC2 },
    };
    for (int i = 0; i < 6; i++)
        set_dp8(s, ptrs[i].cell, m_lda8(s, absx8(s, ptrs[i].table)));
    m_ldx16(s, dp16(s, 0x9C));
    set_dp8(s, 0xA5, m_lda8(s, absx8(s, 0x96DE)));
    set_dp8(s, 0xA7, m_lda8(s, absx8(s, 0x9671)));
    set_dp8(s, 0xA8, m_lda8(s, absx8(s, 0xA2CB)));
    if (!jsr(s, 0xC133A5, 0xC100A7))
        return false;
    m_ldx16(s, dp16(s, 0x9C));
    m_lda8(s, absx8(s, 0x974A));
    m_tax(s);
    if (m_lda8(s, m_read8(s, 0xCCFB91 + c->x)) == 0) {
        m_lda8(s, 0x0C);
    } else if (step_a8(s, -1) == 0) {
        m_ldx16(s, dp16(s, 0x9C));
        set_absx8(s, 0x9829, 0x00);
        m_lda8(s, 0x04);
    } else {
        m_lda8(s, 0x18);
    }
    set_dp8(s, 0x86, (uint8_t)c->a);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
    c->a = m_adc16(s, c->a, dp16(s, 0xA9));
    set_dp16(s, 0x82, c->a);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    set_dp8(s, 0x80, m_lda8(s, 0x08));
    m_ldx16(s, dp16(s, 0x9C));
    m_lda8(s, m_read8(s, 0xCCF82F + c->x));
    m_tax(s);
    m_push16(s, c->x);
    m_set_index(s, &c->y, dp16(s, 0x82));
    do {
        set_absx8(s, 0x97D9, m_lda8(s, m_read8(s, long_ptr(s, 0xC0) + c->y)));
        m_iny(s);
        m_inx(s);
    } while (step_dp8(s, 0x80, -1) != 0);
    set_dp8(s, 0x80, m_lda8(s, 0x08));
    c->x = m_pull16(s);
    m_set_nz16(s, c->x);
    set_dp16(s, 0x84, c->x);
    m_lda8(s, dp8(s, 0x86));
    m_set_index(s, &c->y, c->a);
    do {
        m_lda8(s, m_read8(s, long_ptr(s, 0xC3) + c->y));
        m_tax(s);
        m_lda8(s, m_read8(s, 0xCCFBCE + c->x));
        m_ldx16(s, dp16(s, 0x84));
        set_absx8(s, 0x9799, (uint8_t)c->a);
        m_iny(s);
        step_dp8(s, 0x84, +1);
    } while (step_dp8(s, 0x80, -1) != 0);
    return true;
}

/* LDA $9C : JSR $0112 : STA $80 : CLC : ADC $969C,X : TAY : LDA $9799,Y :
 * BNE, else STZ $969C,X : LDA $80 : TAY -- Y = the enemy's delay entry for
 * frame $969C, restarting at frame 0 when that delay is 0. */
static uint8_t frame_delay(Snes *s, uint16_t site, bool reload)
{
    Cpu *c = &s->cpu;
    m_lda8(s, dp8(s, 0x9C));
    times_8(s, site);
    set_dp8(s, 0x80, (uint8_t)c->a);
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_a8(s, m_adc8(s, (uint8_t)c->a, absx8(s, 0x969C)));
    m_set_index(s, &c->y, c->a);
    uint8_t delay = m_lda8(s, absy8(s, 0x9799));
    if (delay == 0) {
        set_absx8(s, 0x969C, 0x00);
        m_lda8(s, dp8(s, 0x80));
        m_set_index(s, &c->y, c->a);
        if (reload)
            delay = m_lda8(s, absy8(s, 0x9799));
    }
    return delay;
}

// $C1:3351
/* Enemy $9C (X): reload its frame list when its animation changed, then when
 * its delay $9694 runs out show frame $969C's graphics ($967B, slot $9679),
 * rebuild its graphics ($C1:1C4A) unless $967C + slot is 3 or more, and move
 * to the next of up to 4 frames, taking its delay. While $9912 is set (1:
 * for slots other than 4 and 6 and up, with $967C + slot clear) it instead
 * sets the slot's $9922 flag and waits a frame. */
static bool step_enemy(Snes *s)
{
    Cpu *c = &s->cpu;
    uint8_t v = m_lda8(s, absx8(s, 0x974A));
    uint8_t w = absx8(s, 0x5E0D);
    m_cmp8(s, v, w);
    bool changed = v != w;
    if (!changed) {
        v = m_lda8(s, absx8(s, 0x96DE));
        w = absx8(s, 0x96E9);
        m_cmp8(s, v, w);
        changed = v != w;
    }
    if (changed && !load_enemy_animation(s))
        return false;

    /* $C1:3407 */
    m_ldx16(s, dp16(s, 0x9C));
    if (step_absx8(s, 0x9694, -1) != 0)
        return true;
    frame_delay(s, 0x3410, false);
    set_abs8(s, 0x967B, m_lda8(s, absy8(s, 0x97D9)));
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_a8(s, m_adc8(s, m_lda8(s, dp8(s, 0x9C)), 0x03));
    set_abs8(s, 0x9679, (uint8_t)c->a);
    m_tax(s);
    bool hold;
    if (m_lda8(s, abs8(s, 0x9912)) == 0) {
        hold = false;
    } else if (step_a8(s, -1) != 0 || m_lda8(s, absx8(s, 0x967C)) != 0) {
        hold = true;
    } else {
        uint8_t slot = m_lda8(s, abs8(s, 0x9679));
        m_cmp8(s, slot, 0x04);
        if (slot == 0x04) {
            hold = false;
        } else {
            m_cmp8(s, slot, 0x06);
            hold = slot < 0x06;
        }
    }
    if (hold) {
        step_absx8(s, 0x9922, +1);
        m_ldx16(s, dp16(s, 0x9C));
        step_absx8(s, 0x9694, +1);
        return true;
    }

    /* $C1:3456 */
    uint8_t state = m_lda8(s, absx8(s, 0x967C));
    m_cmp8(s, state, 0x03);
    if (state < 0x03 && !jsr(s, 0xC1345D, 0xC11C4A))
        return false;
    m_ldx16(s, dp16(s, 0x9C));
    step_absx8(s, 0x969C, +1);
    uint8_t frame = m_lda8(s, absx8(s, 0x969C));
    m_cmp8(s, frame, 0x04);
    if (frame == 0x04)
        set_absx8(s, 0x969C, 0x00);
    set_absx8(s, 0x9694, frame_delay(s, 0x3471, true));
    return true;
}

// $C1:3234
/* The enemy half of battle_step_combatant_animations: count $A4 (1-3) down
 * to the group, choose the order of its enemies (a negative entry is
 * skipped), step each present one ($96F8), then count $A4 back up unless it
 * is what the caller saved in $993B or 3 or more. */
static void step_enemy_group(Snes *s)
{
    Cpu *c = &s->cpu;
    set_abs8(s, 0x9841, 0x00);
    int group = 0;
    while (step_a8(s, -1) != 0) {
        if (++group == 3) {
            m_rts(s);
            return;
        }
    }
    step_dp8(s, 0xA4, -1);
    static const uint16_t flags[3][3] = { { 0x9925, 0x9926, 0x9927 }, { 0x9928, 0x9929, 0x992A }, { 0x992B, 0x992C } };
    static const int counts[3] = { 3, 3, 2 };
    static const int16_t ops[3][4][3] = {
        { { OP_TDC, OP_INC, OP_INC }, { 1, OP_INC, OP_TDC }, { 2, OP_TDC, OP_INC }, { OP_TDC, OP_INC, OP_INC } },
        { { 3, OP_INC, OP_INC }, { 4, OP_INC, 3 }, { 5, 3, OP_INC }, { 3, OP_INC, OP_INC } },
        { { 6, OP_INC, 0xFF }, { 7, OP_DEC, 0xFF }, { 6, OP_INC, 0xFF } },
    };
    choose_order(s, flags[group], counts[group], ops[group]);

    uint8_t n;
    do {
        /* $C1:333D */
        m_lda8(s, abs8(s, 0x9841));
        m_tax(s);
        if (!(m_lda8(s, absx8(s, 0x992D)) & 0x80)) {
            m_tax(s);
            set_dp16(s, 0x9C, c->x);
            if (m_lda8(s, absx8(s, 0x96F8)) != 0 && !step_enemy(s))
                return;
        }
        step_abs8(s, 0x9841, +1);
        n = m_lda8(s, abs8(s, 0x9841));
        m_cmp8(s, n, 0x03);
    } while (n != 0x03);
    uint8_t a4 = m_lda8(s, dp8(s, 0xA4));
    uint8_t saved = abs8(s, 0x993B);
    m_cmp8(s, a4, saved);
    if (a4 != saved) {
        m_cmp8(s, a4, 0x03);
        if (a4 < 0x03)
            step_dp8(s, 0xA4, +1);
    }
    m_rts(s);
}

// $C1:2D9F
/* Step three combatants' animations for this frame (see the top of this
 * file): with $A4 clear the party, in the order $992D-$992F; for each member
 * present ($96F5), with $9912 = 1 set its $9922 flag, otherwise count down
 * its delay $9689 and, when it runs out, give it its next frame. With $A4
 * set, the group of enemies $A4 selects. Clears $9912 first. */
void battle_step_combatant_animations(Snes *s)
{
    Cpu *c = &s->cpu;
    set_abs8(s, 0x9912, 0x00);
    if (m_lda8(s, dp8(s, 0xA4)) != 0) {
        step_enemy_group(s);
        return;
    }
    set_dp8(s, 0x84, m_lda8(s, 0x03));
    static const uint16_t flags[3] = { 0x9922, 0x9923, 0x9924 };
    static const int16_t ops[4][3] = {
        { OP_TDC, OP_INC, OP_INC }, { 1, OP_INC, OP_TDC }, { 2, OP_TDC, OP_INC }, { OP_TDC, OP_INC, OP_INC },
    };
    choose_order(s, flags, 3, ops);

    /* $C1:2DFD */
    m_tdc(s);
    m_tax(s);
    set_dp16(s, 0x92, c->x);
    uint8_t n;
    do {
        m_ldx16(s, dp16(s, 0x92));
        m_lda8(s, absx8(s, 0x992D));
        m_tax(s);
        set_dp16(s, 0x94, c->x);
        if (m_lda8(s, absx8(s, 0x96F5)) != 0) {
            uint8_t hold = m_lda8(s, abs8(s, 0x9912));
            m_cmp8(s, hold, 0x01);
            if (hold == 0x01) {
                step_absx8(s, 0x9922, +1);
            } else {
                m_ldx16(s, dp16(s, 0x94));
                if (step_absx8(s, 0x9689, -1) == 0 && !step_party_member(s))
                    return;
            }
        }
        step_dp8(s, 0x92, +1);
        n = m_lda8(s, dp8(s, 0x92));
        m_cmp8(s, n, 0x03);
    } while (n != 0x03);
    m_rts(s);
}
