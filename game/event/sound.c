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
#include "event/engine.h"

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

/* The script goes on at `resume`, the object's turn over for this frame. */
static void finish(Snes *s, uint16_t resume) { event_return(s, (EventStep){ resume, false }); }

/* ---- The commands to the driver ------------------------------------------------------------- */

/* Each command's sends, from its handler's calls of the driver. */

// $C0:3C11
/* Play a sound effect, from the middle. */
enum { PLAY_EFFECT = 0xE8, PLAY_EFFECT_BYTES = 2 };
static void play_effect(Platform *pf, const EventCommand *c)
{
    au_play_effect(pf, PF_CALL_DC(0xC03C26, 0xC70004, DRIVER_IGNORES), c->effect, AU_CENTRE);
}

// $C0:3C2F
/* Play a song, which the game remembers as the one playing. */
enum { PLAY_SONG = 0xEA, PLAY_SONG_BYTES = 2 };
static void play_song(GameState *g, Platform *pf, const EventCommand *c)
{
    set_current_song(g, c->song);
    au_play_song(pf, PF_CALL_DC(0xC03C43, 0xC70004, DRIVER_IGNORES), c->song);
}

// $C0:3C4C
/* Fade the music to a volume over a number of steps, and bring both pairs
 * of effect voices to full volume at once: three sends, the music's, then
 * each pair's. */
enum { FADE_MUSIC = 0xEB, FADE_MUSIC_BYTES = 3 };
static void fade_the_music(Platform *pf, const EventCommand *c)
{
    /* the original also leaves $FF as the third parameter, which the fade
     * doesn't read */
    AudioCommand fade = { .command = AU_FADE_MUSIC, .p1 = c->steps, .p2 = c->volume, .p3 = AU_FULL_VOLUME, .count = 3 };
    au_command(pf, PF_CALL_DC(0xC03C69, 0xC70004, DRIVER_IGNORES), fade);
}
static void bring_the_effects_up(Platform *pf, unsigned pair)
{
    static const struct {
        uint32_t site;
        uint8_t what;
    } pairs[] = { { 0xC03C7C, AU_FADE_EFFECTS }, { 0xC03C8F, AU_FADE_MORE_EFFECTS } };
    au_fade(pf, PF_CALL_DC(pairs[pair].site, 0xC70004, DRIVER_IGNORES), pairs[pair].what, 0, AU_FULL_VOLUME);
}

// $C0:3C98
/* Send the driver a command of the script's choosing, with two parameters:
 * whatever the language has no command for (a tempo slide, a mute). */
enum { SEND_SOUND_COMMAND = 0xEC, SEND_SOUND_COMMAND_BYTES = 4 };
static void send_the_command(Platform *pf, const EventCommand *c)
{
    AudioCommand command = { .command = c->sound_command, .p1 = c->first_argument, .p2 = c->second_argument,
                             .count = 2 };
    au_command(pf, PF_CALL_DC(0xC03CB4, 0xC70004, DRIVER_IGNORES), command);
}

/* As C: each command's sends, and the turn over with the script after it. */
static EventCommand command_at(GameState *g, Platform *pf, uint8_t opcode, uint16_t at)
{
    return event_decode(g, pf, event_opcode(opcode), at);
}
static bool after(EventStep *step, uint16_t at, uint16_t bytes)
{
    *step = (EventStep){ (uint16_t)(at + bytes), false };
    return true;
}

bool event_play_effect(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    EventCommand c = command_at(g, pf, PLAY_EFFECT, at);
    play_effect(pf, &c);
    return after(step, at, PLAY_EFFECT_BYTES);
}

bool event_play_song(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    EventCommand c = command_at(g, pf, PLAY_SONG, at);
    play_song(g, pf, &c);
    return after(step, at, PLAY_SONG_BYTES);
}

bool event_fade_music(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    EventCommand c = command_at(g, pf, FADE_MUSIC, at);
    fade_the_music(pf, &c);
    bring_the_effects_up(pf, 0);
    bring_the_effects_up(pf, 1);
    return after(step, at, FADE_MUSIC_BYTES);
}

bool event_send_sound_command(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    EventCommand c = command_at(g, pf, SEND_SOUND_COMMAND, at);
    send_the_command(pf, &c);
    return after(step, at, SEND_SOUND_COMMAND_BYTES);
}

/* On the machine each send is a call of the driver's CPU half, whose return
 * the bridge checks; Y is where the handler's parse of the operands
 * stopped, and the offset kept is the one before the script goes on. */
static bool sent(Snes *s, uint32_t returns_to, BridgeMachine as) { return bridge_back(s, returns_to, as); }

static void as_at_the_call(Snes *s, uint16_t at, uint16_t kept)
{
    ORIGINAL_EFFECT(event_keep_offset(state_of(s), kept));
    ORIGINAL_EFFECT(leave_parse_at_the_call(s, (uint16_t)(at + 1)));
}

void event_cmd_play_effect(Snes *s)
{
    uint16_t at = event_command_offset(s);
    EventCommand c = event_command_of(s, PLAY_EFFECT);
    BridgeMachine as = event_machine(s, false);
    as_at_the_call(s, at, (uint16_t)(at + PLAY_EFFECT_BYTES - 1));
    play_effect(m_platform(s), &c);
    if (sent(s, 0xC03C2A, as))
        finish(s, (uint16_t)(at + PLAY_EFFECT_BYTES));
}

void event_cmd_play_song(Snes *s)
{
    uint16_t at = event_command_offset(s);
    EventCommand c = event_command_of(s, PLAY_SONG);
    BridgeMachine as = event_machine(s, false);
    as_at_the_call(s, at, (uint16_t)(at + PLAY_SONG_BYTES - 1));
    play_song(state_of(s), m_platform(s), &c);
    if (sent(s, 0xC03C47, as))
        finish(s, (uint16_t)(at + PLAY_SONG_BYTES));
}

void event_cmd_fade_music(Snes *s)
{
    uint16_t at = event_command_offset(s);
    EventCommand c = event_command_of(s, FADE_MUSIC);
    BridgeMachine as = event_machine(s, false);
    as_at_the_call(s, at, (uint16_t)(at + FADE_MUSIC_BYTES - 1));
    fade_the_music(m_platform(s), &c);
    if (!sent(s, 0xC03C6D, as))
        return;
    bring_the_effects_up(m_platform(s), 0);
    if (!sent(s, 0xC03C80, as))
        return;
    bring_the_effects_up(m_platform(s), 1);
    if (sent(s, 0xC03C93, as))
        finish(s, (uint16_t)(at + FADE_MUSIC_BYTES));
}

void event_cmd_send_sound_command(Snes *s)
{
    uint16_t at = event_command_offset(s);
    EventCommand c = event_command_of(s, SEND_SOUND_COMMAND);
    BridgeMachine as = event_machine(s, false);
    as_at_the_call(s, at, (uint16_t)(at + SEND_SOUND_COMMAND_BYTES));
    send_the_command(m_platform(s), &c);
    if (sent(s, 0xC03CB8, as))
        finish(s, (uint16_t)(at + SEND_SOUND_COMMAND_BYTES));
}

// $C0:3CBC
/* Wait until the music has faded as far as it was told to. */
static EventFlow wait_for_music_fade(GameState *g, Platform *platform)
{
    (void)g;
    return au_music_fade_left(platform, PF_AT(0xC03CBD)) == 0 ? end_turn() : stay();
}
EVENT_OPERATION(wait_for_music_fade, platform)

// $C0:3CC5
/* Wait until the music has ended: none of its tracks playing. */
static EventFlow wait_for_music_to_end(GameState *g, Platform *platform)
{
    (void)g;
    return au_music_playing(platform, PF_AT(0xC03CC6)) == 0 ? end_turn() : stay();
}
EVENT_OPERATION(wait_for_music_to_end, platform)

EVENT_OPCODES(event_sound_opcodes,
    { 0xE8, .effect = OPERAND(1), .length = 2 },
    { 0xEA, .song = OPERAND(1), .length = 2 },
    { 0xEB, .steps = OPERAND(1), .volume = OPERAND(2), .length = 3 },
    { 0xEC, .sound_command = OPERAND(1), .first_argument = OPERAND(2), .second_argument = OPERAND(3), .length = 4 },
    { 0xED, OPERATION(wait_for_music_fade), .length = 1 },
    { 0xEE, OPERATION(wait_for_music_to_end), .length = 1 });
