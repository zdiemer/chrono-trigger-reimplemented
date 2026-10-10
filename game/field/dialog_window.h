/* Field mode: the dialog window (field/dialog_window.c).
 *
 * A script's line of dialog, or the field's own message, is shown in a window
 * at the top or the bottom of the screen, a page of up to four lines at a
 * time, typed a glyph a frame. What starts one is the scripts' commands
 * (event/dialog.c) and the field's chests; the field's main loop moves it on
 * a step a frame (field_dialog_window_step), its vblank sends the text the
 * step set (field_send_dialog_text), and a question's choices take the pad
 * while the player picks (field_take_choice_buttons). */
#ifndef CT_GAME_FIELD_DIALOG_WINDOW_H
#define CT_GAME_FIELD_DIALOG_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* Where a dialog is (dialog_state). Clearing the text takes four frames, a
 * row of it a frame, and so does clearing it for the next page. */
typedef enum {
    DIALOG_IDLE = 0,
    DIALOG_CLEARS_THE_TEXT = 1,           /* to 4 */
    DIALOG_OPENS_THE_WINDOW = 5,
    DIALOG_SETS_TEXT = 6,
    DIALOG_IS_READ = 7,                   /* the player reads it, or answers its question */
    DIALOG_CLEARS_THE_NEXT_PAGE = 8,      /* to 11 */
    DIALOG_SETS_THE_NEXT_PAGE = 12,
    DIALOG_CLOSES = 13,
} DialogState;

/* A dialog starts, for a script or for the field's own message (the window
 * opens, clears its text and sets the line). */
void dialog_begin(GameState *g);
/* The dialog ends from outside it: the object its script waits on left view,
 * or the chest whose message the field opened left the screen. The window
 * closes. */
void dialog_end(GameState *g);
/* The window shut at once, for a menu (the dialog starts over after it) or a
 * battle (it is gone). */
void dialog_shut_to_start_over(GameState *g);
void dialog_shut(GameState *g);
/* No dialog at all, as the field starts. */
void dialog_forget(GameState *g);

/* How long the window stays once its text is set (field_dialog_hold): frames,
 * or until the player presses the confirm button, which the window then takes
 * for itself (event/contact.c). */
enum { DIALOG_HOLD_UNTIL_CONFIRMED = 0xFFFF };

/* The choice pointer: the line of a question's choices it points at (0-3),
 * taken once the player has picked, or none. */
enum { DIALOG_CHOICE_TAKEN = 4, DIALOG_NO_CHOICE_POINTER = 0x80 };

// $C0:1F87
void field_dialog_window_step(GameState *g);
// $C0:1ADF
void field_take_choice_buttons(GameState *g, uint8_t choosing);
// $C0:6ECB
void field_send_dialog_text(GameState *g, Platform *pf);
// $C0:F05E
void field_refresh_choice_pointer(GameState *g, Platform *pf);
/* Whether the lines' first tiles are to be refreshed: while a choice pointer
 * is shown or has just been taken. */
bool field_choice_pointer_shown(const GameState *g);

/* The window's looks (field/dialog_window.c): its frame's tile map and its
 * text area's, built in the staging buffer ($C0:29F7, $C0:2B78) and sent
 * ($C0:6EB0, $C0:6EF1); the design the player chose, its tiles ($C0:6E5C) and
 * its colours ($C0:70E9); the choice pointer's tiles ($C0:6F5E); and the
 * field's two sprites of its own ($C0:6F0C). */
void field_build_the_window_frame(GameState *g);
void field_build_the_text_area(GameState *g);
void field_send_the_window_frame(GameState *g, Platform *pf);
void field_send_the_text_area(GameState *g, Platform *pf);
void field_send_the_window_design(GameState *g, Platform *pf);
void field_colour_the_window(GameState *g);
void field_send_the_choice_pointer(GameState *g, Platform *pf);
void field_send_the_fields_own_sprites(GameState *g, Platform *pf);
enum { FIELD_BUILD_WINDOW_FRAME_ENTRY = 0xC029F7, FIELD_BUILD_TEXT_AREA_ENTRY = 0xC02B78,
       FIELD_SEND_WINDOW_FRAME_ENTRY = 0xC06EB0, FIELD_SEND_TEXT_AREA_ENTRY = 0xC06EF1,
       FIELD_SEND_WINDOW_DESIGN_ENTRY = 0xC06E5C, FIELD_SEND_CHOICE_POINTER_ENTRY = 0xC06F5E };
void field_build_the_window_frame_entry(Snes *s);
void field_build_the_text_area_entry(Snes *s);
void field_send_the_window_frame_entry(Snes *s);
void field_send_the_text_area_entry(Snes *s);
void field_send_the_window_design_entry(Snes *s);
void field_send_the_choice_pointer_entry(Snes *s);

enum { FIELD_DIALOG_WINDOW_STEP_ENTRY = 0xC01F87, FIELD_TAKE_CHOICE_BUTTONS_ENTRY = 0xC01ADF,
       FIELD_SEND_DIALOG_TEXT_ENTRY = 0xC06ECB, FIELD_REFRESH_CHOICE_POINTER_ENTRY = 0xC0F05E };

/* The entries (game/registry.c), and their guards. */
void field_dialog_window_step_entry(Snes *s);
bool field_dialog_window_can_step(Snes *s);
void field_take_choice_buttons_entry(Snes *s);
void field_send_dialog_text_entry(Snes *s);
void field_refresh_choice_pointer_entry(Snes *s);
void field_draw_choice_line_0(Snes *s);
void field_draw_choice_line_1(Snes *s);
void field_draw_choice_line_2(Snes *s);
void field_draw_choice_line_3(Snes *s);
bool field_choice_lines_dp_is_ppu(Snes *s);

#endif
