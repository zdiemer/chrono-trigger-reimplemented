/* Scripts and sound: playing a sound effect or a song, sending the sound
 * driver a command of its own (a fade, a change of tempo), and waiting for it
 * to be done.
 *
 * Commands to the driver go through the audio service (platform/audio.h),
 * which on the development system writes them to $1E00-$1E03 and calls the
 * driver's entry at $C7:0004, the original's code; so the commands that send
 * one are bridges, which check what the driver left before going on, and
 * keep the command's end in event_saved_position across the call, as the
 * original does. The waits only read the driver's status, and are
 * operations like any other. */
#include "event/bridge.h"

#include "platform/audio.h"

static void load_command(Snes *s, uint8_t command)
{
    s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | command);
    m_set_nz8(s, command);
}

static void registers_at_call(Snes *s, uint8_t command, uint16_t x, uint16_t y)
{
    event_set_registers(s, s->cpu.a, x, y);
    load_command(s, command);
}

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

/* Send a driver command from the JSL at `site`; false if the driver came back
 * other than the command expects, and the original then carries on. */
static bool send(Snes *s, uint32_t site, AudioCommand command)
{
    au_command(m_platform(s), PF_CALL(site, 0xC70004), command);
    return back(s, site + 4, false);
}

/* Go on at the offset kept in event_saved_position, plus `past`, ending the
 * turn; the driver leaves Y. */
static void finish(Snes *s, uint16_t past)
{
    event_return(s, (EventStep){ (uint16_t)(event_saved_position(state_of(s)) + past), false });
}

// $C0:3C11
/* Play a sound effect. */
void event_cmd_play_sound(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    set_event_saved_position(g, (uint16_t)(at + 1));
    registers_at_call(s, 0x18, (uint16_t)(at + 1), (uint16_t)(at + 1));
    uint8_t effect = script_byte(g, (uint16_t)(at + 1));
    if (send(s, 0xC03C26, (AudioCommand){ .command = 0x18, .p1 = effect, .p2 = 0x80, .count = 2 }))
        finish(s, 1);
}

// $C0:3C2F
/* Play a song, which the game remembers as the one playing. */
void event_cmd_play_song(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    uint8_t song = script_byte(g, (uint16_t)(at + 1));
    set_current_song(g, song);
    set_event_saved_position(g, (uint16_t)(at + 1));
    registers_at_call(s, 0x10, (uint16_t)(at + 1), (uint16_t)(at + 1));
    au_play_song(m_platform(s), PF_CALL(0xC03C43, 0xC70004), song);
    if (back(s, 0xC03C47, false))
        finish(s, 1);
}

// $C0:3C4C
/* Driver command $81 with the command's two operands, then $82 and $83. */
void event_cmd_sound_81_83(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    set_event_saved_position(g, (uint16_t)(at + 2));
    registers_at_call(s, 0x81, (uint16_t)(at + 2), (uint16_t)(at + 1));
    if (!send(s, 0xC03C69, (AudioCommand){ .command = 0x81, .p1 = script_byte(g, (uint16_t)(at + 1)),
                                            .p2 = script_byte(g, (uint16_t)(at + 2)), .p3 = 0xFF, .count = 3 }))
        return;
    load_command(s, 0x82);
    if (!send(s, 0xC03C7C, (AudioCommand){ .command = 0x82, .p1 = 0x00, .p2 = 0xFF, .count = 2 }))
        return;
    load_command(s, 0x83);
    au_command(m_platform(s), PF_CALL(0xC03C8F, 0xC70004),
               (AudioCommand){ .command = 0x83, .p1 = 0x00, .p2 = 0xFF, .count = 2 });
    if (back(s, 0xC03C93, false))
        finish(s, 1);
}

// $C0:3C98
/* Send the driver a command of the script's choosing, with two parameters. */
void event_cmd_sound_command(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    uint8_t command = script_byte(g, (uint16_t)(at + 1)), first = script_byte(g, (uint16_t)(at + 2));
    uint8_t second = script_byte(g, (uint16_t)(at + 3));
    set_event_saved_position(g, (uint16_t)(at + 4));
    registers_at_call(s, second, (uint16_t)(at + 4), (uint16_t)(at + 1));
    m_set_nz16(s, s->cpu.x);   /* the INX after the last load */
    au_command(m_platform(s), PF_CALL(0xC03CB4, 0xC70004),
               (AudioCommand){ .command = command, .p1 = first, .p2 = second, .count = 2 });
    if (back(s, 0xC03CB8, false))
        finish(s, 0);
}

// $C0:3CBC
/* Wait until the driver says it is idle: its status on a port, masked, reads
 * zero. */
static EventFlow wait_for_sound(Platform *pf, PfOrigin at, uint8_t port, uint8_t mask)
{
    return (au_status(pf, at, port) & mask) == 0 ? end_turn() : stay();
}

static EventFlow run_wait_for_port_1(GameState *g, const EventArgs *a)
{
    (void)g;
    return wait_for_sound(a->platform, PF_AT(0xC03CBD), 1, 0xFF);
}

static EventFlow run_wait_for_port_3(GameState *g, const EventArgs *a)
{
    (void)g;
    return wait_for_sound(a->platform, PF_AT(0xC03CC6), 3, 0x0F);
}

EVENT_OPCODES(event_sound_opcodes,
    { 0xED, run_wait_for_port_1, { { 0 } }, 1, .routine = 0xC03CBC },
    { 0xEE, run_wait_for_port_3, { { 0 } }, 1, .routine = 0xC03CC5 });
