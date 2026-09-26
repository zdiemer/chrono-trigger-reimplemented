/* Field mode: the pause, and the menus opened from the field (field/menus.c). */
#ifndef CT_GAME_FIELD_MENUS_H
#define CT_GAME_FIELD_MENUS_H

#include <stdbool.h>

#include "snes/system.h"

void field_pause_and_menus(Snes *s);
void field_press_start(Snes *s);
void field_bring_the_party_back(Snes *s);
bool field_party_can_come_back(Snes *s);

#endif
