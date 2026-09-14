/* Controller input shared by the menus and battle. */
#ifndef CT_GAME_CORE_INPUT_H
#define CT_GAME_CORE_INPUT_H

#include "snes/system.h"

void input_read_joypad(Snes *s);
void input_map_buttons(Snes *s);
void play_time_tick(Snes *s);
void input_update(Snes *s);

#endif
