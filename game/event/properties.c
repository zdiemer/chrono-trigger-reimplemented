/* Event commands: setting object properties -- sprite display, position and
 * sprite priority. */
#include "event/event.h"

#include "event/script.h"

static void set8(Snes *s, uint16_t table, uint8_t v) { m_write8(s, m_abs(s, table) + s->cpu.x, v); }
static uint8_t get8(Snes *s, uint16_t table) { return m_read8(s, m_abs(s, table) + s->cpu.x); }

/* TYX : INX : SEC : RTS */
static void next1(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* INY : TYX : LDA script,X : REP #$20 : AND #$00FF : ASL A : TAX : SEP #$20 :
 * LDA $7F0200,X -- the variable named by the next operand. */
static uint8_t load_variable(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_rep(s, CPU_FLAG_M);
    s->cpu.a = m_asl16(s, m_lda16(s, s->cpu.a & 0x00FF));
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    return m_lda8(s, m_read8(s, VARS + s->cpu.x));
}

/* INY : TYX : LDA script,X */
static uint8_t load_operand(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    return m_lda8(s, script_byte(s, s->cpu.x));
}

// $C0:4B49
/* $89: set this object's $1A00 byte to operand 1. */
void event_cmd_set_1a00(Snes *s)
{
    uint8_t v = load_operand(s);
    ldx_current_object(s);
    set8(s, 0x1A00, v);
    next1(s);
}

// $C0:4B58
/* $8A: set this object's $1A00 byte from variable (operand 1). */
void event_cmd_set_1a00_from_variable(Snes *s)
{
    uint8_t v = load_variable(s);
    ldx_current_object(s);
    set8(s, 0x1A00, v);
    next1(s);
}

// $C0:4867
/* $84: set this object's $1B01 byte to operand 1. */
void event_cmd_set_1b01(Snes *s)
{
    uint8_t v = load_operand(s);
    ldx_current_object(s);
    set8(s, 0x1B01, v);
    iny_tyx_sec_rts(s);
}

// $C0:4876
/* $87: set this object's $1000 and $1001 bytes to operand 1 + 1, keeping bit
 * 7 of $1000 (the flag calls check). $1000 is also the step count moves start
 * with. */
void event_cmd_set_step_count(Snes *s)
{
    uint8_t v = load_operand(s);
    ldx_current_object(s);
    v = m_lda8(s, (uint8_t)(v + 1));
    m_write8(s, m_dp(s, 0xD9), v);
    uint8_t flag = m_lda8(s, get8(s, 0x1000) & 0x80);
    v = m_lda8(s, (uint8_t)(flag | m_read8(s, m_dp(s, 0xD9))));
    set8(s, 0x1000, v);
    set8(s, 0x1001, v);
    next1(s);
}

/* With X = tile index (Y << 8 | X) into the map attribute tables at $7E:7000
 * and $7E:7040: bit 6 of each picks sprite priority $30 over $20, for $0C00,X
 * (from $7E:7040) and $0C01,X (from $7E:7000) of this object. */
static void priority_from_map(Snes *s)
{
    uint8_t v = m_lda8(s, m_read8(s, 0x7E7000u + s->cpu.x));
    v = m_lda8(s, v & 0x40);
    v = m_lsr8(s, v);
    v = m_lsr8(s, v);
    v = m_lda8(s, v | 0x20);
    m_write8(s, m_dp(s, 0xD9), v);
    v = m_lda8(s, m_read8(s, 0x7E7040u + s->cpu.x));
    v = m_lda8(s, v & 0x40);
    v = m_lsr8(s, v);
    v = m_lsr8(s, v);
    v = m_lda8(s, v | 0x20);
    ldx_current_object(s);
    set8(s, 0x0C00, v);
    set8(s, 0x0C01, m_lda8(s, m_read8(s, m_dp(s, 0xD9))));
}

/* X = this object, A = tile Y: finish setting a tile position. */
static void finish_tile_position(Snes *s, uint8_t tile_y)
{
    set8(s, 0x1881, tile_y);
    m_write8(s, m_dp(s, 0xDA), tile_y);
    set8(s, 0x1880, m_lda8(s, 0xFF));
    if (m_lda8(s, get8(s, 0x0F80)) & 0x80) {
        m_ldx(s, m_read16(s, m_dp(s, 0xD9)));
        priority_from_map(s);
    }
    next1(s);
}

// $C0:4B74
/* $8B: place this object on the tile at operands (X, Y), and if its priority
 * follows the map ($0F80,X negative), take the priority from the map there. */
void event_cmd_set_tile_position(Snes *s)
{
    uint8_t x = load_operand(s);
    ldx_current_object(s);
    set8(s, 0x1801, x);
    m_write8(s, m_dp(s, 0xD9), x);
    set8(s, 0x1800, m_lda8(s, 0x80));
    uint8_t y = load_operand(s);
    ldx_current_object(s);
    finish_tile_position(s, y);
}

// $C0:4BC3
/* $8C: as $8B with the tile from variables (operands 1 and 2). */
void event_cmd_set_tile_position_from_variables(Snes *s)
{
    uint8_t x = load_variable(s);
    ldx_current_object(s);
    set8(s, 0x1801, x);
    m_write8(s, m_dp(s, 0xD9), x);
    uint8_t y = load_variable(s);
    ldx_current_object(s);
    finish_tile_position(s, y);
}

/* LDA $1881,X : XBA : LDA $1801,X : TAX -- X = this object's tile index. */
static void load_tile_index(Snes *s)
{
    m_lda8(s, get8(s, 0x1881));
    m_xba(s);
    m_lda8(s, get8(s, 0x1801));
    m_tax(s);
}

// $C0:4C27
/* $8D: set this object's 16-bit X and Y positions ($1800, $1880) from
 * operands 1-2 and 3-4, then take priority from the map as $8B does. */
void event_cmd_set_position(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_rep(s, CPU_FLAG_M);
    uint16_t x = m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
    ldx_current_object(s);
    m_write16(s, m_abs(s, 0x1800) + s->cpu.x, x);
    m_iny(s);
    m_iny(s);
    m_tyx(s);
    uint16_t y = m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
    ldx_current_object(s);
    m_write16(s, m_abs(s, 0x1880) + s->cpu.x, y);
    m_sep(s, CPU_FLAG_M);
    if (m_lda8(s, get8(s, 0x0F80)) & 0x80) {
        load_tile_index(s);
        priority_from_map(s);
    }
    m_tyx(s);
    m_inx(s);
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C0:4C74
/* $8E: set this object's sprite priority mode ($0F80,X) to operand 1. With bit 7
 * set, priority comes from the map at its tile; otherwise $0C01,X gets the
 * operand's bits 4-5 and $0C00,X its bits 0-1 (shifted up to bits 4-5). Either
 * way $0F01,X = $FF and $1601,X = 0. */
void event_cmd_set_sprite_priority(Snes *s)
{
    uint8_t mode = load_operand(s);
    ldx_current_object(s);
    set8(s, 0x0F80, mode);
    m_bit_imm8(s, 0x80);
    if (mode & 0x80) {
        load_tile_index(s);
        priority_from_map(s);
    } else {
        m_write8(s, m_dp(s, 0xD9), mode);
        set8(s, 0x0C01, m_lda8(s, mode & 0x30));
        uint8_t v = m_lda8(s, m_read8(s, m_dp(s, 0xD9)));
        for (int i = 0; i < 4; i++)
            v = m_asl8(s, v);
        set8(s, 0x0C00, m_lda8(s, v & 0x30));
    }
    set8(s, 0x0F01, m_lda8(s, 0xFF));
    set8(s, 0x1601, 0x00);
    next1(s);
}

/* $C0:4CD9, A = value, X = slot: STA $1A81,X : TYX : INX : CLC : RTS. $1A81,X
 * is the sprite flag field_update_object_visibility checks (zero or negative:
 * no sprite). */
static void set_sprite_flag(Snes *s)
{
    set8(s, 0x1A81, (uint8_t)s->cpu.a);
    m_tyx(s);
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

static void own_sprite_flag(Snes *s, uint8_t v)
{
    ldx_current_object(s);
    m_lda8(s, v);
    set_sprite_flag(s);
}

static void object_sprite_flag(Snes *s, uint8_t v)
{
    m_iny(s);
    m_tyx(s);
    m_tdc(s);
    m_xba(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_tax(s);
    m_lda8(s, v);
    set_sprite_flag(s);
}

// $C0:4CD5
/* $90: give this object a sprite ($1A81,X = 1). */
void event_cmd_show_sprite(Snes *s) { own_sprite_flag(s, 0x01); }

// $C0:4CE0
/* $91: remove this object's sprite ($1A81,X = 0). */
void event_cmd_hide_sprite(Snes *s) { own_sprite_flag(s, 0x00); }

// $C0:4CE6
/* $7E: set this object's $1A81,X to $80 (no sprite). */
void event_cmd_set_sprite_flag_80(Snes *s) { own_sprite_flag(s, 0x80); }

// $C0:4CEC
/* $7C: give the object in operand 1 a sprite. */
void event_cmd_show_object_sprite(Snes *s) { object_sprite_flag(s, 0x01); }

// $C0:4CF9
/* $7D: remove the sprite of the object in operand 1. */
void event_cmd_hide_object_sprite(Snes *s) { object_sprite_flag(s, 0x00); }
