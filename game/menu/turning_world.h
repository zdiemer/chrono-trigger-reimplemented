/* The Day of Lavos's turning world (game/menu/turning_world.c): the routines
 * of bank C6 the 1000 A.D. world map's scene calls from its object's script. */
#ifndef CT_GAME_MENU_TURNING_WORLD_H
#define CT_GAME_MENU_TURNING_WORLD_H

#include <stdbool.h>

#include "snes/system.h"

bool menu_world_can_be_drawn(Snes *s);
bool menu_world_can_be_turned(Snes *s);
bool menu_world_can_turn_a_frame(Snes *s);

void menu_object_draw_the_world(Snes *s);
void menu_object_start_the_world_turning(Snes *s);
void menu_object_turn_the_world(Snes *s);
void menu_object_turn_the_world_on(Snes *s);

#endif
