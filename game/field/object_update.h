/* Field mode: the per-frame object update loop and its per-object steps. */
#ifndef CT_GAME_FIELD_OBJECT_UPDATE_H
#define CT_GAME_FIELD_OBJECT_UPDATE_H

#include "snes/system.h"

/* field/object_update.c */
void field_update_objects(Snes *s);
void field_update_object_in_window(Snes *s);
void field_unlink_object_row(Snes *s);
void field_link_object_row(Snes *s);
void field_move_object(Snes *s);
void field_move_object_unbounded(Snes *s);
void field_update_screen_position(Snes *s);
void field_step_object_animation(Snes *s);

#endif
