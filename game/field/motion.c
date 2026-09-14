/* Field mode: object motion -- directions, velocities, blocking and the walk
 * animation.
 *
 * Directions are bytes: $00, $40, $80 and $C0 start the four quadrants, the
 * table at $F300 gives the angle within one, and $F800 the per-direction
 * velocity factor (whose sign follows the direction's half circle). An
 * object's speed is $1A00,X and its velocity $1900,X (X axis) and $1980,X (Y
 * axis). Positions are 16-bit, $1800 (X) and $1880 (Y), tile in the high byte.
 * The map attribute tables at $7E:7000 and $7E:7040 are indexed by tile. */
#include "field/field.h"

#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }
static uint8_t obj8(Snes *s, uint16_t table) { return m_read8(s, m_abs(s, table) + s->cpu.x); }
static void set_obj8(Snes *s, uint16_t table, uint8_t v) { m_write8(s, m_abs(s, table) + s->cpu.x, v); }
static uint16_t obj16(Snes *s, uint16_t table) { return m_read16(s, m_abs(s, table) + s->cpu.x); }

/* EOR #$FF : INC A */
static uint8_t negate8(Snes *s, uint8_t v)
{
    v = m_lda8(s, (uint8_t)~v);
    return m_lda8(s, (uint8_t)(v + 1));
}

/* LDX $6D at the current X width */
static void ldx_object(Snes *s) { m_ldx(s, dp16(s, 0x6D)); }

// $C0:ABA2
/* Direction from the tile in $F0/$F1 to the tile in $F2/$F3, into $EE: the
 * quadrant plus the angle from $F300, looked up by the two distances ($F6,
 * $F7), scaled to 5 bits when either is 32 or more. */
void field_direction_to_target(Snes *s)
{
    set_dp8(s, 0xEF, 0x00);
    m_lda8(s, dp8(s, 0xF3));
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t dy = m_sbc8(s, (uint8_t)s->cpu.a, dp8(s, 0xF1));
    m_set_a8(s, dy);
    bool down = m_flag(s, CPU_FLAG_C);
    if (!down)
        dy = negate8(s, dy);
    set_dp8(s, 0xF7, dy);
    m_lda8(s, dp8(s, 0xF2));
    m_set_flag(s, CPU_FLAG_C, true);
    uint8_t dx = m_sbc8(s, (uint8_t)s->cpu.a, dp8(s, 0xF0));
    m_set_a8(s, dx);
    bool right = m_flag(s, CPU_FLAG_C);

    if (down && right) {
        set_dp8(s, 0xF6, dx);
        set_dp8(s, 0xEE, 0x00);
    } else if (down) {
        uint8_t adx = negate8(s, dx);
        m_xba(s);
        set_dp8(s, 0xF6, m_lda8(s, dp8(s, 0xF7)));
        m_xba(s);
        set_dp8(s, 0xF7, adx);
        set_dp8(s, 0xEE, m_lda8(s, 0x40));
    } else if (!right) {
        set_dp8(s, 0xF6, negate8(s, dx));
        set_dp8(s, 0xEE, m_lda8(s, 0x80));
    } else {
        m_xba(s);
        set_dp8(s, 0xF6, m_lda8(s, dp8(s, 0xF7)));
        m_xba(s);
        set_dp8(s, 0xF7, dx);
        set_dp8(s, 0xEE, m_lda8(s, 0xC0));
    }

    /* $C0:ABEE */
    uint8_t f6 = dp8(s, 0xF6), f7 = dp8(s, 0xF7);
    int shift = 0;
    static const struct { uint8_t bit; bool f7; int shift; } tests[] = {
        { 0x40, false, 2 }, { 0x40, true, 2 }, { 0x20, false, 1 }, { 0x20, true, 1 },
    };
    for (int i = 0; i < 4 && !shift; i++) {
        uint8_t v = m_lda8(s, tests[i].f7 ? f7 : f6);
        m_bit_imm8(s, tests[i].bit);
        if (v & tests[i].bit)
            shift = tests[i].shift;
    }
    static const uint16_t f6_mask[3] = { 0x001F, 0x003F, 0x007F };
    static const uint16_t f7_mask[3] = { 0x001F, 0x003E, 0x007C };
    m_lda8(s, f6);
    m_rep(s, CPU_FLAG_M);
    uint16_t low = m_lda16(s, s->cpu.a & f6_mask[shift]);
    for (int i = 0; i < shift; i++)
        low = m_lsr16(s, low);
    s->cpu.a = low;
    set_dp16(s, 0xF4, low);
    uint16_t high = m_lda16(s, dp16(s, 0xF7) & f7_mask[shift]);
    for (int i = 0; i < 5 - shift; i++)
        high = m_asl16(s, high);
    s->cpu.a = m_lda16(s, (uint16_t)(high | dp16(s, 0xF4)));
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, m_read8(s, m_abs(s, 0xF300) + s->cpu.x));
    m_set_flag(s, CPU_FLAG_C, false);
    uint8_t dir = m_adc8(s, (uint8_t)s->cpu.a, dp8(s, 0xEE));
    m_set_a8(s, dir);
    set_dp8(s, 0xEE, dir);
    m_rts(s);
}

/* One axis of a velocity, X 8-bit: the factor for direction index X from
 * $F800 (negated for indexes $80 and up) times the speed already in $4202,
 * the product's high byte negated back. `reload` does LDX $6D before reading
 * the product, as some copies do. */
static uint8_t axis_velocity(Snes *s, bool reload)
{
    uint8_t factor = m_lda8(s, m_read8(s, m_abs(s, 0xF800) + s->cpu.x));
    uint8_t index = (uint8_t)s->cpu.x;
    m_cmp8(s, index, 0x80);
    bool negative = index >= 0x80;
    if (negative)
        factor = negate8(s, factor);
    m_write8(s, m_abs(s, 0x4203), factor);
    if (reload)
        ldx_object(s);
    uint8_t v = m_lda8(s, m_read8(s, m_abs(s, 0x4217)));
    return negative ? negate8(s, v) : v;
}

/* LDA $EE : CLC : ADC #$40 : TAX -- the direction index for the X axis. */
static void x_axis_direction(Snes *s)
{
    m_lda8(s, dp8(s, 0xEE));
    m_set_flag(s, CPU_FLAG_C, false);
    uint8_t dir = m_adc8(s, (uint8_t)s->cpu.a, 0x40);
    m_set_a8(s, dir);
    m_tax(s);
}

/* LDA $1A00,X : STA $4202 : LDX $EE -- X 8-bit. */
static void begin_velocity(Snes *s)
{
    m_write8(s, m_abs(s, 0x4202), m_lda8(s, obj8(s, 0x1A00)));
    m_ldx(s, dp8(s, 0xEE));
}

/* Store the sign of `table`,X into `table`+1,X (LDA : BPL : LDA #$FF : STA / STZ). */
static void sign_extend(Snes *s, uint16_t table)
{
    if (m_lda8(s, obj8(s, table)) & 0x80)
        set_obj8(s, (uint16_t)(table + 1), m_lda8(s, 0xFF));
    else
        set_obj8(s, (uint16_t)(table + 1), 0x00);
}

// $C0:AC69
/* Set the object's (X) velocity from direction $EE and its speed, without
 * looking at the map; sign-extends into $1901/$1981 and leaves the Y axis in
 * $DB. */
void field_set_velocity(Snes *s)
{
    m_sep(s, CPU_FLAG_X);
    set_obj8(s, 0x1900, 0x00);
    set_obj8(s, 0x1980, 0x00);
    begin_velocity(s);
    uint8_t vy = axis_velocity(s, true);
    set_dp8(s, 0xDB, vy);
    set_obj8(s, 0x1980, vy);
    sign_extend(s, 0x1980);
    x_axis_direction(s);
    uint8_t vx = axis_velocity(s, true);
    set_obj8(s, 0x1900, vx);
    m_rep(s, CPU_FLAG_X);
    sign_extend(s, 0x1900);
    m_rts(s);
}

/* The sign bytes of $DD and $DB into $DE and $DC as $C0:ACFD does. Returns
 * false if both velocities are zero (nothing to look up). `clear_de` is the
 * first copy's STZ $DE on a zero X velocity. */
static bool extend_velocities(Snes *s, bool clear_de)
{
    uint8_t vx = m_lda8(s, dp8(s, 0xDD));
    if (vx == 0) {
        if (clear_de)
            set_dp8(s, 0xDE, 0x00);
        if (m_lda8(s, dp8(s, 0xDB)) == 0)
            return false;
    } else if (vx & 0x80) {
        set_dp8(s, 0xDE, m_lda8(s, 0xFF));
    } else {
        set_dp8(s, 0xDE, 0x00);
    }
    if (m_lda8(s, dp8(s, 0xDB)) & 0x80)
        set_dp8(s, 0xDC, m_lda8(s, 0xFF));
    else
        set_dp8(s, 0xDC, 0x00);
    return true;
}

/* LDX $6D : REP #$20 : the position after one step : SEP #$20 : LDA $E0 :
 * REP #$30 : AND $1D1E : SEP #$20 : TAX -- X = the map tile index there. */
static void tile_ahead(Snes *s)
{
    ldx_object(s);
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, obj16(s, 0x1800));
    m_set_flag(s, CPU_FLAG_C, false);
    uint16_t x = m_adc16(s, s->cpu.a, dp16(s, 0xDD));
    s->cpu.a = x;
    set_dp16(s, 0xDF, x);
    m_lda16(s, obj16(s, 0x1880));
    m_set_flag(s, CPU_FLAG_C, false);
    s->cpu.a = m_adc16(s, s->cpu.a, dp16(s, 0xDB));
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, dp8(s, 0xE0));
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    s->cpu.a = m_lda16(s, (uint16_t)(s->cpu.a & m_read16(s, m_abs(s, 0x1D1E))));
    m_sep(s, CPU_FLAG_M);
    m_tax(s);
}

/* LDA $7E7000,X : STA $E1 : LDA $7E7040,X : STA $E5 -- returns $E5. */
static uint8_t load_attributes(Snes *s)
{
    set_dp8(s, 0xE1, m_lda8(s, m_read8(s, 0x7E7000u + s->cpu.x)));
    uint8_t e5 = m_lda8(s, m_read8(s, 0x7E7040u + s->cpu.x));
    set_dp8(s, 0xE5, e5);
    return e5;
}

/* With Y = the object: LDA $0F80,Y, then its sprite priority either from the
 * map attributes ($E1/$E5, X going 8-bit) or from its own priority bits. */
static void update_priority(Snes *s)
{
    uint8_t mode = m_lda8(s, m_read8(s, m_abs(s, 0x0F80) + s->cpu.y));
    if (mode & 0x80) {
        m_sep(s, CPU_FLAG_X);
        uint8_t v = m_lda8(s, dp8(s, 0xE5) & 0x40);
        v = m_lsr8(s, v);
        v = m_lsr8(s, v);
        v = m_lda8(s, v | 0x20);
        ldx_object(s);
        set_obj8(s, 0x0C01, v);
        v = m_lda8(s, dp8(s, 0xE1) & 0x40);
        v = m_lsr8(s, v);
        v = m_lsr8(s, v);
        set_obj8(s, 0x0C00, m_lda8(s, v | 0x20));
        return;
    }
    set_dp8(s, 0xD9, mode);
    ldx_object(s);
    set_obj8(s, 0x0C01, m_lda8(s, mode & 0x30));
    uint8_t v = m_lda8(s, dp8(s, 0xD9));
    for (int i = 0; i < 4; i++)
        v = m_asl8(s, v);
    set_obj8(s, 0x0C00, m_lda8(s, v & 0x30));
}

/* $C0:ADEB / $C0:AF18: LDX $6D : $1900,X = $DD : $1980,X = $DB : REP #$10 : RTS */
static void store_velocity(Snes *s)
{
    ldx_object(s);
    set_obj8(s, 0x1900, m_lda8(s, dp8(s, 0xDD)));
    set_obj8(s, 0x1980, m_lda8(s, dp8(s, 0xDB)));
    m_rep(s, CPU_FLAG_X);
    m_rts(s);
}

// $C0:ACFD
/* Start the object (X) moving in direction $EE at its speed. When it moves,
 * look at the map tile it's heading into: its sprite priority follows that
 * tile if the object's priority does ($0F80 bit 7). With collision on
 * ($1C80,X bit 0) and the tile's $7E:7040 attribute negative, pick a new
 * direction from the random table ($FE00, index $F8) and try once more; if
 * that tile is blocked too, don't move. Leaves Y = the object. */
void field_start_step(Snes *s)
{
    m_sep(s, CPU_FLAG_X);
    set_obj8(s, 0x1900, 0x00);
    set_obj8(s, 0x1980, 0x00);
    begin_velocity(s);
    set_dp8(s, 0xDB, axis_velocity(s, false));
    x_axis_direction(s);
    set_dp8(s, 0xDD, axis_velocity(s, false));
    if (!extend_velocities(s, true)) {
        store_velocity(s);
        return;
    }
    tile_ahead(s);
    m_ldy(s, dp16(s, 0x6D));
    m_lda8(s, m_read8(s, m_abs(s, 0x1C80) + s->cpu.y));
    m_bit_imm8(s, 0x01);
    bool collision = (uint8_t)s->cpu.a & 0x01;
    uint8_t attribute = load_attributes(s);
    if (!collision || !(attribute & 0x80)) {
        update_priority(s);
        store_velocity(s);
        return;
    }

    /* $C0:AE27: blocked; try a random direction. */
    m_sep(s, CPU_FLAG_X);
    uint8_t index = (uint8_t)(dp8(s, 0xF8) + 1);
    set_dp8(s, 0xF8, index);
    m_set_nz8(s, index);
    m_ldx(s, index);
    set_dp8(s, 0xEE, m_lda8(s, m_read8(s, m_abs(s, 0xFE00) + s->cpu.x)));
    ldx_object(s);
    m_sep(s, CPU_FLAG_X);
    begin_velocity(s);
    set_dp8(s, 0xDB, axis_velocity(s, false));
    x_axis_direction(s);
    set_dp8(s, 0xDD, axis_velocity(s, true));
    if (!extend_velocities(s, false)) {
        store_velocity(s);
        return;
    }
    tile_ahead(s);
    if (load_attributes(s) & 0x80) {
        m_sep(s, CPU_FLAG_X);
        set_dp8(s, 0xDB, 0x00);
        set_dp8(s, 0xDD, 0x00);
    } else {
        update_priority(s);
    }
    store_velocity(s);
}

/* One of the checks at $C0:5BF2, on the other object in $E9 against the
 * position in $DB (X) and $DD (Y), entered with the carry the caller left:
 * the other object is within 256 pixels on both axes and on the side the
 * facing points to. Carry set if so. Returns as its RTS would, minus the
 * stack pointer movement (the caller writes the JSR's stack bytes). */
static void object_ahead_check(Snes *s, int facing)
{
    m_rep(s, CPU_FLAG_M);
    m_ldx(s, m_read8(s, m_dp(s, 0xE9)));
    /* Facings 0 and 1 test Y first, 2 and 3 X first; even facings want the
     * other object before this one (negative difference). */
    uint16_t first_table = facing < 2 ? 0x1880 : 0x1800, second_table = facing < 2 ? 0x1800 : 0x1880;
    uint8_t first_pos = facing < 2 ? 0xDD : 0xDB, second_pos = facing < 2 ? 0xDB : 0xDD;
    bool want_negative = (facing & 1) == 0;

    m_lda16(s, obj16(s, first_table));
    uint16_t d = m_sbc16(s, s->cpu.a, dp16(s, first_pos));
    s->cpu.a = d;
    bool ok = want_negative ? (d & 0x8000) : !(d & 0x8000);
    if (ok) {
        if (want_negative)
            d = s->cpu.a = m_lda16(s, (uint16_t)~d);
        m_cmp16(s, d, 0x0100);
        if (d < 0x0100) {
            m_lda16(s, obj16(s, second_table));
            d = m_sbc16(s, s->cpu.a, dp16(s, second_pos));
            s->cpu.a = d;
            if (d & 0x8000)
                d = s->cpu.a = m_lda16(s, (uint16_t)~d);
            m_cmp16(s, d, 0x0100);
            if (d < 0x0100) {
                m_sep(s, CPU_FLAG_M);
                m_set_flag(s, CPU_FLAG_C, true);
                return;
            }
        }
    }
    m_sep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, false);
}

// $C0:5B95
/* Is another object in the way? Only for objects in view ($0F00,X) with
 * blocking on ($1C80,X bit 1): scans the object slots from the last down
 * (slot 0 isn't checked) for one in view, enabled, not this object and solid
 * ($1B01,X bit 0) that's ahead within 256 pixels. Carry set if one is, with
 * its slot in $E9. */
void field_object_in_the_way(Snes *s)
{
    set_dp8(s, 0xEA, 0x00);
    ldx_object(s);
    if (m_lda8(s, obj8(s, 0x0F00)) == 0) {
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    m_lda8(s, obj8(s, 0x1C80));
    m_bit_imm8(s, 0x02);
    if (!((uint8_t)s->cpu.a & 0x02)) {
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    set_dp16(s, 0xC7, s->cpu.y);
    uint8_t facing = m_asl8(s, m_lda8(s, obj8(s, 0x1600)));
    m_set_a8(s, facing);
    set_dp8(s, 0xD9, facing);
    m_rep(s, CPU_FLAG_M);
    set_dp16(s, 0xDB, m_lda16(s, obj16(s, 0x1800)));
    set_dp16(s, 0xDD, m_lda16(s, obj16(s, 0x1880)));
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    uint8_t count = m_asl8(s, m_lda8(s, m_read8(s, 0x7F2000)));
    m_set_a8(s, count);
    m_tax(s);

    for (;;) {
        if (m_lda8(s, obj8(s, 0x0F00)) != 0 && !(m_lda8(s, obj8(s, 0x1100)) & 0x80)) {
            uint8_t slot = (uint8_t)s->cpu.x, self = dp8(s, 0x6D);
            m_cmp8(s, slot, self);
            if (slot != self) {
                m_lda8(s, obj8(s, 0x1B01));
                m_bit_imm8(s, 0x01);
                if ((uint8_t)s->cpu.a & 0x01) {
                    set_dp8(s, 0xE9, slot);
                    m_ldx(s, dp8(s, 0xD9));
                    if (s->cpu.x >= 8) {
                        /* A facing outside the table; the original takes it from here. */
                        s->cpu.pb = 0xC0;
                        s->cpu.pc = 0x5BE6;
                        return;
                    }
                    /* JSR ($5BF2,X), inlined */
                    m_jsr_stack_bytes(s, 0x5BE8);
                    object_ahead_check(s, s->cpu.x >> 1);
                    m_ldx(s, dp8(s, 0xE9));
                    if (m_flag(s, CPU_FLAG_C)) {
                        m_rep(s, CPU_FLAG_X);
                        m_ldy(s, dp16(s, 0xC7));
                        m_rts(s);
                        return;
                    }
                }
            }
        }
        m_ldx(s, (uint16_t)(s->cpu.x - 2));
        if (s->cpu.x == 0)
            break;
    }
    m_rep(s, CPU_FLAG_X);
    m_ldy(s, dp16(s, 0xC7));
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* Set animation `number` ($1680,X) unless it's already playing, restarting it
 * ($1681,X and $1601,X cleared): LDA $1680,X : CMP #n : BEQ + : LDA #n : STA :
 * STZ $1681,X : STZ $1601,X : RTS */
static void set_animation(Snes *s, uint8_t number)
{
    uint8_t current = m_lda8(s, obj8(s, 0x1680));
    m_cmp8(s, current, number);
    if (current != number) {
        set_obj8(s, 0x1680, m_lda8(s, number));
        set_obj8(s, 0x1681, 0x00);
        set_obj8(s, 0x1601, 0x00);
    }
    m_rts(s);
}

// $C0:5614
/* Pick the object's movement animation from its velocity: 0 when not moving,
 * 1 when the absolute velocities add up to less than $20 (or the object's
 * state, $1100, is 4 or more), 6 otherwise. Only with no scripted animation
 * running ($1780 zero); a static frame (mode 3) is first turned back into
 * looping (or cleared if the animation is $FF). */
void field_update_walk_animation(Snes *s)
{
    ldx_object(s);
    uint8_t mode = m_lda8(s, obj8(s, 0x1780));
    if (mode != 0) {
        m_cmp8(s, mode, 0x03);
        if (mode != 0x03) {
            m_rts(s);
            return;
        }
        uint8_t animation = m_lda8(s, obj8(s, 0x1680));
        m_cmp8(s, animation, 0xFF);
        if (animation != 0xFF) {
            set_obj8(s, 0x1780, m_lda8(s, 0x01));
            m_rts(s);
            return;
        }
        set_obj8(s, 0x1780, 0x00);
    }
    uint8_t vx = m_lda8(s, obj8(s, 0x1900));
    if (vx & 0x80)
        vx = negate8(s, vx);
    set_dp8(s, 0xD9, vx);
    uint8_t vy = m_lda8(s, obj8(s, 0x1980));
    if (vy & 0x80)
        vy = negate8(s, vy);
    m_set_flag(s, CPU_FLAG_C, false);
    uint8_t total = m_adc8(s, vy, dp8(s, 0xD9));
    m_set_a8(s, total);
    if (total == 0) {
        set_animation(s, 0x00);
        return;
    }
    m_cmp8(s, total, 0x20);
    if (total >= 0x20) {
        uint8_t state = m_lda8(s, obj8(s, 0x1100));
        m_cmp8(s, state, 0x04);
        if (state < 0x04) {
            set_animation(s, 0x06);
            return;
        }
    }
    set_animation(s, 0x01);
}

// $C0:568B
/* Standing animation (0) for the object, unless a scripted animation runs. */
void field_stand_animation(Snes *s)
{
    ldx_object(s);
    if (m_lda8(s, obj8(s, 0x1780)) != 0) {
        m_rts(s);
        return;
    }
    set_animation(s, 0x00);
}

/* A = 16-bit position `table`,X & $FFF0 for object slot `slot_cell` ($6D or
 * $E9). */
static uint16_t aligned_position(Snes *s, uint8_t slot_cell, uint16_t table)
{
    m_ldx(s, dp16(s, slot_cell));
    uint16_t v = m_lda16(s, obj16(s, table));
    return m_lda16(s, (uint16_t)(v & 0xFFF0));
}

/* Compare the object's and the target's ($E9) aligned position on one axis,
 * M 16-bit: 0 if equal, else the sign of target - object. */
static int compare_axis(Snes *s, uint16_t table)
{
    uint16_t own = aligned_position(s, 0x6D, table);
    set_dp16(s, 0xD9, own);
    uint16_t target = aligned_position(s, 0xE9, table);
    m_cmp16(s, target, own);
    return target == own ? 0 : target > own ? 1 : -1;
}

/* SEP #$20 : LDA #$10 or #$F0 : LDX $6D : STA table,X */
static void set_axis_step(Snes *s, uint16_t table, int direction)
{
    m_sep(s, CPU_FLAG_M);
    uint8_t v = m_lda8(s, direction > 0 ? 0x10 : 0xF0);
    ldx_object(s);
    set_obj8(s, table, v);
}

/* LDA #$01 : STA $1A01,X : STA $7F0B00,X : SEC : RTS -- one step. */
static void one_step(Snes *s, bool with_speed)
{
    set_obj8(s, 0x1A01, m_lda8(s, 0x01));
    if (with_speed)
        m_write8(s, 0x7F0B00u + s->cpu.x, 0x01);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C0:30B3
/* Line the object up with the object in $E9 to within a pixel block ($FFF0):
 * a step of $10 toward it on X, then on Y. Carry clear if already aligned. */
void field_line_up_with_object(Snes *s)
{
    ldx_object(s);
    set_obj8(s, 0x1900, 0x00);
    set_obj8(s, 0x1980, 0x00);
    m_rep(s, CPU_FLAG_M);
    int dx = compare_axis(s, 0x1800);
    if (dx == 0) {
        int dy = compare_axis(s, 0x1880);
        if (dy == 0) {
            m_sep(s, CPU_FLAG_M);
            m_set_flag(s, CPU_FLAG_C, false);
            m_rts(s);
            return;
        }
        set_axis_step(s, 0x1980, dy);
        one_step(s, true);
        return;
    }
    set_axis_step(s, 0x1900, dx);
    m_rep(s, CPU_FLAG_M);
    int dy = compare_axis(s, 0x1880);
    if (dy == 0) {
        ldx_object(s);
        m_sep(s, CPU_FLAG_M);
        one_step(s, true);
        return;
    }
    set_axis_step(s, 0x1980, dy);
    one_step(s, true);
}

// $C0:305D
/* Line the object up with its tile: the sub-tile X byte ($1800) to $80 and Y
 * ($1880) to $F0 (upper nibbles), a step of $10 toward each. Carry clear if
 * already aligned. */
void field_line_up_with_tile(Snes *s)
{
    ldx_object(s);
    set_obj8(s, 0x1900, 0x00);
    set_obj8(s, 0x1980, 0x00);
    uint8_t x = m_lda8(s, (uint8_t)(obj8(s, 0x1800) & 0xF0));
    m_cmp8(s, x, 0x80);
    bool x_aligned = x == 0x80;
    if (!x_aligned)
        set_obj8(s, 0x1900, m_lda8(s, x < 0x80 ? 0x10 : 0xF0));
    uint8_t y = m_lda8(s, (uint8_t)(obj8(s, 0x1880) & 0xF0));
    m_cmp8(s, y, 0xF0);
    if (y == 0xF0) {
        if (x_aligned) {
            m_set_flag(s, CPU_FLAG_C, false);
            m_rts(s);
            return;
        }
        one_step(s, false);
        return;
    }
    set_obj8(s, 0x1980, m_lda8(s, y < 0xF0 ? 0x10 : 0xF0));
    one_step(s, false);
}
