/* Field objects a wider view shows past the window the field runs them in
 * (field/margin_objects.c). */
#ifndef CT_GAME_FIELD_MARGIN_OBJECTS_H
#define CT_GAME_FIELD_MARGIN_OBJECTS_H

#include <stdint.h>

#include "field/object.h"
#include "platform/platform.h"
#include "state.h"

/* The objects listed to show in the margins, nearest the viewer first: each
 * one's frame and its place on the screen. The program's own state, beside
 * the game's memory. */
typedef struct {
    FieldObject object;
    uint8_t frame;
    int16_t x, y;
} FieldObjectPast;
typedef struct {
    uint8_t count;
    FieldObjectPast object[FIELD_OBJECTS];
} FieldObjectsPast;
/* Module storage supplied by the state owner, independent of mapped RAM. */
FieldObjectsPast *field_objects_past(GameState *g);

/* As a frame the field finished is sent: the objects it shows past the
 * window listed again. As a map's screen is sent: none, until the next. */
void field_list_the_objects_past_the_window(GameState *g, Platform *pf);
void field_forget_the_objects_past_the_window(GameState *g);
/* Every vblank: the listed objects shown in the view's margins. */
void field_show_the_objects_past_the_window(GameState *g, Platform *pf);

#endif
