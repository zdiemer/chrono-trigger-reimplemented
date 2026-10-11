/* The place's sign (game/overworld/sign.c). */
#ifndef CT_GAME_OVERWORLD_SIGN_H
#define CT_GAME_OVERWORLD_SIGN_H

#include <stdbool.h>

#include "overworld/overworld.h"

/* The guard's: whether the sign is on one of its steps, and what it does
 * there the typesetter, the upload queue and the sprites take: the name one
 * it can type, room for its two lines, and its sprite script one the sprites
 * read. */
bool overworld_sign_can_show(const GameState *g, MenuObject self);

/* Build the sign's sprite script afresh from the ROM's, its six sprites
 * hidden until it shows a name; the world map does as it comes back. */
void overworld_build_the_sign_script(GameState *g);

/* Clear the label the sign's name is set in, which the map view's markers
 * are set in too. */
void overworld_clear_the_sign_label(GameState *g);

#endif
