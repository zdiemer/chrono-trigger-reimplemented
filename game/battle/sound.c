/* Battle: the pause, and the waits for the music's fade.
 *
 * Pausing the battle (Start, battle/nmi.c) holds the fight: the battle's waits
 * give the pause a turn on every pass (battle/wait.c), and while it is paused
 * the game sits it out there a frame at a time. It silences the battle's sound
 * -- the sound driver is told to (command $F5), and told again to carry on
 * when the pause ends -- and stops the play time clock. Here too are the
 * sounds the battle plays from its frame work: a screen script's, and the
 * battle window's. As the battle ends, it has the driver fade the music out
 * and waits for the fade to run out (battle/closing.c; the driver counts its
 * steps left on its status, platform/audio.h). */
#include "battle/sound.h"

#include "battle/bridge.h"
#include "battle/wait.h"
#include "charge.h"
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

// $CD:3B71
/* A sound effect played, panned where the caller says. */
static void play_a_sound(Platform *pf, uint8_t effect, uint8_t pan)
{
    au_play_effect(pf, PF_CALL_DC(0xCD3B7D, 0xC70004, DRIVER_IGNORES), effect, pan);
}

/* The effect comes in A and its pan in X; the pan is left in A, as the
 * script's sound leaves it. */
void battle_play_a_sound(Snes *s)
{
    uint8_t pan = (uint8_t)s->cpu.x;
    play_a_sound(m_platform(s), (uint8_t)s->cpu.a, pan);
    m_set_a8(s, pan);
    m_rts(s);
}

bool battle_play_a_sound_from(Snes *s, uint32_t site, uint8_t effect, uint8_t pan, BattleFrame kept)
{
    battle_keep(s, kept);
    s->cpu.x = pan;
    if (!battle_on_page(s) || !battle_binary_m8_x16(s)) {
        m_set_a8(s, effect);
        if (!battle_call(s, site, 0xCD3B71, 0))
            return false;
    } else {
        battle_push_the_return(s, site);
        TIMED_AS(state_of(s), 0xCD3B71, play_a_sound(m_platform(s), effect, pan));
        battle_pull_the_return(s);
        m_set_a8(s, pan);
    }
    battle_stop_keeping(s, kept);
    return true;
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

/* One pass of the wait for the fade to run out before a defeat's song on the
 * screen effects' battlefield (in $CD:0D93, the original's still; the others
 * are battle/closing.c's): on to the song once it has, else back to the
 * loop's call of a frame's wait. The status tested is left in A. */
// $CD:0D9C
void battle_wait_music_fade_loop(Snes *s)
{
    uint8_t left = au_music_fade_left(m_platform(s), PF_AT(0xCD0D9C));
    m_set_a8(s, left);
    m_set_nz8(s, left);
    s->cpu.pc = left != 0 ? 0x0D99 : 0x0DA2;
}
