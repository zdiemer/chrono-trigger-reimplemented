/* Event commands: setting which way objects face ($1600,X, with $1601,X
 * cleared). All of these end the object's turn. */
#include "event/event.h"

#include "event/script.h"

/* $C0:554D: STA $1600,X : STZ $1601,X : TYX : INX : CLC : RTS */
static void set_facing(Snes *s)
{
    m_write8(s, m_abs(s, 0x1600) + s->cpu.x, (uint8_t)s->cpu.a);
    m_write8(s, m_abs(s, 0x1601) + s->cpu.x, 0x00);
    m_tyx(s);
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* $C0:554B: LDX $6D, then set_facing. */
static void set_own_facing(Snes *s)
{
    ldx_current_object(s);
    set_facing(s);
}

/* LDA #direction, then set_own_facing. */
static void face_direction(Snes *s, uint8_t direction)
{
    m_lda8(s, direction);
    set_own_facing(s);
}

// $C0:5535
/* $0F: face direction 0. */
void event_cmd_face_0(Snes *s) { face_direction(s, 0); }

// $C0:5539
/* $17: face direction 1. */
void event_cmd_face_1(Snes *s) { face_direction(s, 1); }

// $C0:553D
/* $1B: face direction 2. */
void event_cmd_face_2(Snes *s) { face_direction(s, 2); }

// $C0:5541
/* $1D: face direction 3. */
void event_cmd_face_3(Snes *s) { face_direction(s, 3); }

// $C0:5545
/* $A6: face the direction in operand 1. */
void event_cmd_face_operand(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    set_own_facing(s);
}

// $C0:5557
/* $A7: face the direction in variable (operand 1). */
void event_cmd_face_variable(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_rep(s, CPU_FLAG_M);
    s->cpu.a = m_asl16(s, m_lda16(s, s->cpu.a & 0x00FF));
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, m_read8(s, VARS + s->cpu.x));
    set_own_facing(s);
}

/* INY : TYX : TDC : XBA : LDA script,X : TAX : LDA #direction, then set_facing:
 * make the object in operand 1 face `direction`. */
static void object_face_direction(Snes *s, uint8_t direction)
{
    m_iny(s);
    m_tyx(s);
    m_tdc(s);
    m_xba(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_tax(s);
    m_lda8(s, direction);
    set_facing(s);
}

// $C0:556C
/* $1E: make the object in operand 1 face direction 0. */
void event_cmd_object_face_0(Snes *s) { object_face_direction(s, 0); }

// $C0:5579
/* $1F: make the object in operand 1 face direction 1. */
void event_cmd_object_face_1(Snes *s) { object_face_direction(s, 1); }

// $C0:5586
/* $25: make the object in operand 1 face direction 2. */
void event_cmd_object_face_2(Snes *s) { object_face_direction(s, 2); }

// $C0:5593
/* $26: make the object in operand 1 face direction 3. */
void event_cmd_object_face_3(Snes *s) { object_face_direction(s, 3); }

/* $C0:55A9, with X = the target's slot: face toward the target. If both stand
 * on the same tile, the direction comes from their $1800/$1880 bytes instead
 * of their tiles. $C0:ABA2 (called out) turns $F0-$F3 into $EE, which indexes
 * the facing table at $F700. A disabled target skips the command. */
static void face_object(Snes *s)
{
    if (m_lda8(s, object_byte(s, 0x1100)) & 0x80) {
        m_tyx(s);
        m_inx(s);
        m_inx(s);
        m_set_flag(s, CPU_FLAG_C, true);
        m_rts(s);
        return;
    }
    m_write8(s, m_dp(s, 0xF2), m_lda8(s, object_byte(s, 0x1801)));
    m_write8(s, m_dp(s, 0xF3), m_lda8(s, object_byte(s, 0x1881)));
    m_write16(s, m_dp(s, 0xD9), s->cpu.x);
    ldx_current_object(s);
    m_write8(s, m_dp(s, 0xF0), m_lda8(s, object_byte(s, 0x1801)));
    uint8_t own_y = m_lda8(s, object_byte(s, 0x1881));
    m_write8(s, m_dp(s, 0xF1), own_y);
    uint8_t target_y = m_read8(s, m_dp(s, 0xF3));
    m_cmp8(s, own_y, target_y);
    if (own_y == target_y) {
        uint8_t own_x = m_lda8(s, m_read8(s, m_dp(s, 0xF0)));
        uint8_t target_x = m_read8(s, m_dp(s, 0xF2));
        m_cmp8(s, own_x, target_x);
        if (own_x == target_x) {
            m_write8(s, m_dp(s, 0xF0), m_lda8(s, object_byte(s, 0x1800)));
            m_write8(s, m_dp(s, 0xF1), m_lda8(s, object_byte(s, 0x1880)));
            m_ldx(s, m_read16(s, m_dp(s, 0xD9)));
            m_write8(s, m_dp(s, 0xF2), m_lda8(s, object_byte(s, 0x1800)));
            m_write8(s, m_dp(s, 0xF3), m_lda8(s, object_byte(s, 0x1880)));
        }
    }
    m_jsr(s, 0xC055E6, 0xC0ABA2);
    if (s->cpu.e || (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
        return;   /* unexpected widths: the original carries on from here */
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, m_read16(s, m_dp(s, 0xEE)));
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    uint8_t facing = m_lda8(s, m_read8(s, m_abs(s, 0xF700) + s->cpu.x));
    ldx_current_object(s);
    m_write8(s, m_abs(s, 0x1600) + s->cpu.x, facing);
    m_write8(s, m_abs(s, 0x1601) + s->cpu.x, 0x00);
    m_tyx(s);
    m_inx(s);
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:55A0
/* $A8: face the object in operand 1. */
void event_cmd_face_object(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    m_tdc(s);
    m_xba(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_tax(s);
    face_object(s);
}

// $C0:5605
/* $A9: face party member (operand 1). */
void event_cmd_face_party_member(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    m_tdc(s);
    m_xba(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_tax(s);
    m_lda8(s, m_read8(s, (uint16_t)(s->cpu.d + DP_PARTY + s->cpu.x)));
    m_tax(s);
    face_object(s);
}
