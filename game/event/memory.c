/* Event commands: reading and writing memory -- through 24-bit pointers in
 * the script, and the byte-addressed area at $7F:0000 -- and copying blocks of
 * script data. */
#include "event/event.h"

#include "event/script.h"

#define MEMORY 0x7F0000u

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }

/* The 24-bit address in direct-page cells `cell`..`cell`+2 (long indirect). */
static uint32_t pointer_at(Snes *s, uint8_t cell)
{
    return m_read16(s, m_dp(s, cell)) | (uint32_t)dp8(s, (uint8_t)(cell + 2)) << 16;
}

/* $C0:67A2, called with JSR from `site`: store the pointer from operands 1-3
 * in $CD-$CF, save X (at the variable operand) in $D9, and leave X = that
 * variable's offset with M 16-bit. */
static void source_pointer_and_variable(Snes *s, uint16_t site)
{
    m_jsr_stack_bytes(s, (uint16_t)(site + 2));
    m_tyx(s);
    for (uint8_t cell = 0xCD; cell <= 0xCF; cell++) {
        m_inx(s);
        set_dp8(s, cell, m_lda8(s, script_byte(s, s->cpu.x)));
    }
    m_inx(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_write16(s, m_dp(s, 0xD9), s->cpu.x);
    m_rep(s, CPU_FLAG_M);
    s->cpu.a = m_asl16(s, m_lda16(s, s->cpu.a & 0x00FF));
    m_tax(s);
}

// $C0:6792
/* $48: variable (operand 4) = the byte at the pointer in operands 1-3. */
void event_cmd_var_from_pointer(Snes *s)
{
    source_pointer_and_variable(s, 0x6792);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, VARS + s->cpu.x, m_lda8(s, m_read8(s, pointer_at(s, 0xCD))));
    m_ldx(s, m_read16(s, m_dp(s, 0xD9)));
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C0:67C7
/* $49: 16-bit variable (operand 4) = the two bytes at the pointer in operands 1-3. */
void event_cmd_var_from_pointer16(Snes *s)
{
    source_pointer_and_variable(s, 0x67C7);
    m_write16(s, VARS + s->cpu.x, m_lda16(s, m_read16(s, pointer_at(s, 0xCD))));
    m_sep(s, CPU_FLAG_M);
    m_ldx(s, m_read16(s, m_dp(s, 0xD9)));
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* $C0:67E3, called with JSR from `site`: store the pointer from operands 1-3
 * in $D0-$D2 and leave X at the next operand. */
static void target_pointer(Snes *s, uint16_t site)
{
    m_jsr_stack_bytes(s, (uint16_t)(site + 2));
    m_tyx(s);
    for (uint8_t cell = 0xD0; cell <= 0xD2; cell++) {
        m_inx(s);
        set_dp8(s, cell, m_lda8(s, script_byte(s, s->cpu.x)));
    }
    m_inx(s);
}

// $C0:67D7
/* $4A: store operand 4 at the pointer in operands 1-3. */
void event_cmd_poke(Snes *s)
{
    target_pointer(s, 0x67D7);
    m_write8(s, pointer_at(s, 0xD0), m_lda8(s, script_byte(s, s->cpu.x)));
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C0:67FB
/* $4B: store operands 4-5 (16-bit) at the pointer in operands 1-3. */
void event_cmd_poke16(Snes *s)
{
    target_pointer(s, 0x67FB);
    m_rep(s, CPU_FLAG_M);
    m_write16(s, pointer_at(s, 0xD0), m_lda16(s, m_read16(s, SCRIPT + s->cpu.x)));
    m_sep(s, CPU_FLAG_M);
    m_inx(s);
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* After target_pointer: LDA script,X : STX $D9 : REP #$20 : AND #$00FF :
 * ASL A : TAX -- X = the variable offset; leaves M 16-bit. */
static void variable_for_poke(Snes *s)
{
    m_lda8(s, script_byte(s, s->cpu.x));
    m_write16(s, m_dp(s, 0xD9), s->cpu.x);
    m_rep(s, CPU_FLAG_M);
    s->cpu.a = m_asl16(s, m_lda16(s, s->cpu.a & 0x00FF));
    m_tax(s);
}

// $C0:680C
/* $4C: store variable (operand 4) at the pointer in operands 1-3. */
void event_cmd_poke_variable(Snes *s)
{
    target_pointer(s, 0x680C);
    variable_for_poke(s);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, pointer_at(s, 0xD0), m_lda8(s, m_read8(s, VARS + s->cpu.x)));
    m_ldx(s, m_read16(s, m_dp(s, 0xD9)));
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C0:6829
/* $4D: store 16-bit variable (operand 4) at the pointer in operands 1-3. */
void event_cmd_poke_variable16(Snes *s)
{
    target_pointer(s, 0x6829);
    variable_for_poke(s);
    m_write16(s, pointer_at(s, 0xD0), m_lda16(s, m_read16(s, VARS + s->cpu.x)));
    m_sep(s, CPU_FLAG_M);
    m_ldx(s, m_read16(s, m_dp(s, 0xD9)));
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C0:6846
/* $4E: copy the data that follows in the script into WRAM: operands 1-2 the
 * destination address, operand 3 its bank ($7E; anything else means $7F),
 * operands 4-5 a length L, then L - 2 bytes of data, copied with MVN.
 * Continues after the data. */
void event_cmd_copy_data(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    m_rep(s, CPU_FLAG_M);
    s->cpu.y = m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
    m_set_nz16(s, s->cpu.y);
    m_sep(s, CPU_FLAG_M);
    m_inx(s);
    m_inx(s);
    uint8_t bank = m_lda8(s, script_byte(s, s->cpu.x));
    m_cmp8(s, bank, 0x7E);
    uint8_t dest_bank = bank == 0x7E ? 0x7E : 0x7F;
    m_rep(s, CPU_FLAG_M);
    m_inx(s);
    uint16_t count = m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
    for (int i = 0; i < 3; i++)
        count = m_lda16(s, (uint16_t)(count - 1));
    m_write16(s, m_dp(s, 0xC1), count);
    m_inx(s);
    m_inx(s);
    m_txa(s);
    m_set_flag(s, CPU_FLAG_C, false);
    s->cpu.a = m_adc16(s, s->cpu.a, 0x2001);
    m_tax(s);
    m_lda16(s, m_read16(s, m_dp(s, 0xC1)));

    /* PHB : MVN : PLB */
    m_push8(s, s->cpu.db);
    s->cpu.db = dest_bank;
    do {
        m_write8(s, (uint32_t)dest_bank << 16 | s->cpu.y, m_read8(s, 0x7F0000u | s->cpu.x));
        s->cpu.x++;
        s->cpu.y++;
        s->cpu.a--;
    } while (s->cpu.a != 0xFFFF);
    s->cpu.db = m_pull8(s);
    m_set_nz8(s, s->cpu.db);

    m_txa(s);
    m_set_flag(s, CPU_FLAG_C, true);
    s->cpu.a = m_sbc16(s, s->cpu.a, 0x2001);
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C0:693D
/* $53: variable (operand 3) = the byte at $7F:0000 + operands 1-2. */
void event_cmd_var_from_memory(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
    m_tax(s);
    m_write16(s, m_dp(s, 0xD9), m_lda16(s, m_read16(s, MEMORY + s->cpu.x)));
    m_iny(s);
    m_iny(s);
    m_tyx(s);
    var_offset_here(s);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, VARS + s->cpu.x, m_lda8(s, dp8(s, 0xD9)));
    tyx_inx_sec_rts(s, 1);
}

// $C0:6964
/* $54: 16-bit variable (operand 3) = the two bytes at $7F:0000 + operands 1-2. */
void event_cmd_var_from_memory16(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
    m_tax(s);
    m_write16(s, m_dp(s, 0xD9), m_lda16(s, m_read16(s, MEMORY + s->cpu.x)));
    m_iny(s);
    m_iny(s);
    m_tyx(s);
    var_offset_here(s);
    m_write16(s, VARS + s->cpu.x, m_lda16(s, m_read16(s, m_dp(s, 0xD9))));
    m_sep(s, CPU_FLAG_M);
    tyx_inx_sec_rts(s, 1);
}

// $C0:698B
/* $55: variable (operand 1) = the byte at $7F:0000. */
void event_cmd_var_from_memory0(Snes *s)
{
    var_offset_next(s);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, VARS + s->cpu.x, m_lda8(s, m_read8(s, MEMORY)));
    tyx_inx_sec_rts(s, 1);
}

// $C0:69A6
/* $56: store operand 1 at $7F:0000 + operands 2-3. */
void event_cmd_memory_set(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    set_dp8(s, 0xD9, m_lda8(s, script_byte(s, s->cpu.x)));
    m_inx(s);
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, MEMORY + s->cpu.x, m_lda8(s, dp8(s, 0xD9)));
    m_iny(s);
    m_iny(s);
    iny_tyx_sec_rts(s);
}

/* Variable (operand 1) into $D9-$DA (16-bit), then X = operands 2-3; M 16-bit. */
static void variable_then_address(Snes *s)
{
    var_offset_next(s);
    m_write16(s, m_dp(s, 0xD9), m_lda16(s, m_read16(s, VARS + s->cpu.x)));
    m_iny(s);
    m_tyx(s);
    m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
    m_tax(s);
}

// $C0:69C4
/* $58: store variable (operand 1) at $7F:0000 + operands 2-3. */
void event_cmd_memory_set_from_var(Snes *s)
{
    variable_then_address(s);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, MEMORY + s->cpu.x, m_lda8(s, dp8(s, 0xD9)));
    m_iny(s);
    iny_tyx_sec_rts(s);
}

// $C0:69EB
/* $59: store 16-bit variable (operand 1) at $7F:0000 + operands 2-3. */
void event_cmd_memory_set_from_var16(Snes *s)
{
    variable_then_address(s);
    m_write16(s, MEMORY + s->cpu.x, m_lda16(s, m_read16(s, m_dp(s, 0xD9))));
    m_sep(s, CPU_FLAG_M);
    m_iny(s);
    iny_tyx_sec_rts(s);
}

// $C0:6A12
/* $5A: store operand 1 at $7F:0000. */
void event_cmd_memory0_set(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_write8(s, MEMORY, m_lda8(s, script_byte(s, s->cpu.x)));
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* ROL A (8-bit) */
static uint8_t rol8(Snes *s, uint8_t v)
{
    uint8_t r = (uint8_t)(v << 1 | (m_flag(s, CPU_FLAG_C) ? 1 : 0));
    m_set_flag(s, CPU_FLAG_C, v & 0x80);
    m_set_nz8(s, r);
    return r;
}

/* $C0:6B76 and $C0:6BA4: change a bit in the memory byte at $7F:0000 +
 * operand 2 (+ $100 when operand 1 has bit 7 set), with the mask for operand
 * 1's low nibble from `table`. */
static void memory_bit(Snes *s, uint16_t table, bool set)
{
    m_iny(s);
    m_tyx(s);
    m_tdc(s);
    m_xba(s);
    uint8_t op = m_lda8(s, script_byte(s, s->cpu.x));
    set_dp8(s, 0xDB, op);
    m_lda8(s, op & 0x0F);
    m_tax(s);
    set_dp8(s, 0xD9, m_lda8(s, m_read8(s, m_abs(s, table) + s->cpu.x)));
    uint8_t v = m_lda8(s, dp8(s, 0xDB));
    v = rol8(s, v);
    v = rol8(s, v);
    m_set_a8(s, v);
    m_lda8(s, v & 0x01);
    m_xba(s);
    m_iny(s);
    m_tyx(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_tax(s);
    uint8_t byte = m_read8(s, MEMORY + s->cpu.x), mask = dp8(s, 0xD9);
    m_write8(s, MEMORY + s->cpu.x, m_lda8(s, set ? (uint8_t)(byte | mask) : (uint8_t)(byte & mask)));
    iny_tyx_sec_rts(s);
}

// $C0:6B76
/* $65: set a bit in memory ($7F:0000 or $7F:0100 area; see memory_bit). */
void event_cmd_memory_set_bit(Snes *s) { memory_bit(s, 0xFF20, true); }

// $C0:6BA4
/* $66: clear a bit in memory. */
void event_cmd_memory_clear_bit(Snes *s) { memory_bit(s, 0xFF28, false); }
