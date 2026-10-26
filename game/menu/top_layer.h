/* The layer over the world maps: the objects that move it
 * (game/menu/top_layer.c). */
#ifndef CT_GAME_MENU_TOP_LAYER_H
#define CT_GAME_MENU_TOP_LAYER_H

#include "menu/handlers.h"
#include "snes/system.h"

/* The objects' turns as C (menu/handlers.c). */
MenuTurn menu_top_layer_slow_drift_turn(GameState *g, Platform *pf, MenuObject o);
MenuTurn menu_top_layer_drift_turn(GameState *g, Platform *pf, MenuObject o);
MenuTurn menu_top_layer_fast_drift_turn(GameState *g, Platform *pf, MenuObject o);
MenuTurn menu_top_layer_wave_drift_turn(GameState *g, Platform *pf, MenuObject o);
MenuTurn menu_top_layer_wave_turn(GameState *g, Platform *pf, MenuObject o);
/* The layer's shimmering wave, its lines bent a sine's worth down the
 * screen (game/menu/wave.c). */
MenuTurn menu_shimmer_turn(GameState *g, Platform *pf, MenuObject o);

void menu_top_layer_slow_drift(Snes *s);
void menu_top_layer_drift(Snes *s);
void menu_top_layer_fast_drift(Snes *s);
void menu_top_layer_wave_drift(Snes *s);
void menu_top_layer_wave(Snes *s);
bool menu_top_layer_object_runs(Snes *s);

#endif
