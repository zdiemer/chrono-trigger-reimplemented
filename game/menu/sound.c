/* The menus' music.
 *
 * Opening the menus from the field fades the field's song out and starts the
 * menus' own, which the sound driver plays while it keeps the field's song
 * where it was ($C2:CB96, and the task it starts at $C2:CBBF, which sends
 * command $14); closing them fades the menus' song
 * out and starts the field's again, from where it was kept ($C2:CC0E, command
 * $11). Between the fade and the new song the menus wait for the driver: for
 * the fade to run out (the driver counts its steps left on its status) and,
 * after starting their own song, for the song to set its marker, which it does
 * as it starts. These are those waits, a pass at a time; the rest is the
 * original's. No movie reaches them. */
#include "menu/sound.h"

#include "platform/audio.h"
#include "port/machine.h"

/* One pass of a wait for the music's fade to run out: on to `done` once it
 * has, else to `wait`, the call that lets a frame go by before looking again.
 * The status itself is dead at both, and its flags are read on the way to the
 * call, which saves them. */
static void wait_for_the_fade(Snes *s, uint32_t site, uint32_t wait, uint32_t done)
{
    uint8_t steps_left = au_music_fade_left(m_platform(s), PF_AT(site));
    m_set_nz8(s, steps_left);
    m_stop_at(s, steps_left == 0 ? done : wait, 0);
}

// $C2:CBC1
/* Opening the menus: wait for the field's song to fade out, then on to
 * starting the menus' song at $C2:CBCC. */
void menu_wait_for_field_song_faded(Snes *s) { wait_for_the_fade(s, 0xC2CBC1, 0xC2CBC7, 0xC2CBCC); }

// $C2:CC28
/* Closing them: wait for the menus' song to fade out, then on to starting
 * the field's again at $C2:CC33. */
void menu_wait_for_menu_song_faded(Snes *s) { wait_for_the_fade(s, 0xC2CC28, 0xC2CC2E, 0xC2CC33); }

// $C2:CBE0
/* After starting the menus' song, wait for it to set its marker, then on to
 * $C2:CBE8, where nothing reads what the loop loaded. Idle entry, one pass. */
void menu_wait_for_menu_song_started_loop(Snes *s)
{
    m_stop_at(s, au_song_marker(m_platform(s), PF_AT(0xC2CBE0)) != 0 ? 0xC2CBE8 : 0xC2CBE0, 0);
}
