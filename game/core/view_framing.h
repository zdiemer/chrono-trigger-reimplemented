/* A smaller view's framing (a handheld's screen, vid_view_is_smaller): the
 * camera that follows the action across the original's screen, and the
 * panels kept in view over it.
 *
 * The original's screen is 256x224; a smaller view shows a part of it. Each
 * mode that frames one says, each vblank, what it is about -- the field's
 * party leader, a battle's combatants -- and which of its panels must stay
 * whole -- the dialog window, a battle's windows -- and the camera moves to
 * keep the first comfortably in the view's lines the panels leave open,
 * gliding rather than jumping. Nothing here is the game's: it reads the
 * state outside the game's time and keeps nothing beside it (the camera's
 * place is the view's own, vid_view_aim), so the game plays as at the
 * original's view. On the original's view, or a wider one, it does nothing. */
#ifndef CT_GAME_CORE_VIEW_FRAMING_H
#define CT_GAME_CORE_VIEW_FRAMING_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/video.h"

/* A point of the original's screen the camera keeps in frame. */
typedef struct {
    int16_t x, y;
} ViewFocus;

/* The middle of the original's screen, which a mode with nothing to follow frames. */
static inline ViewFocus view_screen_middle(void)
{
    return (ViewFocus){ VID_ORIGINAL_VIEW_WIDTH / 2, VID_ORIGINAL_VIEW_HEIGHT / 2 };
}

/* Some of the view's lines: from its line `at`, `lines` of them; and the
 * screen's lines from `clear_of` to short of `clear_to` they keep clear of
 * (a panel's own lines on the screen, which the view shows elsewhere). */
typedef struct {
    int16_t at;
    uint16_t lines;
    int16_t clear_of, clear_to;
} ViewLines;

/* All of a view's lines. */
static inline ViewLines view_all_lines(VidView view) { return (ViewLines){ 0, view.height, 0, 0 }; }

/* The camera moved toward keeping `focus` in the middle third of the view
 * across, and of `open` down: it moves only as far as that needs, a quarter
 * of the way a frame (at least a dot), held to the original's screen and
 * where it can, to `open`'s lines clear of the screen's it keeps clear of. */
void view_follow(Platform *pf, ViewFocus focus, ViewLines open);

/* The camera moved toward putting `focus` in the middle of the view across
 * and of `open` down, gliding as above. */
void view_centre_on(Platform *pf, ViewFocus focus, ViewLines open);

/* The screen's lines from `top`, `lines` of them, kept in view from the
 * view's line `at`, the view's middle across on the original's. */
void view_keep_lines(Platform *pf, int16_t top, uint16_t lines, int16_t at);
/* The same, but for the screen's dots short of its X `column_end`, which
 * show its lines from `column_top`: a column laid out by its own lines
 * beside the rest of the screen. */
void view_keep_lines_beside(Platform *pf, int16_t top, uint16_t lines, int16_t at, int16_t column_top,
                            int16_t column_end);
/* The same, the screen's lines whole from its left side, but for the dots
 * from `cut_from` across, `cut_dots` of them, cut from them: a panel the
 * original's screen's width shown narrower by a run of its insides. */
void view_keep_lines_cut(Platform *pf, int16_t top, uint16_t lines, int16_t at, int16_t cut_from, uint16_t cut_dots);

/* The view's lines from `at`, `lines` of them, black, over the camera's
 * picture: what a layout of bands leaves over. */
void view_keep_nothing(Platform *pf, int16_t at, uint16_t lines);

#endif
