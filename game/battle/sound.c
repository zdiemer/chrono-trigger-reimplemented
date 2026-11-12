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

// $CD:3E44
/* The pause's turn: a paused battle is silenced, waits a frame at a time for
 * the pause to end (the vblank handler still reads Start), and carries on
 * with its sound. */
void battle_sit_out_the_pause(GameState *g, Platform *pf)
{
    if (!battle_paused(g))
        return;
    silence(g, pf, PF_CALL(0xCD3E54, 0xC70004), SILENCE_ON);
    do
        battle_sit_out_a_paused_frame(g, pf);
    while (battle_paused(g));
    silence(g, pf, PF_CALL(0xCD3E70, 0xC70004), SILENCE_OFF);
}

// $CD:3B5F
/* A sound a screen script asked for, played once, from the frame wait's
 * tail, panned where the script said. */
void battle_play_the_script_sound(GameState *g, Platform *pf)
{
    if (!battle_script_sound_request(g))
        return;
    set_battle_script_sound_request(g, 0);
    au_play_effect(pf, PF_CALL(0xCD3B7D, 0xC70004), battle_script_sound(g),
                   (uint8_t)battle_script_sound_pan(g));
}

// $CD:3B71
/* A sound effect played, panned where the caller says. */
void battle_play_a_sound(Platform *pf, uint8_t effect, uint8_t pan)
{
    au_play_effect(pf, PF_CALL(0xCD3B7D, 0xC70004), effect, pan);
}
