/* The CPU half of the sound driver in bank C7: uploading the driver, sending
 * commands and uploading data to the SPC700 through the audio service's driver
 * calls (platform/audio.h). */
#ifndef CT_GAME_CORE_SOUND_DRIVER_H
#define CT_GAME_CORE_SOUND_DRIVER_H

#include "snes/system.h"

/* The guard every entry but $C7:0000 and $C7:0004 has: the driver's data bank
 * ($00) and direct page ($1E00). */
bool sound_driver_frame_is_set(Snes *s);

/* the boot upload */
void sound_driver_boot(Snes *s);
void sound_driver_boot_start(Snes *s);
void sound_driver_boot_block(Snes *s);
void sound_driver_boot_next_byte(Snes *s);
void sound_driver_boot_started(Snes *s);
void sound_driver_boot_finish(Snes *s);

/* commands */
void sound_driver_command(Snes *s);
void sound_driver_command_timed_out(Snes *s);
void sound_driver_command_echoed(Snes *s);
void sound_driver_upload_finished(Snes *s);
void sound_driver_2f00_command_echoed(Snes *s);
void sound_driver_slot_uploaded(Snes *s);
void sound_driver_song_timed_out(Snes *s);
void sound_driver_song_echoed(Snes *s);
void sound_driver_song_refused(Snes *s);
void sound_driver_return(Snes *s);

/* subroutines */
void sound_driver_upload_block(Snes *s);
void sound_driver_upload_2f00_data(Snes *s);
void sound_driver_send_song(Snes *s);
void sound_driver_mark_listed_song(Snes *s);
void sound_driver_set_reserve(Snes *s);
void sound_driver_forget_slots_past_limit(Snes *s);
void sound_driver_send_counter(Snes *s);
void sound_driver_counter_echoed(Snes *s);
void sound_driver_resend_counter(Snes *s);
void sound_driver_counter_resent(Snes *s);

/* idle entries */
void sound_driver_boot_wait_ready_loop(Snes *s);
void sound_driver_boot_wait_start_loop(Snes *s);
void sound_driver_boot_wait_byte_loop(Snes *s);
void sound_driver_boot_wait_block_loop(Snes *s);
void sound_driver_boot_wait_jump_loop(Snes *s);
void sound_driver_boot_delay_loop(Snes *s);
void sound_driver_wait_command_echo_loop(Snes *s);
void sound_driver_wait_song_echo_loop(Snes *s);
void sound_driver_wait_upload_echo_loop(Snes *s);
void sound_driver_wait_slot_upload_echo_loop(Snes *s);
void sound_driver_wait_slot_upload_change_loop(Snes *s);
void sound_driver_wait_2f00_command_echo_loop(Snes *s);
void sound_driver_wait_2f00_echo_loop(Snes *s);
void sound_driver_wait_2f00_change_loop(Snes *s);
void sound_driver_wait_echo_loop(Snes *s);
void sound_driver_wait_resend_echo_loop(Snes *s);
void sound_driver_wait_port0_change_loop(Snes *s);

#endif
