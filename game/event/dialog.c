/* Scripts talking: a line of dialog in a window, a question with choices,
 * and the answer.
 *
 * An object's script shows a line and waits on it until the player has read
 * it and the window has closed. A question is a line with two to four of its
 * lines offered as choices; the choice made becomes the object's result,
 * which its script can then test. Where the window goes -- where the game
 * decides, the top or the bottom of the screen -- is part of the command. */
#include "event/dialog.h"

#include "event/leaves.h"
#include "event/script.h"
#include "field/dialog_window.h"
#include "field/field.h"

/* ---- The dialog window ---------------------------------------------------------------- */

bool dialog_on_screen(const GameState *g) { return dialog_state(g) != DIALOG_IDLE; }

/* The window opens on a line for a speaker. */
static void show_the_window(GameState *g, FieldObject speaker, uint8_t line, WindowPlace where)
{
    set_dialog_string(g, line);
    dialog_set_speaker(g, speaker);
    set_dialog_window_object(g, 0);   /* the dialog places its window by the speaker once it starts */
    dialog_begin(g);
    set_dialog_box_position(g, where);
}

/* Open the window on a line for an object, which waits on it. */
static void open_dialog(GameState *g, FieldObject speaker, uint8_t line, WindowPlace where)
{
    set_event_object_wait_dialog(g, speaker, 1);
    show_the_window(g, speaker, line, where);
    forget_dialog_opened_by_the_field(g);
}

// $C0:1F0E
/* The field's own message, a chest's: it clears the mark the battle's
 * messages clear too. */
void field_message_opens(GameState *g, FieldObject speaker, uint8_t line)
{
    show_the_window(g, speaker, line, WINDOW_ANYWHERE);
    mark_dialog_opened_by_the_field(g);
    set_field_message_mark(g, 0);
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
    set_dialog_choice_active(g, DIALOG_CHOICES_SHOWING);
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

/* For event/leaves.c, before a question runs: whether it opens its window
 * (no dialog on screen, and the object not waiting on one it opened), where
 * the original keeps its choices' operand. */
bool event_question_opens(const GameState *g, FieldObject self)
{
    return !dialog_on_screen(g) && !event_object_wait_dialog(g, self);
}

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
        set_dialog_choice_active(g, DIALOG_NO_CHOICES);
        return next_command();
    }
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

/* A question's choices, as the script gives them: its last choice line in
 * bits 0-1 of the operand, its first in bits 2-3. */
#define CHOICES .first_choice = OPERAND_BITS(2, 2, 0x03), .last_choice = OPERAND_BITS(2, 0, 0x03)

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
