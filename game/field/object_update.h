/* Field mode: the per-frame object update loop and its per-object steps. */
#ifndef CT_GAME_FIELD_OBJECT_UPDATE_H
#define CT_GAME_FIELD_OBJECT_UPDATE_H

#include <stdbool.h>

#include "field/object.h"
#include <stdint.h>
#include "state.h"

/* field/object_update.c */
/* Every row empty and every object out of the rows, as a map's screen
 * starts ($C0:B204). */
void field_empty_the_rows(GameState *g);

/* The objects' update, once a frame ($C0:A810): false where a step is one
 * the port doesn't take, the program going no further there
 * (core/program.h). A row joined or left, the same. */
bool field_objects_updated(GameState *g);
bool object_rows_step(GameState *g, FieldObject obj, bool join);
enum { FIELD_UPDATE_SCREEN_POSITION_ENTRY = 0xC0AB45 };

#endif
