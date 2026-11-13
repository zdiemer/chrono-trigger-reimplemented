/* Field objects' frames: drawing an object's new frame. */
#ifndef CT_GAME_FIELD_FRAMES_H
#define CT_GAME_FIELD_FRAMES_H

#include <stdbool.h>

#include <stdint.h>

/* field/frames.c: the frame builder's timing key (TIMED_AS). */
enum { FIELD_DRAW_NEXT_FRAME_ENTRY = 0xC0CAD9 };
#include "field/object.h"
#include "platform/platform.h"

/* A part of the object's next frame drawn: true while more of it is to draw; false too where the object holds
 * a frame it never draws (field/frames.c). */
bool field_frame_part_drawn(GameState *g, FieldObject obj);
/* The frame queue drawn through, while the frame has time. */
void field_queued_frames_drawn(GameState *g, Platform *pf);

/* The frame an object's tiles show put straight onto its records (after a
 * battle, field/battle_return.c), and whether the port can for the object. */
/* One of a frame's sprites as the frame builder would lay it out: its
 * offset from the object's point, the graphics set's tile in each quarter
 * (top left, top right, bottom left, bottom right; field/graphics.c), and
 * its attributes. */
enum { FIELD_FRAME_MOST_SPRITES = 12, FIELD_SPRITE_QUARTERS = 4 };
typedef struct {
    int8_t x, y;
    uint16_t tile[FIELD_SPRITE_QUARTERS];
    bool has_tile[FIELD_SPRITE_QUARTERS];
    uint8_t attributes;
} FrameSpriteLook;
/* The sprites of the object's frame `frame`, worked out without drawing
 * anything: for a wider view to show an object the field doesn't run
 * (field/margin_objects.c). None for the 24-sprite figure, whose one frame
 * is drawn where it is loaded. */
unsigned object_frame_look(const GameState *g, FieldObject obj, uint8_t frame, FrameSpriteLook out[FIELD_FRAME_MOST_SPRITES]);

void object_put_drawn_frame_back(GameState *g, FieldObject obj);
bool object_drawn_frame_can_go_back(const GameState *g, FieldObject obj);

/* A 24-sprite figure's frame drawn whole (field/frames.c); and whether the
 * port draws one with these graphics, frame descriptions and records as the
 * original does. */
void object_draw_whole_figure(GameState *g, Platform *pf, FieldObject obj);
bool field_figure_drawable(const GameState *g, ObjectGraphics set, ObjectGraphics descriptions, uint16_t records);

#endif
