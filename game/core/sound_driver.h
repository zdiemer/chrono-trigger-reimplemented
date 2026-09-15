/* The CPU side of the sound driver interface in bank C7: sending commands and
 * uploading data to the SPC700 through the APU ports. */
#ifndef CT_GAME_CORE_SOUND_DRIVER_H
#define CT_GAME_CORE_SOUND_DRIVER_H

#include "snes/system.h"

void sound_driver_command(Snes *s);
void sound_driver_command_timed_out(Snes *s);
void sound_driver_command_echoed(Snes *s);
void sound_driver_upload_finished(Snes *s);
void sound_driver_upload_block(Snes *s);
void sound_driver_upload_2f00_data(Snes *s);
void sound_driver_send_song(Snes *s);
void sound_driver_song_timed_out(Snes *s);
void sound_driver_song_echoed(Snes *s);
void sound_driver_song_refused(Snes *s);
void sound_driver_wait_command_echo_loop(Snes *s);
void sound_driver_wait_song_echo_loop(Snes *s);
void sound_driver_wait_echo_loop(Snes *s);
void sound_driver_wait_port0_change_loop(Snes *s);

#endif
