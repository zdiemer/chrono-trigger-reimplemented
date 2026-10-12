/* Battle: the pause, and the waits for the music's fade.
 *
 * Pausing the battle (Start, battle/nmi.c) holds the fight: the battle's waits
 * give the pause a turn on every pass (battle/wait.c), and while it is paused
 * the game sits it out there a frame at a time. It silences the battle's sound
 * -- the sound driver is told to (command $F5), and told again to carry on
 * when the pause ends -- and stops the play time clock. And before the battle changes the music -- at its start, for a screen
 * effect's own song -- it has the driver fade the song out and waits, a frame
 * at a time, for the fade to run out (the driver counts its steps left on its
 * status, platform/audio.h); a battle an event starts with the field's song
 * kept doesn't wait at its start. The waits are passes of the original's
 * loops, which call a frame's wait between them. */
#include "battle/sound.h"

#include "battle/bridge.h"
#include "battle/wait.h"
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

/* The sound driver's command reads none of the registers. */
enum { DRIVER_IGNORES = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

// $CD:3E44
/* The pause's turn: a paused battle is silenced, waits a frame at a time for
 * the pause to end (the vblank handler still reads Start), and carries on
 * with its sound. */
static void sit_out_the_pause(GameState *g, Platform *pf)
{
    if (!battle_paused(g))
        return;
    silence(g, pf, PF_CALL_DC(0xCD3E54, 0xC70004, DRIVER_IGNORES), SILENCE_ON);
    do
        battle_sit_out_a_paused_frame(g, pf);
    while (battle_paused(g));
    silence(g, pf, PF_CALL_DC(0xCD3E70, 0xC70004, DRIVER_IGNORES), SILENCE_OFF);
}

void battle_sit_out_the_pause(Snes *s)
{
    sit_out_the_pause(state_of(s), m_platform(s));
    m_rts(s);
}

// $CD:0036
/* The pause's turn from another bank: a call of the same. */
void battle_sit_out_the_pause_long(Snes *s)
{
    if (!battle_run(s, 0xCD04A6, 0xCD3E44, battle_sit_out_the_pause))
        return;
    m_rtl(s);
}

// $CD:3B5F
/* A sound a screen script asked for, played once, from the frame wait's
 * tail, panned where the script said. */
static void play_the_script_sound(GameState *g, Platform *pf)
{
    if (!battle_script_sound_request(g))
        return;
    set_battle_script_sound_request(g, 0);
    au_play_effect(pf, PF_CALL_DC(0xCD3B7D, 0xC70004, DRIVER_IGNORES), battle_script_sound(g),
                   (uint8_t)battle_script_sound_pan(g));
}

/* A sound played leaves its pan in X, and its low byte in A; none asked for,
 * A clear. */
static void leave_the_pan(Snes *s, const GameState *g, bool played)
{
    if (!played) {
        m_set_a8(s, 0);
        return;
    }
    s->cpu.x = battle_script_sound_pan(g);
    m_set_a8(s, (uint8_t)battle_script_sound_pan(g));
}

void battle_play_the_script_sound(Snes *s)
{
    GameState *g = state_of(s);
    bool asked = battle_script_sound_request(g) != 0;
    play_the_script_sound(g, m_platform(s));
    leave_the_pan(s, g, asked);
    m_rts(s);
}

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
