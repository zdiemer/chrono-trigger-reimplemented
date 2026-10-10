/* Field mode: a map's objects started, as a location loads
 * (field/objects_start.c). */
#ifndef CT_GAME_FIELD_OBJECTS_START_H
#define CT_GAME_FIELD_OBJECTS_START_H

#include <stdbool.h>

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* Whatever the objects held let go: every block of sprite tiles, palette
 * slot and frame area free, every row empty and every object out of view;
 * and, as a location loads, the graphics sets the last map's scripts
 * unpacked forgotten. */
void field_objects_let_go(GameState *g, bool forget_the_graphics);

/* Each of the map's objects as its script first finds it, the slots past
 * them disabled, and what the scripts kept for the last map cleared
 * ($C0:5709, before the openings). */
void field_objects_start_afresh(GameState *g, Platform *pf);

/* The entries (game/registry.c): the objects set up ($C0:56A6), and started
 * then running their openings ($C0:5709), with their guards. */
void field_set_up_the_objects(Snes *s);
void field_start_the_objects(Snes *s);
bool field_objects_can_be_set_up(Snes *s);
bool field_objects_can_start(Snes *s);
enum { FIELD_SET_UP_THE_OBJECTS_ENTRY = 0xC056A6, FIELD_START_THE_OBJECTS_ENTRY = 0xC05709 };

#endif
