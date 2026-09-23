/* Event commands: dialog.
 *
 * A dialog command opens a text window on its first run: it marks the object
 * as waiting on a dialog, names the string (by its index in the table $B8
 * set), the speaker (the running object) and where the window goes, and
 * starts the dialog (dialog_state 1). It stays on the command while a dialog
 * is running, and once the dialog has cleared dialog_state, the next run takes
 * the mark away and goes on. A dialog with choices also says which of its
 * lines are the choices, and on the way out copies the choice made into the
 * object's result value.
 *
 * With no string table the original goes to an error handler; the guard
 * leaves that to it. */
#include "event/bridge.h"

/* Where the window goes: 0 the game decides, 1 the top of the screen, 2 the bottom. */
enum { WINDOW_ANYWHERE = 0, WINDOW_TOP = 1, WINDOW_BOTTOM = 2 };
enum { DIALOG_STARTING = 1 };
enum { DIALOG_STARTED_BY_EVENT = 0x20 };   /* field_mode_flags */

// $C0:3557
/* $B8: set the dialog string table (operands 1-3, a 24-bit pointer). */
static EventStep set_dialog_table(GameState *g, const EventCommand *c)
{
    set_dialog_string_table(g, (uint32_t)operand_word(g, c, 1) | (uint32_t)operand(g, c, 3) << 16);
    return next_command(c, 4, 0);
}

/* Open the window on string `string` for the running object. */
static void start_dialog(GameState *g, uint8_t string, uint8_t window)
{
    set_event_object_wait_dialog(g, current_object(g), 1);
    set_dialog_string(g, string);
    set_dialog_speaker(g, (uint8_t)field_current_object(g));
    set_dialog_window_object(g, 0);
    set_dialog_state(g, DIALOG_STARTING);
    set_dialog_box_position(g, window);
    set_field_mode_flags(g, (uint8_t)(field_mode_flags(g) & ~DIALOG_STARTED_BY_EVENT));
}

/* Whether the running object's dialog is over: it opened one and dialog_state
 * is clear again. The wait mark is taken away then. */
static bool dialog_over(GameState *g)
{
    unsigned object = current_object(g);
    if (!event_object_wait_dialog(g, object))
        return false;
    set_event_object_wait_dialog(g, object, 0);
    return true;
}

// $C0:3570
/* $BB, $C1, $C2: show dialog string (operand 1). */
static EventStep dialog(GameState *g, const EventCommand *c, uint8_t window)
{
    if (dialog_state(g) != 0)
        return wait_here(c, 0);
    if (dialog_over(g))
        return next_command(c, 2, 0);
    start_dialog(g, operand(g, c, 1), window);
    return wait_here(c, 0);
}

// $C0:3643
/* $C0, $C3, $C4: dialog string (operand 1) with choices: operand 2's bits
 * 2-3 are its last choice line and bits 0-1 its first (the layout is kept in
 * the scratch cell). */
static EventStep choice(GameState *g, const EventCommand *c, uint8_t window)
{
    if (dialog_state(g) != 0)
        return wait_here(c, 0);
    if (dialog_over(g)) {
        unsigned object = current_object(g);
        set_event_object_result(g, object, (uint16_t)((event_object_result(g, object) & 0xFF00) | dialog_choice(g)));
        set_dialog_choice_active(g, 0);
        return next_command(c, 3, 0);
    }
    uint8_t string = operand(g, c, 1);
    uint8_t layout = operand(g, c, 2);
    keep8(g, SCRATCH_D9, layout);
    set_dialog_choice_first(g, layout & 0x03);
    set_dialog_choice_last(g, (layout >> 2) & 0x03);
    set_dialog_choice_active(g, 1);
    start_dialog(g, string, window);
    return wait_here(c, 0);
}

// $C0:3570
/* $BB: show dialog string (operand 1), where the game decides. */
static EventStep show_dialog(GameState *g, const EventCommand *c) { return dialog(g, c, WINDOW_ANYWHERE); }

// $C0:35B5
/* $C1: show dialog string (operand 1) at the top of the screen. */
static EventStep dialog_top(GameState *g, const EventCommand *c) { return dialog(g, c, WINDOW_TOP); }

// $C0:35FC
/* $C2: show dialog string (operand 1) at the bottom of the screen. */
static EventStep dialog_bottom(GameState *g, const EventCommand *c) { return dialog(g, c, WINDOW_BOTTOM); }

// $C0:3643
/* $C0: dialog with choices, where the game decides. */
static EventStep show_choice(GameState *g, const EventCommand *c) { return choice(g, c, WINDOW_ANYWHERE); }

// $C0:36AA
/* $C3: dialog with choices, at the top of the screen. */
static EventStep choice_top(GameState *g, const EventCommand *c) { return choice(g, c, WINDOW_TOP); }

// $C0:36DD
/* $C4: dialog with choices, at the bottom of the screen. */
static EventStep choice_bottom(GameState *g, const EventCommand *c) { return choice(g, c, WINDOW_BOTTOM); }

EVENT_COMMAND(event_cmd_set_dialog_table, set_dialog_table)
EVENT_COMMAND(event_cmd_dialog, show_dialog)
EVENT_COMMAND(event_cmd_dialog_top, dialog_top)
EVENT_COMMAND(event_cmd_dialog_bottom, dialog_bottom)
EVENT_COMMAND(event_cmd_choice, show_choice)
EVENT_COMMAND(event_cmd_choice_top, choice_top)
EVENT_COMMAND(event_cmd_choice_bottom, choice_bottom)
