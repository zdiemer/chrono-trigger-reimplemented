/* Event commands: sound. Commands for the sound driver go through the audio
 * service (platform/audio.h), which on the development system writes them to
 * $1E00-$1E03 and calls $C7:0004 (core/sound_driver.c). Registered with a guard
 * that the data bank reaches $1E00 and the APU ports the way the service does. */
#include "event/event.h"

#include "event/script.h"
#include "platform/audio.h"

/* The registry guard: absolute addressing reaches $00:1E00 and the APU ports. */
bool event_sound_bank_is_system(Snes *s) { return m_io_bank(s->cpu.db); }

/* True if the callee came back with 8-bit A and 16-bit X; otherwise the port
 * stops at the return address and the original carries on. */
static bool widths_ok(Snes *s) { return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M; }

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
    uint8_t effect = m_lda8(s, script_byte(s, s->cpu.x));
    m_lda8(s, 0x80);
    m_lda8(s, 0x18);
    au_command(m_platform(s), PF_CALL(0xC03C26, 0xC70004),
               (AudioCommand){ .command = 0x18, .p1 = effect, .p2 = 0x80, .count = 2 });
    if (widths_ok(s))
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
    m_lda8(s, 0x10);
    m_write16(s, m_dp(s, 0xC7), s->cpu.x);
    au_play_song(m_platform(s), PF_CALL(0xC03C43, 0xC70004), song);
    if (widths_ok(s))
        finish(s);
}

// $C0:3C4C
/* $EB: send driver command $81 with operands 1 and 2 (and $FF), then
 * commands $82 and $83 with $00, $FF. */
void event_cmd_sound_81_83(Snes *s)
{
    Platform *pf = m_platform(s);
    m_iny(s);
    m_tyx(s);
    uint8_t p1 = m_lda8(s, script_byte(s, s->cpu.x));
    m_inx(s);
    uint8_t p2 = m_lda8(s, script_byte(s, s->cpu.x));
    m_lda8(s, 0xFF);
    m_lda8(s, 0x81);
    m_write16(s, m_dp(s, 0xC7), s->cpu.x);
    au_command(pf, PF_CALL(0xC03C69, 0xC70004), (AudioCommand){ .command = 0x81, .p1 = p1, .p2 = p2, .p3 = 0xFF, .count = 3 });
    static const struct { uint32_t site; uint8_t command; } rest[] = { { 0xC03C7C, 0x82 }, { 0xC03C8F, 0x83 } };
    for (int i = 0; i < 2; i++) {
        if (!widths_ok(s) || !event_sound_bank_is_system(s))
            return;
        m_lda8(s, rest[i].command);
        au_command(pf, PF_CALL(rest[i].site, 0xC70004),
                   (AudioCommand){ .command = rest[i].command, .p1 = 0x00, .p2 = 0xFF, .count = 2 });
    }
    if (!widths_ok(s))
        return;
    finish(s);
}

// $C0:3C98
/* $EC: send a driver command with two parameters (operands 1-3). */
void event_cmd_sound_command(Snes *s)
{
    m_iny(s);
    m_tyx(s);
    uint8_t bytes[3];
    for (int i = 0; i < 3; i++) {
        if (i != 0)
            m_inx(s);
        bytes[i] = m_lda8(s, script_byte(s, s->cpu.x));
    }
    m_inx(s);
    m_write16(s, m_dp(s, 0xC7), s->cpu.x);
    au_command(m_platform(s), PF_CALL(0xC03CB4, 0xC70004),
               (AudioCommand){ .command = bytes[0], .p1 = bytes[1], .p2 = bytes[2], .count = 2 });
    if (!widths_ok(s))
        return;
    m_ldx(s, m_read16(s, m_dp(s, 0xC7)));
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:3CBC
/* $ED: wait until the driver's status on APU port 1 reads zero. */
void event_cmd_wait_apu_port1(Snes *s)
{
    m_tyx(s);
    if (m_lda8(s, au_status(m_platform(s), PF_AT(0xC03CBD), 1)) == 0)
        m_inx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C0:3CC5
/* $EE: wait until the low nibble of the driver's status on APU port 3 reads zero. */
void event_cmd_wait_apu_port3(Snes *s)
{
    m_tyx(s);
    uint8_t v = au_status(m_platform(s), PF_AT(0xC03CC6), 3);
    if (m_lda8(s, v & 0x0F) == 0)
        m_inx(s);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}
