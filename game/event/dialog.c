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

enum { DIALOG_IDLE = 0, DIALOG_OPENING = 1 };
/* field_mode_flags: set when the field opens a dialog itself ($C0:1F0E), and
 * cleared when a script does. */
enum { DIALOG_FROM_THE_FIELD = 0x20 };

static bool dialog_on_screen(const GameState *g) { return dialog_state(g) != DIALOG_IDLE; }

/* Open the window on a line for an object, which waits on it. */
static void open_dialog(GameState *g, FieldObject speaker, uint8_t line, WindowPlace where)
{
    set_event_object_wait_dialog(g, speaker, 1);
    set_dialog_string(g, line);
    set_dialog_speaker(g, field_link_byte(field_link_to(speaker)));
    set_dialog_window_object(g, 0);   /* the dialog places its window by the speaker once it starts */
    set_dialog_state(g, DIALOG_OPENING);
    set_dialog_box_position(g, where);
    set_field_mode_flags(g, (uint8_t)(field_mode_flags(g) & ~DIALOG_FROM_THE_FIELD));
}

/* Whether the object's dialog has been read and closed: it opened one and
 * the window is gone. The object stops waiting on it then. */
static bool finished_reading(GameState *g, FieldObject speaker)
{
    if (!event_object_wait_dialog(g, speaker))
        return false;
    set_event_object_wait_dialog(g, speaker, 0);
    return true;
}

/* The choices a question offers: its first choice line and its last. */
static void offer_choices(GameState *g, uint8_t first_choice, uint8_t last_choice)
{
    set_dialog_choice_first(g, first_choice);
    set_dialog_choice_last(g, last_choice);
    set_dialog_choice_active(g, 1);
}

/* An object's result: the last answer given to its question, or a value a
 * script set. A byte of it is the answer. */
static void set_answer(GameState *g, FieldObject object, uint8_t answer)
{
    set_event_object_result(g, object, (uint16_t)((event_object_result(g, object) & 0xFF00) | answer));
}

/* ---- The commands ------------------------------------------------------------------------ */

// $C0:3557
/* Choose the text the map's lines come from: a table of strings anywhere in
 * the ROM, which the dialog commands name lines of. */
static EventFlow use_text(GameState *g, Address text)
{
    set_dialog_string_table(g, text);
    return next_command();
}
EVENT_OPERATION(use_text, text)

// $C0:3570
/* Show a line of dialog, and wait until it has been read. */
static EventFlow say(GameState *g, FieldObject self, uint8_t line, WindowPlace window)
{
    if (dialog_on_screen(g))
        return stay();
    if (finished_reading(g, self))
        return next_command();
    open_dialog(g, self, line, window);
    return stay();
}
EVENT_OPERATION(say, self, line, window)

/* For the original's code and shadow mode: the original keeps the choices'
 * operand byte in its first scratch cell as it opens the window. */
static void keep_choices(GameState *g) { keep_byte(g, KEPT_FIRST, script_byte(g, (uint16_t)(event_leaves->at + 2))); }

// $C0:3643
/* Ask a question: a line with choices, and wait for the answer, which
 * becomes the object's result. */
static EventFlow ask(GameState *g, FieldObject self, uint8_t line, uint8_t first_choice, uint8_t last_choice,
                     WindowPlace window)
{
    if (dialog_on_screen(g))
        return stay();
    if (finished_reading(g, self)) {
        set_answer(g, self, dialog_choice(g));
        set_dialog_choice_active(g, 0);
        return next_command();
    }
    ORIGINAL_EFFECT(keep_choices(g));
    offer_choices(g, first_choice, last_choice);
    open_dialog(g, self, line, window);
    return stay();
}
EVENT_OPERATION(ask, self, line, first_choice, last_choice, window)

// $C0:653B
/* Set the object's result, from a variable or from the story. */
static EventFlow set_result(GameState *g, FieldObject self, uint16_t value)
{
    set_event_object_result(g, self, value);
    return next_command();
}
EVENT_OPERATION(set_result, self, value)

// $C0:6568
/* If the object's answer is the one given. */
static EventFlow if_answer(GameState *g, FieldObject self, uint8_t answer)
{
    return unless_jump((uint8_t)event_object_result(g, self) == answer);
}
EVENT_OPERATION(if_answer, self, answer)

/* The guard's part: without a table of text the original goes to an error
 * handler, which is its own business. */
static bool text_chosen(const GameState *g)
{
    return (dialog_string_table(g) >> 16) != 0;
}
EVENT_CHECK_TAKING_NOTHING(text_chosen)

/* A question's choices, as the script gives them: its first choice line in
 * bits 0-1 of the operand, its last in bits 2-3. */
#define CHOICES .first_choice = OPERAND_BITS(2, 0, 0x03), .last_choice = OPERAND_BITS(2, 2, 0x03)

EVENT_OPCODES(event_dialog_opcodes,
    { 0xB8, OPERATION(use_text), .text = OPERAND_ADDRESS(1), .length = 4 },
    { 0xBB, OPERATION(say), .line = OPERAND(1), .window = CONSTANT(WINDOW_ANYWHERE), .length = 2, CHECK(text_chosen) },
    { 0xC1, OPERATION(say), .line = OPERAND(1), .window = CONSTANT(WINDOW_TOP), .length = 2, CHECK(text_chosen) },
    { 0xC2, OPERATION(say), .line = OPERAND(1), .window = CONSTANT(WINDOW_BOTTOM), .length = 2, CHECK(text_chosen) },
    { 0xC0, OPERATION(ask), .line = OPERAND(1), CHOICES, .window = CONSTANT(WINDOW_ANYWHERE), .length = 3,
      CHECK(text_chosen) },
    { 0xC3, OPERATION(ask), .line = OPERAND(1), CHOICES, .window = CONSTANT(WINDOW_TOP), .length = 3,
      CHECK(text_chosen) },
    { 0xC4, OPERATION(ask), .line = OPERAND(1), CHOICES, .window = CONSTANT(WINDOW_BOTTOM), .length = 3,
      CHECK(text_chosen) },
    { 0x19, OPERATION(set_result), .value = VARIABLE_WORD(1), .length = 2 },
    { 0x1C, OPERATION(set_result), .value = STORY_WORD(1), .length = 3 },
    { 0x1A, OPERATION(if_answer), .answer = OPERAND(1), .length = 3, .jump = 2 });
