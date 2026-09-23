/* Scripts and sound: a sound effect, a song, the music fading, a command of
 * the script's own to the sound driver, and waiting on the driver -- for the
 * music's fade to end, or for the song to reach a marker a scene keeps time
 * by.
 *
 * Commands to the driver go through the audio service (platform/audio.h),
 * which on the development system hands them to the driver's CPU half at
 * $C7:0004, the original's code; so the commands that send one are bridges,
 * which check what the driver left before going on. The waits only read the
 * driver's status, and are operations like any other. */
#include "event/bridge.h"

#include "platform/audio.h"

/* What the driver's command routine reads of the registers it is called
 * with: nothing (it takes the command from its buffer), and nothing reads X,
 * A's low byte or the flags after it returns either (checked against
 * liveness by tools/dontcare_check.py). A's high byte and Y it hands back as
 * it found them, and the command leaves them to its caller: the dispatch's,
 * and the original's parse position (event/leaves.h). */
#define DRIVER_IGNORES (DC_AL | DC_X | DC_N | DC_V | DC_Z | DC_C)

/* Y as the original's handler has it at the driver's call: where its parse
 * of the operands stopped, which the driver hands back and the command
 * leaves its caller (after the openings at map setup, the menus' dispatcher
 * at $C2:8C36 stores it). */
static void leave_parse_at_the_call(Snes *s, uint16_t y) { s->cpu.y = y; }

/* The original keeps where the command goes on in event_saved_position across
 * the driver's call, and reads it back itself. */
static void keep_resume_point_across(GameState *g, uint16_t at) { set_event_saved_position(g, at); }

/* Back from a call-out, which returns to `returns_to`: true if the callee
 * left the machine as the command runs (event_machine_back); otherwise the
 * port stops there, declaring it, and the original carries on. */
static bool back(Snes *s, uint32_t returns_to, bool x8)
{
    if (event_machine_back(s, x8))
        return true;
    m_stop_at_own(s, returns_to, 0);
    return false;
}

/* The script goes on at `resume`, the object's turn over for this frame. */
static void finish(Snes *s, uint16_t resume) { event_return(s, (EventStep){ resume, false }); }

// $C0:3C11
/* Play a sound effect, from the middle. */
void event_cmd_play_effect(Snes *s)
{
    uint16_t at = event_command_offset(s);
    uint16_t resume = (uint16_t)(at + 2);
    uint8_t effect = script_byte(state_of(s), (uint16_t)(at + 1));
    ORIGINAL_EFFECT(keep_resume_point_across(state_of(s), (uint16_t)(resume - 1)));
    ORIGINAL_EFFECT(leave_parse_at_the_call(s, (uint16_t)(at + 1)));
    au_play_effect(m_platform(s), PF_CALL_DC(0xC03C26, 0xC70004, DRIVER_IGNORES), effect, AU_CENTRE);
    if (back(s, 0xC03C2A, false))
        finish(s, resume);
}

// $C0:3C2F
/* Play a song, which the game remembers as the one playing. */
void event_cmd_play_song(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    uint16_t resume = (uint16_t)(at + 2);
    uint8_t song = script_byte(g, (uint16_t)(at + 1));
    set_current_song(g, song);
    ORIGINAL_EFFECT(keep_resume_point_across(g, (uint16_t)(resume - 1)));
    ORIGINAL_EFFECT(leave_parse_at_the_call(s, (uint16_t)(at + 1)));
    au_play_song(m_platform(s), PF_CALL_DC(0xC03C43, 0xC70004, DRIVER_IGNORES), song);
    if (back(s, 0xC03C47, false))
        finish(s, resume);
}

// $C0:3C4C
/* Fade the music to a volume over a number of steps, and bring both pairs
 * of effect voices to full volume at once. */
void event_cmd_fade_music(Snes *s)
{
    uint16_t at = event_command_offset(s);
    uint16_t resume = (uint16_t)(at + 3);
    uint8_t steps = script_byte(state_of(s), (uint16_t)(at + 1));
    uint8_t volume = script_byte(state_of(s), (uint16_t)(at + 2));
    Platform *pf = m_platform(s);
    ORIGINAL_EFFECT(keep_resume_point_across(state_of(s), (uint16_t)(resume - 1)));
    ORIGINAL_EFFECT(leave_parse_at_the_call(s, (uint16_t)(at + 1)));
    /* the original also leaves $FF as the third parameter, which the fade
     * doesn't read */
    au_command(pf, PF_CALL_DC(0xC03C69, 0xC70004, DRIVER_IGNORES),
               (AudioCommand){ .command = AU_FADE_MUSIC, .p1 = steps, .p2 = volume, .p3 = AU_FULL_VOLUME, .count = 3 });
    if (!back(s, 0xC03C6D, false))
        return;
    au_fade(pf, PF_CALL_DC(0xC03C7C, 0xC70004, DRIVER_IGNORES), AU_FADE_EFFECTS, 0, AU_FULL_VOLUME);
    if (!back(s, 0xC03C80, false))
        return;
    au_fade(pf, PF_CALL_DC(0xC03C8F, 0xC70004, DRIVER_IGNORES), AU_FADE_MORE_EFFECTS, 0, AU_FULL_VOLUME);
    if (back(s, 0xC03C93, false))
        finish(s, resume);
}

// $C0:3C98
/* Send the driver a command of the script's choosing, with two parameters:
 * whatever the language has no command for (a tempo slide, a mute). */
void event_cmd_send_sound_command(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    uint16_t resume = (uint16_t)(at + 4);
    AudioCommand c = { .command = script_byte(g, (uint16_t)(at + 1)), .p1 = script_byte(g, (uint16_t)(at + 2)),
                       .p2 = script_byte(g, (uint16_t)(at + 3)), .count = 2 };
    ORIGINAL_EFFECT(keep_resume_point_across(g, resume));
    ORIGINAL_EFFECT(leave_parse_at_the_call(s, (uint16_t)(at + 1)));
    au_command(m_platform(s), PF_CALL_DC(0xC03CB4, 0xC70004, DRIVER_IGNORES), c);
    if (back(s, 0xC03CB8, false))
        finish(s, resume);
}

// $C0:3CBC
/* Wait until the music has faded as far as it was told to. */
static EventFlow wait_for_music_fade(GameState *g, const EventArgs *a)
{
    (void)g;
    return au_music_fade_left(a->platform, PF_AT(0xC03CBD)) == 0 ? end_turn() : stay();
}

// $C0:3CC5
/* Wait until the song reaches the marker a scene keeps time by (the marker
 * back to zero). */
static EventFlow wait_for_song_marker(GameState *g, const EventArgs *a)
{
    (void)g;
    return au_song_marker(a->platform, PF_AT(0xC03CC6)) == 0 ? end_turn() : stay();
}

EVENT_OPCODES(event_sound_opcodes,
    { 0xE8, .bridge = event_cmd_play_effect, .length = 2, .routine = 0xC03C11 },
    { 0xEA, .bridge = event_cmd_play_song, .length = 2, .routine = 0xC03C2F },
    { 0xEB, .bridge = event_cmd_fade_music, .length = 3, .routine = 0xC03C4C },
    { 0xEC, .bridge = event_cmd_send_sound_command, .length = 4, .routine = 0xC03C98 },
    { 0xED, wait_for_music_fade, .length = 1, .routine = 0xC03CBC },
    { 0xEE, wait_for_song_marker, .length = 1, .routine = 0xC03CC5 });
