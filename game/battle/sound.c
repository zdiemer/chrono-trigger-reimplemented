/* Battle: pausing the sound while the battle is paused, and loops that wait for
 * the sound driver's status on APU port 1, through the audio service
 * (platform/audio.h). */
#include "battle/sound.h"

#include "platform/audio.h"
#include "port/machine.h"

/* The registry guard for the pause poll: absolute addressing reaches the command
 * buffer at $00:1E00 (a bank with the low RAM mirror: $00-$3F, $80-$BF or $7E),
 * where the audio service writes it. */
bool battle_sound_bank_reaches_1e00(Snes *s) { return m_io_bank(s->cpu.db) || s->cpu.db == 0x7E; }

// $CD:3E49
/* In $CD:3E44, which the battle calls every frame and which returns at once
 * unless the battle is paused ($CD02 nonzero; that test stays in the original,
 * so the frequent call isn't an entry): driver command $F5 with $F5 (and $F5 in
 * $0407), then the original's loop at $CD:3E58 waits a frame at a time until
 * $CD02 clears; $CD:3E65 follows. The command is presumably what silences the
 * sound during the pause. */
void battle_sound_pause(Snes *s)
{
    m_lda8(s, 0xF5);
    m_write8(s, m_abs(s, 0x0407), 0xF5);
    au_command(m_platform(s), PF_CALL(0xCD3E54, 0xC70004),
               (AudioCommand){ .command = 0xF5, .p1 = 0xF5, .count = 1 });
    /* the call-out returned to $CD:3E58, the wait loop */
}

// $CD:3E65
/* The pause ended: clear $0407 and send driver command $F5 with $00, then RTS. */
void battle_sound_resume(Snes *s)
{
    m_write8(s, m_abs(s, 0x0407), 0x00);
    m_lda8(s, 0xF5);
    au_command(m_platform(s), PF_CALL(0xCD3E70, 0xC70004),
               (AudioCommand){ .command = 0xF5, .p1 = 0x00, .count = 1 });
    m_rts(s);
}

// $CD:042B
/* One pass of a loop that calls $CD:044A (at $CD:0428) until bit 6 of $2A1F is
 * set or APU port 1 reads zero: continues at $CD:0438 then, or at the call
 * otherwise. */
void battle_wait_sound_port1_or_2a1f(Snes *s)
{
    if (m_lda8(s, m_read8(s, m_abs(s, 0x2A1F)) & 0x40) != 0) {
        s->cpu.pc = 0x0438;
        return;
    }
    uint8_t v = m_lda8(s, au_status(m_platform(s), PF_AT(0xCD0432), 1));
    s->cpu.pc = v != 0 ? 0x0428 : 0x0438;
}

/* LDA $002141 : BNE loop -- one pass of a loop that calls $CD:3E7D (a frame)
 * until APU port 1 reads zero. */
static void wait_port1(Snes *s, uint32_t at, uint16_t loop, uint16_t exit)
{
    uint8_t v = m_lda8(s, au_status(m_platform(s), PF_AT(at), 1));
    s->cpu.pc = v != 0 ? loop : exit;
}

// $CD:0D9C
/* In $CD:0D93: continues at the frame call at $CD:0D99 while port 1 is
 * nonzero, else at $CD:0DA2. */
void battle_wait_sound_port1_0d9c(Snes *s) { wait_port1(s, 0xCD0D9C, 0x0D99, 0x0DA2); }

// $CD:0DB7
/* In $CD:0DB1: continues at the frame call at $CD:0DB4 while port 1 is
 * nonzero, else at $CD:0DBD. */
void battle_wait_sound_port1_0db7(Snes *s) { wait_port1(s, 0xCD0DB7, 0x0DB4, 0x0DBD); }
