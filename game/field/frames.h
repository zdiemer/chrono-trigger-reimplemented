/* Field objects' frames: drawing an object's new frame. */
#ifndef CT_GAME_FIELD_FRAMES_H
#define CT_GAME_FIELD_FRAMES_H

#include <stdbool.h>

#include "snes/system.h"

/* field/frames.c */
void field_draw_next_frame(Snes *s);
bool field_frames_can_draw(Snes *s);
void field_put_drawn_frame(Snes *s);
bool field_frames_can_put(Snes *s);

#endif
