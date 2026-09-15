/* Menu: waits for the sound driver's status (menu/sound.c). */
#ifndef CT_GAME_MENU_SOUND_H
#define CT_GAME_MENU_SOUND_H

#include "snes/system.h"

void menu_wait_sound_port1_cbc1(Snes *s);
void menu_wait_sound_port1_cc28(Snes *s);
void menu_wait_sound_port3_loop(Snes *s);

#endif
