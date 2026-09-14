/* Event commands: moving objects around the map.
 *
 * Object tables, indexed by slot, as these commands use them: $1100 state
 * (negative: skip the target), $1600 facing, $1801/$1881 tile X/Y, $1000 the
 * step count a move starts with, $1A01 steps left (the command waits while
 * nonzero), $1A80 set while a counted move runs, $1C81 flags choosing an extra
 * routine on arrival.
 *
 * The routines these commands call aren't ported yet and are called out, not
 * yet analyzed: $C0:ABA2 leaves a value in $EE that indexes the facing table at
 * $F700; $C0:5B95 returns a carry that makes the command add $30 to $EE;
 * $C0:ACFD runs after a move is set up; $C0:5614 runs when the command stays
 * on the move and $C0:568B when it finishes; $C0:30B3 and $C0:305D run on
 * arrival when the object's $1C81 flags ask for them, and carry set from them
 * keeps the command going. */
#include "event/event.h"

#include "event/script.h"

enum {
    STATE = 0x1100,
    FACING = 0x1600,
    STEP_LENGTH = 0x1000,
    TILE_X = 0x1801,
    TILE_Y = 0x1881,
    STEPS_LEFT = 0x1A01,
    COUNTED_MOVE = 0x1A80,
    ARRIVAL_FLAGS = 0x1C81,
    STEP_SPEED = 0x0B00,        /* in bank $7F */
};

enum { DP_TARGET_X = 0xF2, DP_TARGET_Y = 0xF3, DP_OWN_X = 0xF0, DP_OWN_Y = 0xF1, DP_TARGET = 0xE9,
       DP_DIRECTION = 0xEE, DP_SAVED_Y = 0xC7 };

/* Call out. False if the callee didn't return with 8-bit A and the expected X
 * width: the port then stops, at the call's return address, and the original
 * carries on from there. */
static bool call(Snes *s, uint32_t site, uint32_t target, bool x8)
{
    m_jsr(s, site, target);
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == (CPU_FLAG_M | (x8 ? CPU_FLAG_X : 0));
}

/* TYX : CLC : RTS -- end the turn, staying on this command. */
static void stay(Snes *s)
{
    m_tyx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* TYX : INX x n : SEC : RTS -- continue with the command n bytes on. */
static void advance(Snes *s, int n)
{
    m_tyx(s);
    while (n--)
        m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* LDX $6D : LDA $1A01,X : BEQ + : TYX : CLC : RTS
 * True (having ended the turn) if the object is still in the middle of a move. */
static bool still_moving(Snes *s)
{
    ldx_current_object(s);
    if (m_lda8(s, object_byte(s, STEPS_LEFT)) == 0)
        return false;
    stay(s);
    return true;
}

/* The counted moves' version:
 * LDX $6D : LDA $1A80,X : BEQ fresh : LDA $1A01,X : BEQ done : stay
 * done: STZ $1A80,X, continue after the 3-byte command.
 * True if the turn is decided; false to start a new move. */
static bool counted_move_in_progress(Snes *s)
{
    ldx_current_object(s);
    if (m_lda8(s, object_byte(s, COUNTED_MOVE)) == 0)
        return false;
    if (m_lda8(s, object_byte(s, STEPS_LEFT)) != 0) {
        stay(s);
        return true;
    }
    m_write8(s, m_abs(s, COUNTED_MOVE) + s->cpu.x, 0x00);
    advance(s, 3);
    return true;
}

/* TYX : INX : TDC : XBA : LDA script,X : TAX -- X = the object slot operand. */
static void load_object_operand(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    m_tdc(s);
    m_xba(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_tax(s);
}

/* LDA $97,X : TAX -- X = the slot of party member X. */
static void party_member_slot(Snes *s)
{
    m_lda8(s, m_read8(s, (uint16_t)(s->cpu.d + DP_PARTY + s->cpu.x)));
    m_tax(s);
}

/* A is already loaded: CMP t : BEQ : INC A : CMP t : BEQ : DEC A : DEC A : CMP t
 * True if A is within one of t. */
static bool within_one(Snes *s, uint8_t t)
{
    uint8_t a = (uint8_t)s->cpu.a;
    for (int delta = 0; delta < 3; delta++) {
        if (delta == 1)
            a = m_lda8(s, (uint8_t)(a + 1));
        else if (delta == 2)
            a = m_lda8(s, (uint8_t)(a - 2));
        m_cmp8(s, a, t);
        if (a == t)
            return true;
    }
    return false;
}

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }

/* With X = the target slot: record the target's tile ($F2/$F3) and slot ($E9)
 * and the current object's tile ($F0/$F1), leaving X = the current object and
 * A = its tile Y. */
static void load_positions(Snes *s)
{
    set_dp8(s, DP_TARGET_X, m_lda8(s, object_byte(s, TILE_X)));
    set_dp8(s, DP_TARGET_Y, m_lda8(s, object_byte(s, TILE_Y)));
    m_write16(s, m_dp(s, DP_TARGET), s->cpu.x);
    ldx_current_object(s);
    set_dp8(s, DP_OWN_X, m_lda8(s, object_byte(s, TILE_X)));
    set_dp8(s, DP_OWN_Y, m_lda8(s, object_byte(s, TILE_Y)));
}

/* After load_positions: true if within one tile of the target on both axes. */
static bool next_to_target(Snes *s)
{
    if (!within_one(s, dp8(s, DP_TARGET_Y)))
        return false;
    m_lda8(s, dp8(s, DP_OWN_X));
    return within_one(s, dp8(s, DP_TARGET_X));
}

/* LDA $F700,X : LDX $6D : STA $1600,X -- face the direction indexed by X. */
static void set_facing_from_direction(Snes *s)
{
    uint8_t facing = m_lda8(s, m_read8(s, m_abs(s, 0xF700) + s->cpu.x));
    ldx_current_object(s);
    m_write8(s, m_abs(s, FACING) + s->cpu.x, facing);
}

/* Code at `at`, 39 bytes, the same in each move command: call $C0:ABA2 and
 * face the direction $F700[$EE] gives; if $C0:5B95 then returns carry set, add
 * $30 to $EE and face $F700[$EE] instead. Returns false if the port stopped at
 * a call-out. */
static bool face_target(Snes *s, uint32_t at)
{
    if (!call(s, at, 0xC0ABA2, false))
        return false;
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, m_read16(s, m_dp(s, DP_DIRECTION)));
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    set_facing_from_direction(s);
    if (!call(s, at + 0x12, 0xC05B95, false))
        return false;
    if (m_flag(s, CPU_FLAG_C)) {
        m_lda8(s, dp8(s, DP_DIRECTION));
        m_set_flag(s, CPU_FLAG_C, false);
        uint8_t turned = m_adc8(s, (uint8_t)s->cpu.a, 0x30);
        m_set_a8(s, turned);
        set_dp8(s, DP_DIRECTION, turned);
        m_tax(s);
        set_facing_from_direction(s);
    }
    return true;
}

/* Code at `at`: LDX $6D : LDA $1000,X : STA $1A01,X : STA $7F0B00,X : STY $C7 :
 * JSR $ACFD [: JSR $5614] : LDX $C7 : CLC : RTS -- start a move of the
 * object's step length, and end the turn on this command. */
static void start_move(Snes *s, uint32_t at, bool moving_hook)
{
    ldx_current_object(s);
    uint8_t length = m_lda8(s, object_byte(s, STEP_LENGTH));
    m_write8(s, m_abs(s, STEPS_LEFT) + s->cpu.x, length);
    m_write8(s, 0x7F0000u + STEP_SPEED + s->cpu.x, length);
    m_write16(s, m_dp(s, DP_SAVED_Y), s->cpu.y);
    if (!call(s, at + 0x0E, 0xC0ACFD, false))
        return;
    if (moving_hook && !call(s, at + 0x11, 0xC05614, false))
        return;
    m_ldx(s, m_read16(s, m_dp(s, DP_SAVED_Y)));
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* JSR $568B : TYX : INX x n : SEC : RTS -- arrived; next command. */
static void arrive(Snes *s, uint32_t site, int command_length)
{
    if (call(s, site, 0xC0568B, false))
        advance(s, command_length);
}

/* JSR $5614 : TYX : CLC : RTS -- stay on the command. */
static void keep_moving(Snes *s, uint32_t site)
{
    if (call(s, site, 0xC05614, false))
        stay(s);
}

/* LDX $6D : LDA $1C81,X : BIT #bit -- whether an arrival routine applies. */
static bool lines_up_on_arrival(Snes *s, uint8_t bit)
{
    ldx_current_object(s);
    m_lda8(s, object_byte(s, ARRIVAL_FLAGS));
    m_bit_imm8(s, bit);
    return (uint8_t)s->cpu.a & bit;
}

/* $C0:5228, with X = the target's slot: move toward it one step at a time,
 * arriving when within one tile. */
static void approach(Snes *s)
{
    if (m_lda8(s, object_byte(s, STATE)) & 0x80) {
        arrive(s, 0xC05273, 2);
        return;
    }
    load_positions(s);
    if (next_to_target(s)) {
        if (lines_up_on_arrival(s, 0x02)) {
            if (!call(s, 0xC0526E, 0xC030B3, false))
                return;
            if (m_flag(s, CPU_FLAG_C)) {
                keep_moving(s, 0xC0527B);
                return;
            }
        }
        arrive(s, 0xC05273, 2);
        return;
    }
    if (face_target(s, 0xC05281))
        start_move(s, 0xC052A8, true);
}

// $C0:5215
/* $94: move toward the object in operand 1 until next to it. */
void event_cmd_move_toward_object(Snes *s)
{
    if (still_moving(s))
        return;
    load_object_operand(s);
    approach(s);
}

// $C0:5410
/* $95: move toward party member (operand 1) until next to them. */
void event_cmd_move_toward_party_member(Snes *s)
{
    if (still_moving(s))
        return;
    load_object_operand(s);
    party_member_slot(s);
    approach(s);
}

/* $C0:52D3, with X = the target's slot: as approach, but the object keeps its
 * facing and doesn't run the arrival and moving hooks. */
static void approach_keeping_facing(Snes *s)
{
    if (m_lda8(s, object_byte(s, STATE)) & 0x80) {
        advance(s, 2);
        return;
    }
    load_positions(s);
    if (next_to_target(s)) {
        if (lines_up_on_arrival(s, 0x02)) {
            if (!call(s, 0xC05319, 0xC030B3, false))
                return;
            if (m_flag(s, CPU_FLAG_C)) {
                stay(s);
                return;
            }
        }
        advance(s, 2);
        return;
    }
    if (!call(s, 0xC05326, 0xC0ABA2, false))
        return;
    if (!call(s, 0xC05329, 0xC05B95, false))
        return;
    if (m_flag(s, CPU_FLAG_C)) {
        m_lda8(s, dp8(s, DP_DIRECTION));
        m_set_flag(s, CPU_FLAG_C, false);
        uint8_t turned = m_adc8(s, (uint8_t)s->cpu.a, 0x30);
        m_set_a8(s, turned);
        set_dp8(s, DP_DIRECTION, turned);
    }
    start_move(s, 0xC05335, false);
}

// $C0:52C0
/* $9E: as $94, keeping the object's facing. */
void event_cmd_move_toward_object_keep_facing(Snes *s)
{
    if (still_moving(s))
        return;
    load_object_operand(s);
    approach_keeping_facing(s);
}

// $C0:54F5
/* $9F: as $95, keeping the object's facing. */
void event_cmd_move_toward_party_member_keep_facing(Snes *s)
{
    if (still_moving(s))
        return;
    load_object_operand(s);
    party_member_slot(s);
    approach_keeping_facing(s);
}

/* $C0:536B, with X = the target's slot: one move of operand 2 steps toward the
 * target; the command finishes when that move does, or on arrival. */
static void approach_counted(Snes *s)
{
    if (!(m_lda8(s, object_byte(s, STATE)) & 0x80)) {
        load_positions(s);
        if (!next_to_target(s)) {
            if (!face_target(s, 0xC053CA))
                return;
            /* $C0:53F1 */
            m_tyx(s);
            m_inx(s);
            m_inx(s);
            uint8_t steps = m_lda8(s, script_byte(s, s->cpu.x));
            ldx_current_object(s);
            m_write8(s, m_abs(s, STEPS_LEFT) + s->cpu.x, steps);
            m_write8(s, 0x7F0000u + STEP_SPEED + s->cpu.x, steps);
            uint32_t flag = m_abs(s, COUNTED_MOVE) + s->cpu.x;
            uint8_t v = (uint8_t)(m_read8(s, flag) + 1);
            m_write8(s, flag, v);
            m_set_nz8(s, v);
            m_write16(s, m_dp(s, DP_SAVED_Y), s->cpu.y);
            if (!call(s, 0xC05406, 0xC0ACFD, false) || !call(s, 0xC05409, 0xC05614, false))
                return;
            m_ldx(s, m_read16(s, m_dp(s, DP_SAVED_Y)));
            m_set_flag(s, CPU_FLAG_C, false);
            m_rts(s);
            return;
        }
        if (lines_up_on_arrival(s, 0x02)) {
            if (!call(s, 0xC053B1, 0xC030B3, false))
                return;
            if (m_flag(s, CPU_FLAG_C)) {
                keep_moving(s, 0xC053C4);
                return;
            }
        }
    }
    /* $C0:53B6 */
    ldx_current_object(s);
    m_write8(s, m_abs(s, COUNTED_MOVE) + s->cpu.x, 0x00);
    arrive(s, 0xC053BB, 3);
}

// $C0:534A
/* $98: move up to operand 2 steps toward the object in operand 1. */
void event_cmd_move_toward_object_counted(Snes *s)
{
    if (counted_move_in_progress(s))
        return;
    load_object_operand(s);
    approach_counted(s);
}

// $C0:550E
/* $99: move up to operand 2 steps toward party member (operand 1). */
void event_cmd_move_toward_party_member_counted(Snes *s)
{
    if (counted_move_in_progress(s))
        return;
    load_object_operand(s);
    party_member_slot(s);
    approach_counted(s);
}

// $C0:5429
/* $8F: move toward party member (operand 1). Arrives when next to them, or
 * when not next to them but the object's tile is at least 2 tiles inside the
 * screen's left/top edge ($1D0A/$1D0E, halved) and within 14 columns and 13
 * rows of it. */
void event_cmd_move_toward_party_member_until_on_screen(Snes *s)
{
    if (still_moving(s))
        return;
    load_object_operand(s);
    party_member_slot(s);
    if (m_lda8(s, object_byte(s, STATE)) & 0x80) {
        arrive(s, 0xC054A8, 2);
        return;
    }
    load_positions(s);
    if (!next_to_target(s)) {
        /* $C0:547C */
        uint8_t v = m_read8(s, m_abs(s, 0x1D0A));
        m_set_flag(s, CPU_FLAG_C, v & 1);
        set_dp8(s, 0xDB, m_lda8(s, v >> 1));
        v = m_read8(s, m_abs(s, 0x1D0E));
        m_set_flag(s, CPU_FLAG_C, v & 1);
        set_dp8(s, 0xDD, m_lda8(s, v >> 1));
        bool on_screen = false;
        m_set_flag(s, CPU_FLAG_C, true);
        uint8_t dx = m_sbc8(s, m_lda8(s, dp8(s, DP_OWN_X)), dp8(s, 0xDB));
        m_set_a8(s, dx);
        if (dx != 0) {
            m_cmp8(s, dx, 0x01);
            if (dx != 0x01) {
                m_cmp8(s, dx, 0x0E);
                if (dx < 0x0E) {
                    m_set_flag(s, CPU_FLAG_C, true);
                    uint8_t dy = m_sbc8(s, m_lda8(s, dp8(s, DP_OWN_Y)), dp8(s, 0xDD));
                    m_set_a8(s, dy);
                    if (dy != 0) {
                        m_cmp8(s, dy, 0x01);
                        if (dy != 0x01) {
                            m_cmp8(s, dy, 0x0D);
                            on_screen = dy < 0x0D;
                        }
                    }
                }
            }
        }
        if (!on_screen) {
            if (face_target(s, 0xC054B6))
                start_move(s, 0xC054DD, true);
            return;
        }
    }
    ldx_current_object(s);
    arrive(s, 0xC054A8, 2);
}

// $C0:4F61
/* $96: move to the tile at operands (X, Y). */
void event_cmd_move_to_tile(Snes *s)
{
    if (still_moving(s))
        return;
    m_tyx(s);
    m_inx(s);
    set_dp8(s, DP_TARGET_X, m_lda8(s, script_byte(s, s->cpu.x)));
    m_inx(s);
    set_dp8(s, DP_TARGET_Y, m_lda8(s, script_byte(s, s->cpu.x)));
    ldx_current_object(s);
    set_dp8(s, DP_OWN_X, m_lda8(s, object_byte(s, TILE_X)));
    uint8_t y = m_lda8(s, object_byte(s, TILE_Y));
    set_dp8(s, DP_OWN_Y, y);
    uint8_t ty = dp8(s, DP_TARGET_Y);
    m_cmp8(s, y, ty);
    bool there = false;
    if (y == ty) {
        uint8_t x = m_lda8(s, dp8(s, DP_OWN_X));
        uint8_t tx = dp8(s, DP_TARGET_X);
        m_cmp8(s, x, tx);
        there = x == tx;
    }
    if (!there) {
        if (face_target(s, 0xC04FAB))
            start_move(s, 0xC04FD2, true);
        return;
    }
    m_lda8(s, object_byte(s, ARRIVAL_FLAGS));
    m_bit_imm8(s, 0x01);
    if ((uint8_t)s->cpu.a & 0x01) {
        if (!call(s, 0xC04F97, 0xC0305D, false))
            return;
        if (m_flag(s, CPU_FLAG_C)) {
            keep_moving(s, 0xC04FA5);
            return;
        }
    }
    arrive(s, 0xC04F9C, 3);
}

// $C0:4F0A
/* $9D: move in the direction held in variable (operand 1) for the number of
 * steps in variable (operand 2). */
void event_cmd_move_by_variables(Snes *s)
{
    if (counted_move_in_progress(s))
        return;
    m_tyx(s);
    m_inx(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_rep(s, CPU_FLAG_M);
    s->cpu.a = m_asl16(s, m_lda16(s, s->cpu.a & 0x00FF));
    m_tax(s);
    m_lda16(s, m_read16(s, VARS + s->cpu.x));
    m_write16(s, m_dp(s, DP_DIRECTION), m_lda16(s, s->cpu.a & 0x00FF));
    m_tyx(s);
    m_inx(s);
    m_inx(s);
    m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
    s->cpu.a = m_asl16(s, m_lda16(s, s->cpu.a & 0x00FF));
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    uint8_t steps = m_lda8(s, m_read8(s, VARS + s->cpu.x));
    ldx_current_object(s);
    m_write8(s, m_abs(s, STEPS_LEFT) + s->cpu.x, steps);
    m_lda8(s, 0x80);
    m_write8(s, 0x7F0000u + STEP_SPEED + s->cpu.x, 0x80);
    uint32_t flag = m_abs(s, COUNTED_MOVE) + s->cpu.x;
    uint8_t v = (uint8_t)(m_read8(s, flag) + 1);
    m_write8(s, flag, v);
    m_set_nz8(s, v);
    m_write16(s, m_dp(s, DP_SAVED_Y), s->cpu.y);
    if (!call(s, 0xC04F5A, 0xC0ACFD, false))
        return;
    m_ldx(s, m_read16(s, m_dp(s, DP_SAVED_Y)));
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:3002
/* $B0: run a routine chosen by the object's state ($1100,X): 0 runs
 * $C0:9E29 and then, if $C0:5B8D accepts the object's facing, $C0:3154; 1 runs
 * $C0:A26B; 2 runs $C0:A2CE; each sets $1A01,X to 1 first. Other states do
 * nothing. Does nothing while $38 is nonzero. Always ends the turn on this
 * command. */
void event_cmd_run_state_routine(Snes *s)
{
    if (m_lda8(s, dp8(s, 0x38)) != 0) {
        stay(s);
        return;
    }
    m_write16(s, m_dp(s, DP_SAVED_Y), s->cpu.y);
    m_sep(s, CPU_FLAG_X);
    ldx_current_object(s);
    uint8_t state = m_lda8(s, object_byte(s, STATE));
    uint32_t site, target;
    if (state == 0) {
        site = 0xC03047, target = 0xC09E29;
    } else if (m_lda8(s, (uint8_t)(state - 1)) == 0) {
        site = 0xC03037, target = 0xC0A26B;
    } else if (m_lda8(s, (uint8_t)(state - 2)) == 0) {
        site = 0xC03027, target = 0xC0A2CE;
    } else {
        m_rep(s, CPU_FLAG_X);
        m_ldx(s, m_read16(s, m_dp(s, DP_SAVED_Y)));
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    m_lda8(s, 0x01);
    ldx_current_object(s);
    m_write8(s, m_abs(s, STEPS_LEFT) + s->cpu.x, 0x01);
    if (!call(s, site, target, true))
        return;
    if (state == 0) {
        ldx_current_object(s);
        m_lda8(s, object_byte(s, FACING));
        if (!call(s, 0xC0304F, 0xC05B8D, true))
            return;
        if (m_flag(s, CPU_FLAG_C) && !call(s, 0xC03054, 0xC03154, true))
            return;
    }
    m_rep(s, CPU_FLAG_X);
    m_ldx(s, m_read16(s, m_dp(s, DP_SAVED_Y)));
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* Commands that repeat a move command every frame: STY $E3 : JSR cmd :
 * LDX $E3 [: CLC] : RTS, so the script never moves past them. */
static void repeat_command(Snes *s, uint32_t site, uint32_t command, bool end_turn)
{
    m_write16(s, m_dp(s, 0xE3), s->cpu.y);
    if (!call(s, site, command, false))
        return;
    m_ldx(s, m_read16(s, m_dp(s, 0xE3)));
    if (end_turn)
        m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:3546
/* $B5: keep following the object in operand 1 (runs $94 every turn, and ends
 * the turn). */
void event_cmd_follow_object(Snes *s) { repeat_command(s, 0xC03548, 0xC05215, true); }

// $C0:354F
/* $B6: keep following party member (operand 1) (runs $95 every turn; the
 * turn continues when $95 would continue). */
void event_cmd_follow_party_member(Snes *s) { repeat_command(s, 0xC03551, 0xC05410, false); }
