/* Scripts talking: a line of dialog in a window, a question with choices,
 * and the answer.
 *
 * An object's script shows a line and waits on it until the player has read
 * it and the window has closed. A question is a line with two to four of its
 * lines offered as choices; the choice made becomes the object's result,
 * which its script can then test. Where the window goes -- where the game
 * decides, the top or the bottom of the screen -- is part of the command. */
#include "event/leaves.h"
#include "event/script.h"

/* ---- The dialog window ---------------------------------------------------------------- */

typedef enum { WINDOW_ANYWHERE = 0, WINDOW_TOP = 1, WINDOW_BOTTOM = 2 } WindowPlace;

enum { DIALOG_IDLE = 0, DIALOG_OPENING = 1 };
/* field_mode_flags: set when the field opens a dialog itself ($C0:1F0E), and
 * cleared when a script does. */
enum { DIALOG_FROM_THE_FIELD = 0x20 };

static bool dialog_on_screen(const GameState *g) { return dialog_state(g) != DIALOG_IDLE; }

/* Open the window on a line for an object, which waits on it. */
static void open_dialog(GameState *g, EventObject speaker, uint8_t line, WindowPlace where)
{
    set_event_object_wait_dialog(g, speaker, 1);
    set_dialog_string(g, line);
    set_dialog_speaker(g, (uint8_t)(speaker * 2));
    set_dialog_window_object(g, 0);   /* the dialog places its window by the speaker once it starts */
    set_dialog_state(g, DIALOG_OPENING);
    set_dialog_box_position(g, where);
    set_field_mode_flags(g, (uint8_t)(field_mode_flags(g) & ~DIALOG_FROM_THE_FIELD));
}

/* Whether the object's dialog has been read and closed: it opened one and
 * the window is gone. The object stops waiting on it then. */
static bool finished_reading(GameState *g, EventObject speaker)
{
    if (!event_object_wait_dialog(g, speaker))
        return false;
    set_event_object_wait_dialog(g, speaker, 0);
    return true;
}

/* The choices a question offers, as the script gives them: its first choice
 * line in bits 0-1, its last in bits 2-3. */
static void offer_choices(GameState *g, uint8_t lines)
{
    set_dialog_choice_first(g, lines & 0x03);
    set_dialog_choice_last(g, (lines >> 2) & 0x03);
    set_dialog_choice_active(g, 1);
}

/* An object's result: the last answer given to its question, or a value a
 * script set. A byte of it is the answer. */
static void set_answer(GameState *g, EventObject object, uint8_t answer)
{
    set_event_object_result(g, object, (uint16_t)((event_object_result(g, object) & 0xFF00) | answer));
}

/* ---- The commands ------------------------------------------------------------------------ */

// $C0:3557
/* Choose the text the map's lines come from: a table of strings anywhere in
 * the ROM, which the dialog commands name lines of. */
static EventFlow use_text(GameState *g, uint32_t table)
{
    set_dialog_string_table(g, table);
    return next_command();
}

// $C0:3570
/* Show a line of dialog, and wait until it has been read. */
static EventFlow say(GameState *g, EventObject self, uint8_t line, WindowPlace where)
{
    if (dialog_on_screen(g))
        return stay();
    if (finished_reading(g, self))
        return next_command();
    open_dialog(g, self, line, where);
    return stay();
}

// $C0:3643
/* Ask a question: a line with choices, and wait for the answer, which
 * becomes the object's result. */
static EventFlow ask(GameState *g, EventObject self, uint8_t line, uint8_t choices, WindowPlace where)
{
    if (dialog_on_screen(g))
        return stay();
    if (finished_reading(g, self)) {
        set_answer(g, self, dialog_choice(g));
        set_dialog_choice_active(g, 0);
        return next_command();
    }
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, choices));
    offer_choices(g, choices);
    open_dialog(g, self, line, where);
    return stay();
}

// $C0:653B
/* Set the object's result, from a variable or from the story. */
static EventFlow set_result(GameState *g, EventObject self, uint16_t value)
{
    set_event_object_result(g, self, value);
    return next_command();
}

// $C0:6568
/* If the object's answer is the one given. */
static EventFlow if_answer(GameState *g, EventObject self, uint8_t answer)
{
    return unless_jump((uint8_t)event_object_result(g, self) == answer);
}

static EventFlow run_use_text(GameState *g, const EventArgs *a) { return use_text(g, a->arg[0]); }
static EventFlow run_say(GameState *g, const EventArgs *a) { return say(g, a->self, a->arg[0], a->arg[1]); }
static EventFlow run_ask(GameState *g, const EventArgs *a) { return ask(g, a->self, a->arg[0], a->arg[1], a->arg[2]); }
static EventFlow run_set_result(GameState *g, const EventArgs *a) { return set_result(g, a->self, a->arg[0]); }
static EventFlow run_if_answer(GameState *g, const EventArgs *a) { return if_answer(g, a->self, a->arg[0]); }

/* The guard's part: without a table of text the original goes to an error
 * handler, which is its own business. */
static bool text_chosen(const GameState *g, uint16_t at)
{
    (void)at;
    return (dialog_string_table(g) >> 16) != 0;
}

EVENT_OPCODES(event_dialog_opcodes,
    { 0xB8, run_use_text, { ADDRESS(1) }, 4, .routine = 0xC03557 },
    { 0xBB, run_say, { BYTE(1), CONSTANT(WINDOW_ANYWHERE) }, 2, .accepts = text_chosen, .routine = 0xC03570 },
    { 0xC1, run_say, { BYTE(1), CONSTANT(WINDOW_TOP) }, 2, .accepts = text_chosen, .routine = 0xC035B5 },
    { 0xC2, run_say, { BYTE(1), CONSTANT(WINDOW_BOTTOM) }, 2, .accepts = text_chosen, .routine = 0xC035FC },
    { 0xC0, run_ask, { BYTE(1), BYTE(2), CONSTANT(WINDOW_ANYWHERE) }, 3, .accepts = text_chosen,
      .routine = 0xC03643 },
    { 0xC3, run_ask, { BYTE(1), BYTE(2), CONSTANT(WINDOW_TOP) }, 3, .accepts = text_chosen, .routine = 0xC036AA },
    { 0xC4, run_ask, { BYTE(1), BYTE(2), CONSTANT(WINDOW_BOTTOM) }, 3, .accepts = text_chosen,
      .routine = 0xC036DD },
    { 0x19, run_set_result, { VARIABLE_WORD(1) }, 2, .routine = 0xC0653B },
    { 0x1C, run_set_result, { STORY_WORD(1) }, 3, .routine = 0xC0654E },
    { 0x1A, run_if_answer, { BYTE(1) }, 3, 2, .routine = 0xC06568 });
