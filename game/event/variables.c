/* Event commands: copying values into script variables and object data. */
#include "event/event.h"

#include "event/script.h"

/* INY : TYX : LDA script,X : REP #$20 : AND #$00FF : ASL A : TAX
 * X = variable index (operand) * 2; leaves M 16-bit. */
static void load_var_index(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_rep(s, CPU_FLAG_M);
    s->cpu.a = m_asl16(s, m_lda16(s, s->cpu.a & 0x00FF));
    m_tax(s);
}

/* $C0:655C, A 16-bit: LDX $6D : STA $7F0A80,X : SEP #$20 : TYX : INX : SEC : RTS */
static void store_object_value(Snes *s)
{
    ldx_current_object(s);
    m_write16(s, 0x7F0A80u + s->cpu.x, s->cpu.a);
    m_sep(s, CPU_FLAG_M);
    m_tyx(s);
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C0:653B
/* $19: set the object's 16-bit $7F:0A80 value from variable (operand 1). */
void event_cmd_set_object_value_from_var(Snes *s)
{
    load_var_index(s);
    m_lda16(s, m_read16(s, VARS + s->cpu.x));
    store_object_value(s);
}

// $C0:654E
/* $1C: set the object's 16-bit $7F:0A80 value from the two bytes at $7F:0000
 * + the 16-bit operand. */
void event_cmd_set_object_value_from_memory(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
    m_iny(s);
    m_tax(s);
    m_lda16(s, m_read16(s, 0x7F0000u + s->cpu.x));
    store_object_value(s);
}

// $C0:658F
/* $20: set variable (operand 1) to the byte at $7E:2980. */
void event_cmd_set_var_from_2980(Snes *s)
{
    load_var_index(s);
    m_sep(s, CPU_FLAG_M);
    uint8_t v = m_lda8(s, m_read8(s, 0x7E2980));
    m_write8(s, VARS + s->cpu.x, v);
    m_iny(s);
    m_tyx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* Y at the operand before a variable index: store `value` into that
 * variable. Leaves Y at the index operand. */
static void store_var(Snes *s, uint8_t value)
{
    load_var_index(s);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, value);
    m_write8(s, VARS + s->cpu.x, value);
}

/* INY : TYX : TDC : XBA : LDA script,X -- the object operand, in A. */
static void load_object_operand(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_tdc(s);
    m_xba(s);
    m_lda8(s, script_byte(s, s->cpu.x));
}

/* $C0:65B2, A = slot: variables (operand 2, operand 3) = the object's tile. */
static void store_tile(Snes *s)
{
    m_tax(s);
    m_write8(s, m_dp(s, 0xD9), m_lda8(s, object_byte(s, 0x1801)));
    m_write8(s, m_dp(s, 0xDB), m_lda8(s, object_byte(s, 0x1881)));
    store_var(s, m_read8(s, m_dp(s, 0xD9)));
    store_var(s, m_read8(s, m_dp(s, 0xDB)));
    m_iny(s);
    m_tyx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C0:65AA
/* $21: store the tile X and Y of the object in operand 1 into variables
 * (operands 2 and 3). */
void event_cmd_get_object_tile(Snes *s)
{
    load_object_operand(s);
    store_tile(s);
}

// $C0:65EB
/* $22: store the tile X and Y of party member (operand 1) into variables
 * (operands 2 and 3). */
void event_cmd_get_party_member_tile(Snes *s)
{
    load_object_operand(s);
    m_tax(s);
    m_lda8(s, m_read8(s, (uint16_t)(s->cpu.d + DP_PARTY + s->cpu.x)));
    store_tile(s);
}

/* $C0:6600, A = slot: variable (operand 2) = the object's facing. */
static void store_facing(Snes *s)
{
    m_tax(s);
    m_write8(s, m_dp(s, 0xD9), m_lda8(s, object_byte(s, 0x1600)));
    store_var(s, m_read8(s, m_dp(s, 0xD9)));
    m_iny(s);
    m_tyx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C0:65F8
/* $23: store the facing of the object in operand 1 into variable (operand 2). */
void event_cmd_get_object_facing(Snes *s)
{
    load_object_operand(s);
    store_facing(s);
}

// $C0:661F
/* $24: store the facing of party member (operand 1) into variable (operand 2). */
void event_cmd_get_party_member_facing(Snes *s)
{
    load_object_operand(s);
    m_tax(s);
    m_lda8(s, m_read8(s, (uint16_t)(s->cpu.d + DP_PARTY + s->cpu.x)));
    store_facing(s);
}
