/* Event commands: changing location, starting battles, and a few party requests.
 *
 * A location is stored as a triple: a 16-bit value whose low 9 bits are the
 * location number (base+0), a 16-bit value (base+2), and a byte (base+4)
 * built from the first value's high byte: bits 1-4 shifted down, plus bit 7.
 * The field main loop acts on a change once bit 7 of $17 is set. */
#include "event/event.h"

#include "event/script.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }

/* X at the first operand: store the location triple at direct-page `base`. */
static void store_location(Snes *s, uint8_t base)
{
    m_rep(s, CPU_FLAG_M);
    uint16_t first = m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
    m_write16(s, m_dp(s, 0xD9), first);
    m_write16(s, m_dp(s, base), m_lda16(s, (uint16_t)(first & 0x01FF)));
    m_inx(s);
    m_inx(s);
    m_write16(s, m_dp(s, (uint8_t)(base + 2)), m_lda16(s, m_read16(s, SCRIPT + s->cpu.x)));
    m_sep(s, CPU_FLAG_M);
    uint8_t high = m_lda8(s, dp8(s, 0xDA));
    high = m_lsr8(s, high);
    set_dp8(s, (uint8_t)(base + 4), m_lda8(s, high & 0x0F));
    m_lda8(s, dp8(s, 0xDA) & 0x80);
    m_tsb8(s, m_dp(s, (uint8_t)(base + 4)));
}

/* LDA $17 : ORA #$80 : STA $17 : LDA #$0F : STA $19 -- start the change, at
 * full brightness. */
static void request_change(Snes *s)
{
    set_dp8(s, 0x17, m_lda8(s, (uint8_t)(dp8(s, 0x17) | 0x80)));
    set_dp8(s, 0x19, m_lda8(s, 0x0F));
}

/* LDA $17 : BMI stay -- true if a change is already pending (turn ended). */
static bool change_pending(Snes *s)
{
    if (!(m_lda8(s, dp8(s, 0x17)) & 0x80))
        return false;
    m_tyx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
    return true;
}

// $C0:3AB7
/* $E0: change location to operands 1-4, unless a change is pending. Ends the
 * turn on this command (the change takes over). */
void event_cmd_change_location(Snes *s)
{
    if (change_pending(s))
        return;
    m_tyx(s);
    m_inx(s);
    store_location(s, 0x12);
    request_change(s);
    m_tyx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:3B9E
/* $E2: change location to the one in variables (operand 1 16-bit, operands
 * 2-4 bytes into $14, $15, $16), unless a change is pending. */
void event_cmd_change_location_from_variables(Snes *s)
{
    if (change_pending(s))
        return;
    m_write16(s, m_dp(s, 0xC7), s->cpu.y);
    m_iny(s);
    m_tyx(s);
    var_offset_here(s);
    m_write16(s, m_dp(s, 0x12), m_lda16(s, m_read16(s, VARS + s->cpu.x)));
    for (uint8_t cell = 0x14; cell <= 0x16; cell++) {
        m_iny(s);
        m_tyx(s);
        if (cell == 0x14) {
            m_lda16(s, m_read16(s, SCRIPT + s->cpu.x));
            s->cpu.a = m_asl16(s, m_lda16(s, s->cpu.a & 0x00FF));
            m_tax(s);
        } else {
            var_offset_here(s);
        }
        m_sep(s, CPU_FLAG_M);
        set_dp8(s, cell, m_lda8(s, m_read8(s, VARS + s->cpu.x)));
    }
    request_change(s);
    m_ldx(s, m_read16(s, m_dp(s, 0xC7)));
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* The end of $DC/$DD: INX : INX : SEC : RTS */
static void skip_location(Snes *s)
{
    m_inx(s);
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C0:3AF1
/* $DC: store location (operands 1-4) at $0A-$0E and continue. */
void event_cmd_set_location_0a(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    store_location(s, 0x0A);
    skip_location(s);
}

// $C0:3B1B
/* $DD: store location (operands 1-4) at $00-$04 and continue. */
void event_cmd_set_location_00(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    store_location(s, 0x00);
    skip_location(s);
}

// $C0:3B45
/* $DE: set $1E to 1, then as $DD. */
void event_cmd_set_location_00_with_1e(Snes *s)
{
    set_dp8(s, 0x1E, m_lda8(s, 0x01));
    event_cmd_set_location_00(s);
}

/* $C0:3B4B: store location (operands 1-4) at $12-$16, then leave the rest --
 * waiting for vblank, blanking, swapping locations and restarting the field
 * engine at $C0:0000 with a fresh stack -- to the original at $C0:3B71. */
static void change_location_now(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    store_location(s, 0x12);
    m_stop_at(s, 0xC03B71, 0);
}

// $C0:3B4B
/* $E1: change location to operands 1-4 immediately (the previous location
 * moves to $05-$09). */
void event_cmd_change_location_now(Snes *s) { change_location_now(s); }

// $C0:3B98
/* $DF: set $1E to 1, then as $E1. */
void event_cmd_change_location_now_with_1e(Snes *s)
{
    set_dp8(s, 0x1E, m_lda8(s, 0x01));
    change_location_now(s);
}

// $C0:3A83
/* $D8: start a battle (seen in gato-battle.ctm: the first run starts the
 * fight, the second continues after it). Operands 1 and 2 go to $7E:2989 and
 * $7E:2A1F, bits 0 and 5 of $17 are set and $1F cleared, and the command
 * waits. It continues (skipping its operands) once bit 0 of $18 is set, which
 * it clears. Nothing happens while bit 0 of $17 is still set. */
void event_cmd_start_battle(Snes *s)
{
    m_lda8(s, dp8(s, 0x17));
    m_bit_imm8(s, 0x01);
    if ((uint8_t)s->cpu.a & 0x01) {
        m_set_flag(s, CPU_FLAG_C, false);
        m_tyx(s);
        m_rts(s);
        return;
    }
    m_lda8(s, dp8(s, 0x18));
    m_bit_imm8(s, 0x01);
    if ((uint8_t)s->cpu.a & 0x01) {
        m_lda8(s, 0x01);
        m_trb8(s, m_dp(s, 0x18));
        m_set_flag(s, CPU_FLAG_C, false);
        m_tyx(s);
        m_inx(s);
        m_inx(s);
        m_inx(s);
        m_rts(s);
        return;
    }
    m_tyx(s);
    m_inx(s);
    m_write8(s, 0x7E2989, m_lda8(s, script_byte(s, s->cpu.x)));
    m_inx(s);
    m_write8(s, 0x7E2A1F, m_lda8(s, script_byte(s, s->cpu.x)));
    set_dp8(s, 0x17, m_lda8(s, (uint8_t)(dp8(s, 0x17) | 0x21)));
    set_dp8(s, 0x1F, 0x00);
    m_set_flag(s, CPU_FLAG_C, false);
    m_tyx(s);
    m_rts(s);
}

// $C0:3A68
/* $D5: call $C2:8004 with A = $0A, Y = operand 1 and X = operand 2. */
void event_cmd_party_routine_0a(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    m_tdc(s);
    m_xba(s);
    m_lda8(s, script_byte(s, s->cpu.x));
    m_inx(s);
    m_set_index(s, &s->cpu.y, s->cpu.a);   /* TAY */
    m_lda8(s, script_byte(s, s->cpu.x));
    m_inx(s);
    m_write16(s, m_dp(s, 0xC7), s->cpu.x);
    m_tax(s);
    m_lda8(s, 0x0A);
    m_jsl(s, 0xC03A7C, 0xC28004);
    if (s->cpu.e || (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
        return;
    m_ldx(s, m_read16(s, m_dp(s, 0xC7)));
    m_rts(s);
}

// $C0:3A07
/* $D6: take character (operand 1) out of the active party: the first of the
 * party slots $97, $99, $9B holding an object whose $1101 byte matches gets
 * that object's state set to 3 and the slot (and its copy at $7E:2980+) set
 * to $80. */
void event_cmd_remove_from_party(Snes *s)
{
    m_tyx(s);
    m_inx(s);
    uint8_t character = m_lda8(s, script_byte(s, s->cpu.x));
    m_inx(s);
    m_write16(s, m_dp(s, 0xC7), s->cpu.x);
    set_dp8(s, 0xD9, character);
    static const uint8_t slots[3] = { 0x97, 0x99, 0x9B };
    for (int i = 0; i < 3; i++) {
        if (i < 2) {
            m_tdc(s);
            m_xba(s);
        }
        uint8_t slot = m_lda8(s, dp8(s, slots[i]));
        if (slot & 0x80)
            continue;
        m_tax(s);
        uint8_t id = m_lda8(s, m_read8(s, m_abs(s, 0x1101) + s->cpu.x));
        m_cmp8(s, id, dp8(s, 0xD9));
        if (id != dp8(s, 0xD9))
            continue;
        m_write8(s, m_abs(s, 0x1100) + s->cpu.x, m_lda8(s, 0x03));
        set_dp8(s, slots[i], m_lda8(s, 0x80));
        m_write8(s, 0x7E2980u + (uint32_t)i, 0x80);
        break;
    }
    m_ldx(s, m_read16(s, m_dp(s, 0xC7)));
    m_rts(s);
}
