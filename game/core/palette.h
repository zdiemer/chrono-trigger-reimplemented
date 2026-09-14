/* Palette animation shared by every game mode. */
#ifndef CT_GAME_CORE_PALETTE_H
#define CT_GAME_CORE_PALETTE_H

#include "snes/system.h"

void palette_animate(Snes *s);
void palette_animate_fade(Snes *s);
void palette_fade_toward_white(Snes *s);

#endif
