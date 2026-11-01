/* The game's sound commands, handed to the sound driver the host plays (the C
 * driver, game/sound/): the boot and every command as calls, with nothing
 * uploaded and nothing waited for on the ports. This is the whole of what the
 * cartridge's CPU half in bank $C7 did; that half, which spoke to the
 * original's driver on the sound processor, is no port's any more and runs
 * only in the reference play, as the cartridge's code
 * (docs/REIMPLEMENTATION.md, "The sound driver's teardown"). */
#ifndef CT_GAME_CORE_SOUND_COMMANDS_H
#define CT_GAME_CORE_SOUND_COMMANDS_H

#include "platform/audio.h"
#include "state.h"

// $C7:0000 (as the host takes it)
/* The boot, as a host takes it: false where no host plays. */
bool sound_driver_host_boots(GameState *g, Platform *pf);

/* A command the game sends, as au_command sends it: into the command buffer
 * and, where a host plays, on to it. */
void sound_driver_send(GameState *g, Platform *pf, AudioCommand c);

/* The command already in the buffer handed to the host, and the buffer
 * cleared. */
void sound_driver_hand_the_buffer_to_the_host(GameState *g, Platform *pf);

#endif
