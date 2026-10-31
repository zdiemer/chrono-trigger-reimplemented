/* The game's sound commands, as a host that plays a sound driver of its own
 * takes them (platform/audio.h, au_driver_hosted): the C sound driver
 * (game/sound/), played in the sound processor's place.
 *
 * The game asks for sound by putting a command and its three parameters in
 * the command buffer and calling the driver. The host takes the command as
 * a call, with the set of effects the next song is to bring, which a
 * command $70 switches between the field's and the battle's. Nothing is
 * uploaded: the C driver places the songs' samples itself. This is the
 * whole of the CPU half a build with no machine needs; the protocol the
 * original's driver is spoken to in, on the machine, is core/sound_driver.c. */
#include "core/sound_commands.h"

#include "vars.h"

/* The set of effects the game asks the next song to bring: the field's (0)
 * or the battle's. */
static bool battle_effects(uint8_t asked) { return asked != 0; }

/* The boot, as a host takes it: the next song brings the first set of
 * effects, and the command buffer is clear. */
bool sound_driver_host_boots(GameState *g, Platform *pf)
{
    if (!au_driver_boot_taken(pf))
        return false;
    set_sound_driver_effects_asked(g, 0);
    set_sound_command(g, 0);
    return true;
}

// $C7:0004 (as a host that plays its own driver takes it)
/* The command in the buffer, handed to the host with the set of effects the
 * next song is to bring, which a switch of them changes; then the buffer is
 * clear. */
void sound_driver_hand_the_buffer_to_the_host(GameState *g, Platform *pf)
{
    AudioCommand c = { .command = sound_command(g), .p1 = sound_command_parameters(g, 0),
                       .p2 = sound_command_parameters(g, 1), .p3 = sound_command_parameters(g, 2), .count = 3 };
    au_driver_command_taken(pf, c, sound_driver_effects_asked(g));
    if (c.command == AU_SWITCH_EFFECTS)
        set_sound_driver_effects_asked(g, battle_effects(c.p1));
    set_sound_command(g, 0);
}

void sound_driver_send(GameState *g, Platform *pf, AudioCommand c)
{
    const uint8_t parameters[3] = { c.p1, c.p2, c.p3 };
    for (unsigned i = 0; i < c.count && i < 3; i++)
        set_sound_command_parameters(g, i, parameters[i]);
    set_sound_command(g, c.command);
    if (au_driver_hosted(pf))
        sound_driver_hand_the_buffer_to_the_host(g, pf);
}
