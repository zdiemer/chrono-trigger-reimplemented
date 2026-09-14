/* Event commands: sound. Commands for the sound driver are written to
 * $1E00-$1E03 (command, then parameters) and sent with JSL $C7:0004, which
 * stays in the original for now. */
#include "event/event.h"

#include "event/script.h"

/* JSL, then false if the callee didn't come back with 8-bit A and 16-bit X
 * (the port stops at the return address and the original carries on). */
static bool jsl(Snes *s, uint32_t site, uint32_t target)
{
    m_jsl(s, site, target);
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M;
}

static void set_abs8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr), v); }

/* LDX $C7 : INX : CLC : RTS */
static void finish(Snes *s)
{
    m_ldx(s, m_read16(s, m_dp(s, 0xC7)));
    m_inx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:3C11
/* $E8: play sound effect (operand 1): driver command $18 with $80. */
void event_cmd_play_sound(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    m_write16(s, m_dp(s, 0xC7), s->cpu.y);
    set_abs8(s, 0x1E01, m_lda8(s, script_byte(s, s->cpu.x)));
    set_abs8(s, 0x1E02, m_lda8(s, 0x80));
    set_abs8(s, 0x1E00, m_lda8(s, 0x18));
    if (jsl(s, 0xC03C26, 0xC70004))
        finish(s);
}

// $C0:3C2F
/* $EA: play song (operand 1): driver command $10; the song is also kept in
 * $7E:29AE. */
void event_cmd_play_song(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    uint8_t song = m_lda8(s, script_byte(s, s->cpu.x));
    m_write8(s, 0x7E29AE, song);
    set_abs8(s, 0x1E01, song);
    set_abs8(s, 0x1E00, m_lda8(s, 0x10));
    m_write16(s, m_dp(s, 0xC7), s->cpu.x);
    if (jsl(s, 0xC03C43, 0xC70004))
        finish(s);
}

// $C0:3C4C
/* $EB: send driver command $81 with operands 1 and 2 (and $FF), then
 * commands $82 and $83 with $00, $FF. */
void event_cmd_sound_81_83(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    set_abs8(s, 0x1E01, m_lda8(s, script_byte(s, s->cpu.x)));
    m_inx(s);
    set_abs8(s, 0x1E02, m_lda8(s, script_byte(s, s->cpu.x)));
    set_abs8(s, 0x1E03, m_lda8(s, 0xFF));
    set_abs8(s, 0x1E00, m_lda8(s, 0x81));
    m_write16(s, m_dp(s, 0xC7), s->cpu.x);
    if (!jsl(s, 0xC03C69, 0xC70004))
        return;
    static const struct { uint32_t site; uint8_t command; } rest[] = { { 0xC03C7C, 0x82 }, { 0xC03C8F, 0x83 } };
    for (int i = 0; i < 2; i++) {
        set_abs8(s, 0x1E01, m_lda8(s, 0x00));
        set_abs8(s, 0x1E02, m_lda8(s, 0xFF));
        set_abs8(s, 0x1E00, m_lda8(s, rest[i].command));
        if (!jsl(s, rest[i].site, 0xC70004))
            return;
    }
    finish(s);
}

// $C0:3C98
/* $EC: send a driver command with two parameters (operands 1-3). */
void event_cmd_sound_command(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    for (uint16_t addr = 0x1E00; addr <= 0x1E02; addr++) {
        if (addr != 0x1E00)
            m_inx(s);
        set_abs8(s, addr, m_lda8(s, script_byte(s, s->cpu.x)));
    }
    m_inx(s);
    m_write16(s, m_dp(s, 0xC7), s->cpu.x);
    if (!jsl(s, 0xC03CB4, 0xC70004))
        return;
    m_ldx(s, m_read16(s, m_dp(s, 0xC7)));
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:3CBC
/* $ED: wait until APU port 1 ($2141) reads zero. */
void event_cmd_wait_apu_port1(Snes *s)
{
    m_tyx(s);
    if (m_lda8(s, m_read8(s, m_abs(s, 0x2141))) == 0)
        m_inx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:3CC5
/* $EE: wait until the low nibble of APU port 3 ($2143) reads zero. */
void event_cmd_wait_apu_port3(Snes *s)
{
    m_tyx(s);
    uint8_t v = m_read8(s, m_abs(s, 0x2143));
    if (m_lda8(s, v & 0x0F) == 0)
        m_inx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}
