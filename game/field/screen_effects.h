/* Field mode: the screen effects, shapes the windows cut into the picture
 * (field/screen_effects.c). Scripts start them (event $FF, and $FE for the
 * searchlight, game/event/screen.c) and the field's main loop moves the one
 * under way on a step every frame. */
#ifndef CT_GAME_FIELD_SCREEN_EFFECTS_H
#define CT_GAME_FIELD_SCREEN_EFFECTS_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"
#include "vars.h"

/* The effect under way (field_screen_effect). The ones drawn into a single
 * frame's table take two frames, the second drawing the same into the other
 * table set; the ripple, once played, stays RIPPLE_PLAYED until the script
 * waiting on it sees it. */
typedef enum {
    NO_SCREEN_EFFECT = 0,
    HOLE_OPENING = 1,             /* a round hole in BG1 and BG2 grows to its radius */
    HOLE_CLOSING = 2,             /* ... and shrinks away */
    CURTAIN = 3,                  /* a ragged edge sweeps across the screen */
    OVAL_OPENING = 4,             /* an oval of the map's colour blending grows to its radius */
    OVAL_CLOSING = 5,             /* ... and shrinks away */
    SEARCHLIGHT = 6,              /* the colour blending inside a moving four-cornered shape */
    BG3_IN_A_CIRCLE = 7,          /* BG3 shows only through a circle at the top of the screen */
    BG3_IN_A_CIRCLE_SECOND = 8,
    BG3_BLENDED_ROUND_A_CIRCLE = 9,   /* BG3 alone, blended over the map, cut by that circle */
    BG3_BLENDED_ROUND_A_CIRCLE_SECOND = 10,
    ON_TO_THE_SCENE = 11,         /* the field closes, and a scene starts */
    RIPPLE = 12,                  /* a rippling copy of BG1 seen through a circle that opens and closes */
    RIPPLE_PLAYED = 13,
    OVAL_GONE = 14,               /* the oval's blending gone and the screen's colour as the field starts it */
    SCREEN_EFFECTS                /* past the last: the original's table runs on into its error trap */
} ScreenEffect;

static inline ScreenEffect field_screen_effect_now(const GameState *g)
{
    return (ScreenEffect)field_screen_effect(g);
}

/* Start an effect that takes nothing but its name. */
void screen_effect_start(GameState *g, ScreenEffect effect);

/* A hole (HOLE_OPENING) or an oval (OVAL_OPENING) opening at a place on the
 * screen, to a radius in lines. */
void screen_circle_opens(GameState *g, ScreenEffect opening, uint8_t x, uint8_t y, uint8_t radius);

/* The four ways the curtain sweeps: its ragged edge moves from one side of
 * the screen to the other, uncovering the picture or covering it (and leaving
 * it covered). */
typedef enum { CURTAIN_UNCOVERS_RIGHTWARD, CURTAIN_COVERS_RIGHTWARD, CURTAIN_UNCOVERS_LEFTWARD, CURTAIN_COVERS_LEFTWARD } Curtain;

/* A script waiting on a curtain: the curtain started if nothing runs, and
 * once it has swept across, done with. True when it is done. */
bool screen_curtain_drawn(GameState *g, Curtain curtain);

/* A script waiting on the ripple: started if nothing runs; true once it has
 * played. */
bool screen_ripple_played(GameState *g);

/* A point of a window's shape: its pixel across and its line, as the
 * windows' tables hold their edges. */
typedef struct {
    uint8_t x, y;
} WindowPoint;

/* The searchlight's four corners, in drawing order, each moving from where it
 * starts to where it ends over a number of frames (event $FE). */
enum { SEARCHLIGHT_CORNERS = FIELD_SEARCHLIGHT_CORNERS_COUNT };
typedef struct {
    WindowPoint from[SEARCHLIGHT_CORNERS], to[SEARCHLIGHT_CORNERS];
} SearchlightMove;
void screen_searchlight_moves(GameState *g, const SearchlightMove *move, uint8_t frames);

/* How far a coordinate moves a frame to get from `from` to `to` in `frames`
 * frames, in 8.8 fixed point; frames 0 is no move the game makes. */
uint16_t screen_searchlight_speed(uint8_t from, uint8_t to, uint8_t frames);

/* The step the main loop runs ($C0:21E1), and its guard: an effect the port
 * draws as the original does. */
void field_move_screen_effect_on(Snes *s);
/* As C, with the guard's state half first: false where a step is the
 * machine's yet, or where the field closes into its scene, which is where
 * the program goes next. */
bool field_screen_effect_moved_on(GameState *g, Platform *pf);
bool field_screen_effect_can_move_on(Snes *s);

#endif
