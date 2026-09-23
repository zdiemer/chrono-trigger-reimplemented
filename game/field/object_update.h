/* Field mode: the per-frame object update loop and its per-object steps. */
#ifndef CT_GAME_FIELD_OBJECT_UPDATE_H
#define CT_GAME_FIELD_OBJECT_UPDATE_H

#include "snes/system.h"

/* field/object_update.c */
void field_update_objects(Snes *s);
void field_unlink_object_row(Snes *s);
void field_link_object_row(Snes *s);
bool field_object_rows_linked(Snes *s);
void field_update_screen_position(Snes *s);

#endif
