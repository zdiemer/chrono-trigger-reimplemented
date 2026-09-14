/* Event commands: calling functions in other objects' scripts, and returning.
 *
 * Each object's script starts with a table of 16 function offsets (2 bytes
 * each, at slot * 16 in the script buffer, slots being 2 bytes wide). A call
 * names a target object, a priority level (high nibble, lower numbers win) and
 * a function (low nibble). Objects run at the level in $1C00,X, and keep a
 * saved script position per level at $7F:0580 + level * $80 + slot.
 *
 * A call at a lower level than the target's current one preempts it: the
 * target's position is saved for its current level and it jumps to the
 * function at the new level. A call at a higher level is queued: the function
 * becomes the saved position for that level, unless one is already saved. A
 * return clears the current level's position and resumes at the next higher
 * level that has one.
 *
 * Calls skip targets that are disabled ($1100,X or $1000,X negative), and skip
 * or wait on targets refusing calls ($1C01,X nonzero). */
#include "event/event.h"

#include "event/script.h"

enum { LEVEL = 0x1C00, REFUSING = 0x1C01, STATE = 0x1100, FLAGS = 0x1000, POSITION = 0x1180,
       CALL_WAITING = 0x0980 /* in bank $7F, per caller */ };
enum { DP_CURRENT_LEVEL = 0xD9, DP_TARGET = 0xDB, DP_CALL = 0xDD, DP_LEVEL = 0xDF, DP_SAVE_INDEX = 0xE1,
       DP_TABLE = 0xE3 };
#define SAVED_POSITIONS 0x7F0580u

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }

/* TYX : INX x n : SEC : RTS */
static void next(Snes *s, int n)
{
    m_tyx(s);
    while (n--)
        m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* TYX : CLC : RTS -- wait on this command. */
static void wait(Snes *s)
{
    m_tyx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* A = level: STA $4202 : LDA #$80 : STA $4203 : NOP : NOP : REP #$20 : CLC :
 * LDA $4216 : ADC $DB : STA $E1 -- index of the target's saved position for
 * that level. Leaves M 16-bit. */
static uint16_t saved_position_index(Snes *s)
{
    m_write8(s, m_abs(s, 0x4202), (uint8_t)s->cpu.a);
    m_lda8(s, 0x80);
    m_write8(s, m_abs(s, 0x4203), 0x80);
    m_rep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, false);
    uint16_t product = m_lda16(s, m_read16(s, m_abs(s, 0x4216)));
    s->cpu.a = m_adc16(s, product, dp16(s, DP_TARGET));
    m_write16(s, m_dp(s, DP_SAVE_INDEX), s->cpu.a);
    return s->cpu.a;
}

/* LDA $DB : AND #$00FF : ASL A x4 : STA $E3 : LDA $DD : AND #$000F : ASL A :
 * CLC : ADC $E3 : TAX : LDA script,X -- the called function's offset (M 16-bit). */
static uint16_t function_offset(Snes *s)
{
    uint16_t a = m_lda16(s, dp16(s, DP_TARGET) & 0x00FF);
    for (int i = 0; i < 4; i++)
        a = m_asl16(s, a);
    s->cpu.a = a;
    m_write16(s, m_dp(s, DP_TABLE), a);
    a = m_lda16(s, dp16(s, DP_CALL) & 0x000F);
    s->cpu.a = a = m_asl16(s, a);
    m_set_flag(s, CPU_FLAG_C, false);
    s->cpu.a = m_adc16(s, a, dp16(s, DP_TABLE));
    m_tax(s);
    return m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
}

/* With X = target slot: STA $DB, clear $DC, and check the target can take the
 * call. Returns 0 if it can, 1 if it refuses calls ($1C01), 2 if disabled. The
 * order of TAX and STZ $DC differs between copies but has the same effect. */
static int check_target(Snes *s)
{
    m_write8(s, m_dp(s, DP_TARGET), (uint8_t)s->cpu.a);
    m_tax(s);
    m_write8(s, m_dp(s, DP_TARGET + 1), 0x00);
    if (m_lda8(s, m_read8(s, m_abs(s, REFUSING) + s->cpu.x)) != 0)
        return 1;
    if (m_lda8(s, m_read8(s, m_abs(s, STATE) + s->cpu.x)) & 0x80)
        return 2;
    if (m_lda8(s, m_read8(s, m_abs(s, FLAGS) + s->cpu.x)) & 0x80)
        return 2;
    return 0;
}

/* The disabled checks alone (the waiting check at $C0:61AF skips $1C01). */
static bool target_disabled(Snes *s)
{
    m_write8(s, m_dp(s, DP_TARGET), (uint8_t)s->cpu.a);
    m_write8(s, m_dp(s, DP_TARGET + 1), 0x00);
    m_tax(s);
    if (m_lda8(s, m_read8(s, m_abs(s, STATE) + s->cpu.x)) & 0x80)
        return true;
    return m_lda8(s, m_read8(s, m_abs(s, FLAGS) + s->cpu.x)) & 0x80;
}

/* LDA $1C00,X : STA $D9, then (with the call byte loaded by `load_call_byte`)
 * STA $DD : LSR A x4 : STA $DF : CMP $D9. Returns the comparison of the
 * called level with the target's current one (<0, 0, >0). */
static int compare_levels(Snes *s, void (*load_call_byte)(Snes *))
{
    m_write8(s, m_dp(s, DP_CURRENT_LEVEL), m_lda8(s, m_read8(s, m_abs(s, LEVEL) + s->cpu.x)));
    load_call_byte(s);
    uint8_t call = (uint8_t)s->cpu.a;
    m_write8(s, m_dp(s, DP_CALL), call);
    uint8_t level = call;
    for (int i = 0; i < 4; i++)
        level = m_lsr8(s, level);
    m_set_a8(s, level);
    m_write8(s, m_dp(s, DP_LEVEL), level);
    uint8_t current = dp8(s, DP_CURRENT_LEVEL);
    m_cmp8(s, level, current);
    return level < current ? -1 : level > current;
}

/* INY : TYX : LDA script,X (the call byte follows the target operand) */
static void call_byte_after_iny(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_lda8(s, script_byte(s, s->cpu.x));
}

/* TYX : INX : INX : LDA script,X */
static void call_byte_at_y2(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    m_inx(s);
    m_lda8(s, script_byte(s, s->cpu.x));
}

/* Preempt the target: save its position for its current level and start the
 * function at the new level. Ends with M 8-bit. */
static void preempt(Snes *s)
{
    m_lda8(s, dp8(s, DP_CURRENT_LEVEL));
    uint16_t index = saved_position_index(s);
    m_ldx(s, dp16(s, DP_TARGET));
    uint16_t position = m_lda16(s, m_read16(s, m_abs(s, POSITION) + s->cpu.x));
    m_ldx(s, index);
    m_write16(s, SAVED_POSITIONS + s->cpu.x, position);
    uint16_t start = function_offset(s);
    m_ldx(s, dp16(s, DP_TARGET));
    m_write16(s, m_abs(s, POSITION) + s->cpu.x, start);
    m_sep(s, CPU_FLAG_M);
    uint8_t level = m_lda8(s, dp8(s, DP_LEVEL));
    m_write8(s, m_abs(s, LEVEL) + s->cpu.x, level);
    m_write8(s, m_abs(s, 0x1A80) + s->cpu.x, 0x00);
    m_write8(s, m_abs(s, 0x1A01) + s->cpu.x, 0x00);
}

/* $C0:603E, A = the called level: queue the function as the target's saved
 * position for that level unless one is saved. */
static void queue(Snes *s)
{
    uint16_t index = saved_position_index(s);
    m_tax(s);
    if (m_lda16(s, m_read16(s, SAVED_POSITIONS + index)) == 0) {
        uint16_t start = function_offset(s);
        m_ldx(s, dp16(s, DP_SAVE_INDEX));
        m_write16(s, SAVED_POSITIONS + s->cpu.x, start);
    }
    m_sep(s, CPU_FLAG_M);
}

/* TYX : INX : TDC : XBA : LDA script,X -- the target operand, when Y is still
 * at the command byte. */
static void target_operand_from_command(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    m_tdc(s);
    m_xba(s);
    m_lda8(s, script_byte(s, s->cpu.x));
}

/* TAX : LDA $97,X -- the slot of party member A. */
static uint8_t party_slot(Snes *s)
{
    m_tax(s);
    return m_lda8(s, m_read8(s, (uint16_t)(s->cpu.d + DP_PARTY + s->cpu.x)));
}

/* $C0:5FBE, A = target slot, Y at the target operand: call without waiting. */
static void call_now(Snes *s)
{
    int refused = check_target(s);
    if (refused) {
        m_iny(s);
        m_iny(s);
        next(s, 0);
        return;
    }
    int order = compare_levels(s, call_byte_after_iny);
    if (order > 0)
        queue(s);
    else if (order < 0)
        preempt(s);
    m_iny(s);
    next(s, 0);
}

// $C0:5FB6
/* $02: call a function of the object in operand 1 (level and function in
 * operand 2), without waiting. */
void event_cmd_call(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_tdc(s);
    m_xba(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    call_now(s);
}

// $C0:61E9
/* $05: as $02 for party member (operand 1); skipped if there's no such member. */
void event_cmd_call_party_member(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_tdc(s);
    m_xba(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    if (party_slot(s) & 0x80) {
        m_iny(s);
        m_iny(s);
        next(s, 0);
        return;
    }
    call_now(s);
}

/* $C0:6086, A = target slot, Y at the command: wait until the target takes the
 * call (it isn't refusing calls, and the call's level beats its current one),
 * then continue; skip it if the target is disabled. */
static void call_when_accepted(Snes *s)
{
    switch (check_target(s)) {
    case 1: wait(s); return;
    case 2: next(s, 3); return;
    }
    if (compare_levels(s, call_byte_at_y2) >= 0) {
        wait(s);
        return;
    }
    preempt(s);
    next(s, 3);
}

// $C0:607E
/* $03: call a function of the object in operand 1, waiting until the call is
 * accepted. */
void event_cmd_call_when_accepted(Snes *s)
{
    target_operand_from_command(s);
    call_when_accepted(s);
}

// $C0:61FE
/* $06: as $03 for party member (operand 1). */
void event_cmd_call_party_member_when_accepted(Snes *s)
{
    target_operand_from_command(s);
    if (party_slot(s) & 0x80) {
        m_iny(s);
        m_iny(s);
        m_iny(s);
        next(s, 0);
        return;
    }
    call_when_accepted(s);
}

/* $C0:6122: the first part of a waiting call: as call_when_accepted, but once
 * the call starts, mark the caller as waiting ($7F:0980) and stay. */
static void start_waiting_call(Snes *s)
{
    switch (check_target(s)) {
    case 1: wait(s); return;
    case 2: next(s, 3); return;
    }
    if (compare_levels(s, call_byte_at_y2) >= 0) {
        wait(s);
        return;
    }
    preempt(s);
    m_lda8(s, 0x01);
    ldx_current_object(s);
    m_write8(s, 0x7F0000u + CALL_WAITING + s->cpu.x, 0x01);
    wait(s);
}

/* $C0:61AF: the second part: stay until the target has dropped back below the
 * called level (or is disabled), then clear the waiting mark and continue. */
static void finish_waiting_call(Snes *s)
{
    if (!target_disabled(s) && compare_levels(s, call_byte_at_y2) >= 0) {
        wait(s);
        return;
    }
    m_lda8(s, 0x00);
    ldx_current_object(s);
    m_write8(s, 0x7F0000u + CALL_WAITING + s->cpu.x, 0x00);
    next(s, 3);
}

static bool waiting_on_call(Snes *s)
{
    ldx_current_object(s);
    return m_lda8(s, m_read8(s, 0x7F0000u + CALL_WAITING + s->cpu.x)) != 0;
}

// $C0:6109
/* $04: call a function of the object in operand 1 and wait until it has run. */
void event_cmd_call_and_wait(Snes *s)
{
    bool waiting = waiting_on_call(s);
    target_operand_from_command(s);
    if (waiting)
        finish_waiting_call(s);
    else
        start_waiting_call(s);
}

// $C0:6214
/* $07: as $04 for party member (operand 1). */
void event_cmd_call_party_member_and_wait(Snes *s)
{
    bool waiting = waiting_on_call(s);
    target_operand_from_command(s);
    uint8_t slot = party_slot(s);
    if (waiting) {
        finish_waiting_call(s);
        return;
    }
    if (slot & 0x80) {
        m_iny(s);
        m_iny(s);
        m_iny(s);
        next(s, 0);
        return;
    }
    start_waiting_call(s);
}

// $C0:5F74
/* $00: return from a function: clear the saved position for the current level
 * and resume at the next higher level with one. At level 7, the lowest, wait
 * here instead. */
void event_cmd_return(Snes *s)
{
    ldx_current_object(s);
    uint8_t level = m_lda8(s, m_read8(s, m_abs(s, LEVEL) + s->cpu.x));
    m_cmp8(s, level, 0x07);
    if (level == 0x07) {
        wait(s);
        return;
    }
    m_write8(s, m_abs(s, 0x4202), level);
    m_lda8(s, 0x80);
    m_write8(s, m_abs(s, 0x4203), 0x80);
    m_rep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, false);
    uint16_t product = m_lda16(s, m_read16(s, m_abs(s, 0x4216)));
    s->cpu.a = m_adc16(s, product, m_read16(s, m_dp(s, DP_OBJECT)));
    m_tax(s);
    m_lda16(s, 0x0000);
    m_write16(s, SAVED_POSITIONS + s->cpu.x, 0x0000);

    /* $C0:5F97: step down a level until a saved position turns up. */
    for (unsigned guard = 0;; guard++) {
        if (guard == 0x10000) {
            /* Doesn't happen in the game; leave an endless search to the original. */
            s->cpu.pb = 0xC0;
            s->cpu.pc = 0x5F97;
            return;
        }
        m_txa(s);
        m_set_flag(s, CPU_FLAG_C, false);
        uint16_t index = m_adc16(s, s->cpu.a, 0x0080);
        s->cpu.a = index;
        m_sep(s, CPU_FLAG_M);
        ldx_current_object(s);
        uint32_t level_addr = m_abs(s, LEVEL) + s->cpu.x;
        uint8_t v = (uint8_t)(m_read8(s, level_addr) + 1);
        m_write8(s, level_addr, v);
        m_set_nz8(s, v);
        m_rep(s, CPU_FLAG_M);
        m_tax(s);
        if (m_lda16(s, m_read16(s, SAVED_POSITIONS + s->cpu.x)) != 0)
            break;
    }
    ldx_current_object(s);
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C0:6240
/* $08: make this object refuse calls ($1C01,X = 1). */
void event_cmd_refuse_calls(Snes *s)
{
    ldx_current_object(s);
    m_lda8(s, 0x01);
    m_write8(s, m_abs(s, REFUSING) + s->cpu.x, 0x01);
    next(s, 1);
}

// $C0:624B
/* $09: make this object accept calls ($1C01,X = 0). */
void event_cmd_accept_calls(Snes *s)
{
    ldx_current_object(s);
    m_lda8(s, 0x00);
    m_write8(s, m_abs(s, REFUSING) + s->cpu.x, 0x00);
    next(s, 1);
}

/* INY : TYX : TDC : XBA : LDA script,X : TAX -- X = the object operand. */
static void load_object_operand(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_tdc(s);
    m_xba(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_tax(s);
}

// $C0:6254
/* $0A: disable the object in operand 1 ($1100,X = $80, $1A81,X = 0). */
void event_cmd_disable_object(Snes *s)
{
    load_object_operand(s);
    m_lda8(s, 0x80);
    m_write8(s, m_abs(s, STATE) + s->cpu.x, 0x80);
    m_lda8(s, 0x00);
    m_write8(s, m_abs(s, 0x1A81) + s->cpu.x, 0x00);
    next(s, 1);
}

// $C0:6269
/* $0B: set bit 7 of $1000,X for the object in operand 1 (calls then skip it);
 * if that's this object, end its turn. */
void event_cmd_set_object_flag(Snes *s)
{
    load_object_operand(s);
    uint32_t addr = m_abs(s, FLAGS) + s->cpu.x;
    uint8_t v = m_lda8(s, (uint8_t)(m_read8(s, addr) | 0x80));
    m_write8(s, addr, v);
    uint16_t self = m_read16(s, m_dp(s, DP_OBJECT));
    m_cmp16(s, s->cpu.x, self);
    if (s->cpu.x != self) {
        next(s, 1);
        return;
    }
    m_tyx(s);
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:6282
/* $0C: clear bit 7 of $1000,X for the object in operand 1. */
void event_cmd_clear_object_flag(Snes *s)
{
    load_object_operand(s);
    uint32_t addr = m_abs(s, FLAGS) + s->cpu.x;
    uint8_t v = m_lda8(s, (uint8_t)(m_read8(s, addr) & 0x7F));
    m_write8(s, addr, v);
    next(s, 1);
}

/* INY : TYX : TDC : XBA : LDA script,X : LDX $6D : STA table,X : TYX : INX : SEC */
static void set_own_byte(Snes *s, uint16_t table)
{
    m_iny(s);
    m_tyx(s);
    m_tdc(s);
    m_xba(s);
    uint8_t v = m_lda8(s, script_byte(s, s->cpu.x));
    ldx_current_object(s);
    m_write8(s, m_abs(s, table) + s->cpu.x, v);
    next(s, 1);
}

// $C0:6295
/* $0D: set this object's $1C80,X to operand 1. */
void event_cmd_set_1c80(Snes *s) { set_own_byte(s, 0x1C80); }

// $C0:62A4
/* $0E: set this object's arrival flags ($1C81,X) to operand 1. */
void event_cmd_set_arrival_flags(Snes *s) { set_own_byte(s, 0x1C81); }
