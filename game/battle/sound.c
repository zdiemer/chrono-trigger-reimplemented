/* Battle: the sound while the battle is paused, and the waits for the music's
 * fade.
 *
 * Pausing the battle (Start, battle/nmi.c) silences its sound -- the sound
 * driver is told to (command $F5), and told again to carry on when the pause
 * ends -- and stops the play time clock. And before the battle changes the music -- at its start, for a screen
 * effect's own song -- it has the driver fade the song out and waits, a frame
 * at a time, for the fade to run out (the driver counts its steps left on its
 * status, platform/audio.h); a battle an event starts with the field's song
 * kept doesn't wait at its start. The waits are passes of the original's
 * loops, which call a frame's wait between them. */
#include "battle/sound.h"

#include "battle/bridge.h"
#include "platform/audio.h"
#include "vars.h"

/* The driver's command that silences the battle, with the pause's on or off
 * as its operand; the play time clock stops with the same byte. */
enum { SILENCE = 0xF5, SILENCE_ON = 0xF5, SILENCE_OFF = 0x00 };

static void silence(GameState *g, Platform *pf, PfOrigin o, uint8_t on)
{
    set_play_time_stopped(g, on);
    au_command(pf, o, (AudioCommand){ .command = SILENCE, .p1 = on, .count = 1 });
}

// $CD:3E49
/* The battle is paused: silence it, then on to the original's loop at
 * $CD:3E58, which waits a frame at a time for the pause to end. (The test of
 * the pause before it, in $CD:3E44, stays the original's, since the battle
 * calls it every frame.) */
void battle_sound_pause(Snes *s)
{
    m_set_a8(s, SILENCE_ON);
    silence(state_of(s), m_platform(s), PF_CALL(0xCD3E54, 0xC70004), SILENCE_ON);
}

// $CD:3E65
/* The pause ended: let the sound carry on. */
void battle_sound_resume(Snes *s)
{
    m_set_a8(s, SILENCE);
    silence(state_of(s), m_platform(s), PF_CALL(0xCD3E70, 0xC70004), SILENCE_OFF);
    m_rts(s);
}

/* The driver's commands are built in its buffer at $1E00, which the data
 * bank must reach (low RAM's banks, or $7E). */
bool battle_sound_buffer_reachable(Snes *s) { return m_bank_reaches_low_ram(s); }

/* ---- The fade's waits ------------------------------------------------------------------ */

/* One pass of a wait for the fade to run out: on to `done` once it has (or
 * `anyway`), else back to `wait`, the loop's call of a frame's wait. The
 * status tested is left in A. */
static void wait_for_fade(Snes *s, PfOrigin o, bool anyway, uint16_t wait, uint16_t done)
{
    if (anyway) {
        s->cpu.pc = done;
        return;
    }
    uint8_t left = au_music_fade_left(m_platform(s), o);
    m_set_a8(s, left);
    m_set_nz8(s, left);
    s->cpu.pc = left != 0 ? wait : done;
}

/* An event can start a battle with the field's song kept. */
enum { KEEPS_FIELD_SONG = 0x40 };

// $CD:042B (in $CD:0418, the start's)
void battle_wait_start_music_fade_loop(Snes *s)
{
    bool kept = (battle_event_flags(state_of(s)) & KEEPS_FIELD_SONG) != 0;
    if (kept) {
        m_set_a8(s, KEEPS_FIELD_SONG);
        m_set_nz8(s, KEEPS_FIELD_SONG);
    }
    wait_for_fade(s, PF_AT(0xCD0432), kept, 0x0428, 0x0438);
}

// $CD:0D9C (in $CD:0D93, before a song of the screen effects')
void battle_wait_music_fade_loop(Snes *s) { wait_for_fade(s, PF_AT(0xCD0D9C), false, 0x0D99, 0x0DA2); }

// $CD:0DB7 (in $CD:0DB1, before the HP gradient's)
void battle_wait_music_fade_for_gradient_loop(Snes *s)
{
    wait_for_fade(s, PF_AT(0xCD0DB7), false, 0x0DB4, 0x0DBD);
}
