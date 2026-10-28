/* The gate's screen's song on the original's machine (game/menu/sound.c
 * decides which song and what to send).
 *
 * The original sends each command from one of two routines it calls: the
 * fade's ($C2:CBF7), and the song's start ($C2:CBEB), which runs on into
 * the fade's; the driver is called from inside them ($C2:CBF3, $C2:CC09),
 * so their returns are on the stack while it runs, and a bridge puts them
 * there. The waits for a fade are the menus' task switch, called out a
 * frame at a time, and the screen's own fade out waits too (menu/fades.c). The wait
 * for the gate's song to be playing spins on the sound processor's port
 * without letting a frame go by: it is an idle loop here
 * (menu_wait_for_menu_music_playing_loop), and the time it takes is the
 * driver's to start the song (docs/TODO.md). */
#include "menu/sound.h"

#include "menu/bridge.h"
#include "menu/fades.h"
#include "menu/menu.h"
#include "original_effects.h"
#include "platform/audio.h"
#include "port/machine.h"
#include "port/native.h"
#include "vars.h"

/* The driver, which reads the accumulator's high byte of the registers. */
enum { SOUND_DRIVER = 0xC70004, SEND_THE_SONG = 0xC2CBF3, SEND_THE_FADE = 0xC2CC09 };
/* The wait for the music, and the task's end ($C2:8216), which its exit jumps
 * to ($C2:CBE8). */
enum { MUSIC_PLAYING_WAIT = 0xC2CBE0, TASK_ENDS = 0xC28216 };
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

/* The task that starts the gate's song (menu_start_task). */
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

/* Where the original calls the fade's and the song's routines from. */
static const uint32_t SENT_FROM[] = {
    [GATE_SONG_FADED_OUT] = 0xC2CBB0, [GATE_SONG_STARTED] = 0xC2CBDD,
    [GATE_SONG_FADED_OUT_AGAIN] = 0xC2CC1D, [PLACE_SONG_STARTED_AGAIN] = 0xC2CC4B,
};
static const struct {
    uint32_t site;
} FADE_WAITS_AT[] = { [GATE_SONG_AT_THE_GATE_S_FADE] = { 0xC2CBC7 }, [GATE_SONG_AT_THE_PLACE_S_FADE] = { 0xC2CC2E } };

static bool sent_on_the_machine(void *ctx, GateSongSend at, AudioCommand c) { return send_fade(ctx, SENT_FROM[at], c); }

static bool song_and_fade_sent_on_the_machine(void *ctx, GateSongSend at, AudioCommand song, AudioCommand fade)
{
    return send_song_and_fade(ctx, SENT_FROM[at], song, fade);
}

static bool wait_on_the_machine(void *ctx, GateSongPlace at) { return menu_wait_a_frame_from(ctx, FADE_WAITS_AT[at].site); }

/* The waits for a fade ask with 8-bit A; the task starts with 16-bit
 * registers. */
static void registers_on_the_machine(void *ctx, GateSongPlace at)
{
    if (at == GATE_SONG_AT_THE_TASK_S_START)
        menu_set_widths(ctx, false, false);
    else
        menu_set_widths(ctx, true, false);
}

static bool screen_faded_out_on_the_machine(void *ctx) { return menu_fade_out_from(ctx, 0xC2CC20); }

/* The wait for the song to be playing is an idle loop of its own. */
static bool music_waited_for_on_the_machine(void *ctx)
{
    m_stop_at(ctx, MUSIC_PLAYING_WAIT, READS_NOTHING);
    return false;
}

static void decided_on_the_machine(void *ctx) { ORIGINAL_EFFECT(leave_song_decided(ctx)); }

static const GateSongSteps on_the_machine = {
    sent_on_the_machine, song_and_fade_sent_on_the_machine, wait_on_the_machine, registers_on_the_machine,
    screen_faded_out_on_the_machine, music_waited_for_on_the_machine, decided_on_the_machine,
};

// $C2:CB8B
/* The original comes back with 16-bit registers where it started the task. */
void menu_switch_to_the_gate_song_entry(Snes *s)
{
    if (menu_switch_to_the_gate_song_with(state_of(s), m_platform(s), &on_the_machine, s))
        m_rts(s);
}

// $C2:CBBF
void menu_play_the_gate_song_entry(Snes *s) { (void)menu_play_the_gate_song_with(state_of(s), m_platform(s), &on_the_machine, s); }

// $C2:CC0E
void menu_switch_back_to_the_field_song_entry(Snes *s)
{
    if (menu_switch_back_to_the_field_song_with(state_of(s), m_platform(s), &on_the_machine, s))
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
