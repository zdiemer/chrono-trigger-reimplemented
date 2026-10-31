/* The game's sound commands, handed to a sound driver the host plays (the C
 * driver, game/sound/): the boot and every command as calls, with nothing
 * uploaded and nothing waited for on the ports. This is all of the sound
 * driver's CPU half a build with no machine has; the half that talks to the
 * original driver on the sound processor is core/sound_driver.h. */
#ifndef CT_GAME_CORE_SOUND_COMMANDS_H
#define CT_GAME_CORE_SOUND_COMMANDS_H

#include "platform/audio.h"
#include "state.h"

/* The boot, as a host takes it: false where no host plays. */
bool sound_driver_host_boots(GameState *g, Platform *pf);

/* A command the game sends, as au_command sends it: into the command buffer
 * and, where a host plays, on to it. A build with no machine calls this in
 * place of the call the machine makes ($C7:0004). */
void sound_driver_send(GameState *g, Platform *pf, AudioCommand c);

/* The command already in the buffer handed to the host, and the buffer
 * cleared: what $C7:0004 does when a host plays. */
void sound_driver_hand_the_buffer_to_the_host(GameState *g, Platform *pf);

#endif
