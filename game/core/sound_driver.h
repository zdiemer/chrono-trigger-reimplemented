/* The CPU half of the sound driver in bank C7: uploading the driver, sending
 * commands and uploading data to the SPC700 through the audio service's driver
 * calls (platform/audio.h). */
#ifndef CT_GAME_CORE_SOUND_DRIVER_H
#define CT_GAME_CORE_SOUND_DRIVER_H

#include "platform/audio.h"
#include "snes/system.h"
#include "state.h"

/* With a host that plays a driver of its own (au_driver_hosted): the boot,
 * false where no host takes it; and a command the game sends, as au_command
 * sends it, through the buffer to the host, which a build with no machine
 * calls in place of the call the machine makes. */
bool sound_driver_host_boots(GameState *g, Platform *pf);
void sound_driver_send(GameState *g, Platform *pf, AudioCommand c);

/* The guard every entry but $C7:0000 and $C7:0004 has: the driver's data bank
 * ($00) and direct page ($1E00). */
bool sound_driver_frame_is_set(Snes *s);
/* $C7:0004's: the slots hold what the driver keeps there. */
bool sound_driver_slots_are_kept(Snes *s);

/* the boot upload */
void sound_driver_boot(Snes *s);
void sound_driver_boot_upload(Snes *s);
void sound_driver_boot_finish(Snes *s);

/* commands; and the routines the original's commands call that wait for the
 * driver, as its own code reaches them (the command runs them as C) */
void sound_driver_command(Snes *s);
void sound_driver_send_song(Snes *s);
void sound_driver_upload_block(Snes *s);
void sound_driver_upload_effects(Snes *s);

/* idle entries */
void sound_driver_boot_wait_ready_loop(Snes *s);
void sound_driver_boot_delay_loop(Snes *s);

#endif
