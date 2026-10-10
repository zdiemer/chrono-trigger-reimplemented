/* Field mode: the per-frame object update loop and its per-object steps. */
#ifndef CT_GAME_FIELD_OBJECT_UPDATE_H
#define CT_GAME_FIELD_OBJECT_UPDATE_H

#include "snes/system.h"
#include "state.h"

/* field/object_update.c */
/* Every row empty and every object out of the rows, as a map's screen
 * starts ($C0:B204). */
void field_empty_the_rows(GameState *g);

void field_update_objects(Snes *s);
enum { FIELD_UPDATE_SCREEN_POSITION_ENTRY = 0xC0AB45 };
void field_leave_object_row(Snes *s);
void field_join_object_row(Snes *s);
bool field_object_rows_linked(Snes *s);
bool field_objects_counted(Snes *s);
void field_update_screen_position(Snes *s);

#endif
