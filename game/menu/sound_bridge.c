/* The gate's screen's song on the original's machine (game/menu/sound.c
 * decides which song and what to send).
 *
 * The original sends each command from one of two routines it calls: the
 * fade's ($C2:CBF7), and the song's start ($C2:CBEB), which runs on into
 * the fade's; the driver is called from inside them ($C2:CBF3, $C2:CC09),
 * so their returns are on the stack while it runs, and a bridge puts them
 * there. The waits for a fade are the menus' task switch, called out a
 * frame at a time; the screen's own fade out is called out too. The wait
 * for the gate's song to be playing spins on the sound processor's port
 * without letting a frame go by: it is an idle loop here
 * (menu_wait_for_menu_music_playing_loop), and the time it takes is the
 * driver's to start the song (docs/TODO.md). */
#include "menu/sound.h"

#include "menu/bridge.h"
#include "menu/menu.h"
#include "original_effects.h"
#include "platform/audio.h"
#include "port/machine.h"
#include "port/native.h"
#include "vars.h"

/* The driver, which reads the accumulator's high byte of the registers, and
 * the screen's fade out, which reads the same. */
enum { SOUND_DRIVER = 0xC70004, SEND_THE_SONG = 0xC2CBF3, SEND_THE_FADE = 0xC2CC09 };
enum { FADE_OUT = 0xC283CA, MUSIC_PLAYING_WAIT = 0xC2CBE0, TASK_ENDS = 0xC2CBE8 };
enum {
    READS_A_HIGH = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
};

static bool send_at(Snes *s, uint32_t jsl, AudioCommand c)
{
    MenuMachine m = menu_machine_now(s);
    au_command(m_platform(s), PF_CALL_DC(jsl, SOUND_DRIVER, READS_A_HIGH), c);
    return menu_back(s, jsl + MENU_JSL_SIZE, m);
}

/* For the original's code and shadow mode: the steps and the volume of a
 * fade, which it hands its sending in the menus' first cells. */
static void leave_fade(GameState *g, AudioCommand fade)
{
    set_menu_scratch_song_steps(g, fade.p1);
    set_menu_scratch_song_volume(g, fade.p2);
}

/* A fade sent from the fade's routine, called at `call`. */
static bool send_fade(Snes *s, uint32_t call, AudioCommand fade)
{
    ORIGINAL_EFFECT(leave_fade(state_of(s), fade));
    m_push16(s, (uint16_t)(call + MENU_JSR_SIZE - 1));
    if (!send_at(s, SEND_THE_FADE, fade))
        return false;
    (void)m_pull16(s);
    return true;
}

/* A song's start and the fade after it, from the start's routine, called
 * at `call`. */
static bool send_song_and_fade(Snes *s, uint32_t call, AudioCommand song, AudioCommand fade)
{
    ORIGINAL_EFFECT(leave_fade(state_of(s), fade));
    m_push16(s, (uint16_t)(call + MENU_JSR_SIZE - 1));
    if (!send_at(s, SEND_THE_SONG, song) || !send_at(s, SEND_THE_FADE, fade))
        return false;
    (void)m_pull16(s);
    return true;
}

/* A wait for the music's fade to run out, a frame at a time: the driver
 * counts the steps it has left. */
static bool wait_for_the_fade(Snes *s, uint32_t asked_at, uint32_t yield_at)
{
    for (;;) {
        menu_set_widths(s, true, false);
        if (au_music_fade_left(m_platform(s), PF_AT(asked_at)) == 0)
            return true;
        if (!menu_wait_a_frame_from(s, yield_at))
            return false;
    }
}

/* The task that starts the gate's song (menu_start_task). */
enum { PLAY_THE_GATE_SONG = 0xCBBF };

/* For the original's code and shadow mode: the flags its tests leave, which
 * its caller keeps on the stack as it draws the panel -- the song compared
 * with the gate's where that is the one playing, otherwise a load of
 * something other than zero (the story's note, or the task's slot). */
static void leave_song_decided(Snes *s)
{
    const GameState *g = state_of(s);
    bool already = !(story_game_notes(g) & MENU_PLACE_SONG_PLAYS_ON) && menu_exchange_song_before(g) == MENU_GATE_SONG;
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, already);
    if (already)
        m_set_flag(s, CPU_FLAG_C, true);
}

// $C2:CB8B
/* The place's song faded out, and the task started that brings the gate's
 * in; the original comes back with 16-bit registers where it started it. */
void menu_switch_to_the_gate_song(Snes *s)
{
    GameState *g = state_of(s);
    if (menu_gate_song_wanted(g)) {
        if (!send_fade(s, 0xC2CBB0, menu_fade_the_song_out()))
            return;
        menu_set_widths(s, false, false);
        menu_start_task(g, MENU_DESCRIPTION_TASK, PLAY_THE_GATE_SONG);
    }
    ORIGINAL_EFFECT(leave_song_decided(s));
    m_rts(s);
}

// $C2:CBBF
/* The task: once the fade has run out, the gate's song, its volume brought
 * up at once; then the wait for it to be playing, which ends the task. */
void menu_play_the_gate_song(Snes *s)
{
    if (!wait_for_the_fade(s, 0xC2CBC1, 0xC2CBC7)
        || !send_song_and_fade(s, 0xC2CBDD, menu_start_the_gate_song(), menu_bring_the_song_up()))
        return;
    m_stop_at(s, MUSIC_PLAYING_WAIT, READS_NOTHING);
}

// $C2:CC0E
/* The gate's song faded out with the screen, and the place's back from
 * where the driver saved it, fading in. */
void menu_switch_back_to_the_field_song(Snes *s)
{
    GameState *g = state_of(s);
    bool played = menu_gate_song_played(g);
    if (played && !send_fade(s, 0xC2CC1D, menu_fade_the_song_out()))
        return;
    if (!menu_call(s, 0xC2CC20, FADE_OUT, READS_A_HIGH))
        return;
    if (played
        && (!wait_for_the_fade(s, 0xC2CC28, 0xC2CC2E)
            || !send_song_and_fade(s, 0xC2CC4B, menu_start_the_place_song_again(g), menu_fade_the_place_song_in(g))))
        return;
    m_rts(s);
}

// $C2:CBE0
/* After starting the gate's song, the wait for its music to be playing
 * (the driver's status names the tracks that are), then on to the task's
 * end, where nothing reads what the loop loaded. The original spins on the
 * sound processor's port here without letting a frame go by, so how long it
 * waits is how long the driver takes to start the song: a wait on the
 * sound processor's timing (docs/TODO.md). Idle entry, one pass. */
void menu_wait_for_menu_music_playing_loop(Snes *s)
{
    m_stop_at(s, au_music_playing(m_platform(s), PF_AT(0xC2CBE0)) != 0 ? TASK_ENDS : MUSIC_PLAYING_WAIT, 0);
}
