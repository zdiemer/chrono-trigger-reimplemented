/* Field objects' frames: drawing an object's new frame. */
#ifndef CT_GAME_FIELD_FRAMES_H
#define CT_GAME_FIELD_FRAMES_H

#include <stdbool.h>

#include "snes/system.h"

/* field/frames.c */
void field_draw_next_frame(Snes *s);
bool field_frames_can_draw(Snes *s);
enum { FIELD_DRAW_NEXT_FRAME_ENTRY = 0xC0CAD9 };
/* The frame queue drawn through, while the frame has time. */
void field_draw_queued_frames(Snes *s);
bool field_queued_frames_can_be_drawn(Snes *s);
#include "field/object.h"
#include "platform/platform.h"

/* A part of the object's next frame drawn (field_draw_next_frame's work),
 * as C: true while more of it is to draw. */
bool field_frame_part_drawn(GameState *g, FieldObject obj);
/* The frame queue drawn through, while the frame has time, as C. */
void field_queued_frames_drawn(GameState *g, Platform *pf);

/* The frame an object's tiles show put straight onto its records (after a
 * battle, field/battle_return.c), and whether the port can for the object. */
void object_put_drawn_frame_back(GameState *g, FieldObject obj);
bool object_drawn_frame_can_go_back(const GameState *g, FieldObject obj);
void field_put_drawn_frame(Snes *s);
bool field_frames_can_put(Snes *s);

/* A 24-sprite figure's frame drawn whole (field/frames.c); and whether the
 * port draws one with these graphics, frame descriptions and records as the
 * original does (the guards'). What the drawing leaves in Y, which the event
 * engine has live after the command that loads a figure. */
void object_draw_whole_figure(GameState *g, Platform *pf, FieldObject obj);
bool field_figure_drawable(const GameState *g, ObjectGraphics set, ObjectGraphics descriptions, uint16_t records);
uint16_t field_whole_figure_y(void);
void field_draw_whole_figure(Snes *s);
bool field_figure_can_be_drawn(Snes *s);

#endif
