/* Field objects' frames: drawing an object's new frame. */
#ifndef CT_GAME_FIELD_FRAMES_H
#define CT_GAME_FIELD_FRAMES_H

#include <stdbool.h>

#include "snes/system.h"

/* field/frames.c */
void field_draw_next_frame(Snes *s);
bool field_frames_can_draw(Snes *s);
#include "field/object.h"

/* The frame an object's tiles show put straight onto its records (after a
 * battle, field/battle_return.c), and whether the port can for the object. */
void object_put_drawn_frame_back(GameState *g, FieldObject obj);
bool object_drawn_frame_can_go_back(const GameState *g, FieldObject obj);
void field_put_drawn_frame(Snes *s);
bool field_frames_can_put(Snes *s);

#endif
