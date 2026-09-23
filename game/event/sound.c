/* Event commands: sound. Commands for the sound driver go through the audio
 * service (platform/audio.h), which on the development system writes them to
 * $1E00-$1E03 and calls the driver's entry at $C7:0004, the original's code;
 * so the commands that send one are bridges that check what the driver left
 * before going on, and keep the command's end in event_saved_position across
 * it, as the original does. Registered with a guard that the data bank reaches
 * $1E00 and the APU ports the way the service does. */
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
    event_return(s, step_at((uint16_t)(event_saved_position(state_of(s)) + past), false, s->cpu.y));
}

// $C0:3C11
/* $E8: play sound effect (operand 1). */
void event_cmd_play_sound(Snes *s)
{
    GameState *g = state_of(s);
    EventCommand c = event_command_at(s);
    set_event_saved_position(g, (uint16_t)(c.at + 1));
    registers_at_call(s, 0x18, (uint16_t)(c.at + 1), (uint16_t)(c.at + 1));
    if (send(s, 0xC03C26, (AudioCommand){ .command = 0x18, .p1 = operand(g, &c, 1), .p2 = 0x80, .count = 2 }))
        finish(s, 1);
}

// $C0:3C2F
/* $EA: play song (operand 1), which the game keeps as the current song. */
void event_cmd_play_song(Snes *s)
{
    GameState *g = state_of(s);
    EventCommand c = event_command_at(s);
    uint8_t song = operand(g, &c, 1);
    set_current_song(g, song);
    set_event_saved_position(g, (uint16_t)(c.at + 1));
    registers_at_call(s, 0x10, (uint16_t)(c.at + 1), (uint16_t)(c.at + 1));
    au_play_song(m_platform(s), PF_CALL(0xC03C43, 0xC70004), song);
    if (back(s, 0xC03C47, false))
        finish(s, 1);
}

// $C0:3C4C
/* $EB: driver command $81 with operands 1 and 2, then $82 and $83. */
void event_cmd_sound_81_83(Snes *s)
{
    GameState *g = state_of(s);
    EventCommand c = event_command_at(s);
    set_event_saved_position(g, (uint16_t)(c.at + 2));
    registers_at_call(s, 0x81, (uint16_t)(c.at + 2), (uint16_t)(c.at + 1));
    if (!send(s, 0xC03C69, (AudioCommand){ .command = 0x81, .p1 = operand(g, &c, 1), .p2 = operand(g, &c, 2),
                                            .p3 = 0xFF, .count = 3 }))
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
/* $EC: a driver command (operand 1) with two parameters (operands 2-3). */
void event_cmd_sound_command(Snes *s)
{
    GameState *g = state_of(s);
    EventCommand c = event_command_at(s);
    set_event_saved_position(g, (uint16_t)(c.at + 4));
    registers_at_call(s, operand(g, &c, 3), (uint16_t)(c.at + 4), (uint16_t)(c.at + 1));
    m_set_nz16(s, s->cpu.x);   /* the INX after the last load */
    au_command(m_platform(s), PF_CALL(0xC03CB4, 0xC70004),
               (AudioCommand){ .command = operand(g, &c, 1), .p1 = operand(g, &c, 2), .p2 = operand(g, &c, 3),
                               .count = 2 });
    if (back(s, 0xC03CB8, false))
        finish(s, 0);
}

/* Stay on the command until the driver's status reads `idle`. */
static EventStep wait_status(const EventCommand *c, bool idle) { return end_turn(c, idle ? 1 : 0, 0); }

// $C0:3CBC
/* $ED: wait until the driver's status on port 1 reads zero. */
static EventStep wait_sound_port1(GameState *g, Platform *pf, const EventCommand *c)
{
    (void)g;
    return wait_status(c, au_status(pf, PF_AT(0xC03CBD), 1) == 0);
}

// $C0:3CC5
/* $EE: wait until the low nibble of the driver's status on port 3 reads zero. */
static EventStep wait_sound_port3(GameState *g, Platform *pf, const EventCommand *c)
{
    (void)g;
    return wait_status(c, (au_status(pf, PF_AT(0xC03CC6), 3) & 0x0F) == 0);
}

EVENT_PLATFORM_COMMAND(event_cmd_wait_apu_port1, wait_sound_port1)
EVENT_PLATFORM_COMMAND(event_cmd_wait_apu_port3, wait_sound_port3)
