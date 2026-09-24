/* The menus' music: the waits for the sound driver between the field's song
 * and the menus' own (game/menu/sound.c). */
#ifndef CT_GAME_MENU_SOUND_H
#define CT_GAME_MENU_SOUND_H

#include "snes/system.h"

void menu_wait_for_field_song_faded(Snes *s);
void menu_wait_for_menu_song_faded(Snes *s);
void menu_wait_for_menu_music_playing_loop(Snes *s);

#endif
