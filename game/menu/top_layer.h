/* The layer over the world maps: the objects that move it
 * (game/menu/top_layer.c). */
#ifndef CT_GAME_MENU_TOP_LAYER_H
#define CT_GAME_MENU_TOP_LAYER_H

#include "snes/system.h"

void menu_top_layer_slow_drift(Snes *s);
void menu_top_layer_drift(Snes *s);
void menu_top_layer_fast_drift(Snes *s);
void menu_top_layer_wave_drift(Snes *s);
void menu_top_layer_wave(Snes *s);
bool menu_top_layer_object_runs(Snes *s);

#endif
