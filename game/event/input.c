/* Scripts reading the controller: a cutscene waiting for a button, a
 * minigame reading the pad.
 *
 * Two views of the buttons: as held this frame, in the game's order
 * (A X L R B Y Select Start) or through the player's own button assignments
 * (input_mapped); and as pressed, which the field latches as presses arrive
 * (field_input_latched: the assignments in its low byte, the game's order in
 * its high byte) and a script takes, so that one press answers one question.
 *
 * Each is a condition: on after it if the button is down, otherwise its jump. */
#include "event/script.h"

/* The buttons, in the game's order, as the held buttons have them. */
enum { BUTTON_A = 0x80, BUTTON_X = 0x40, BUTTON_L = 0x20, BUTTON_R = 0x10, BUTTON_B = 0x08, BUTTON_Y = 0x04 };

/* The player's button assignments, as the remapped buttons have them: each
 * bit is set while all the buttons of one assignment are down. The scripts
 * ask about the first assignment and the fourth (game/core/input.c orders
 * them); what each is for in the game isn't established (docs/TODO.md). */
enum { FIRST_ASSIGNMENT = 0x80, FOURTH_ASSIGNMENT = 0x02, ANYTHING = 0xFFFF };

/* A press as the field latches it: the game's order in the high byte. */
#define PRESSED(button) ((uint16_t)((button) << 8))

enum { HELD = 1 };   /* input_mapped: the held buttons, through the assignments */

// $C0:66C6
/* If a button is held. */
static EventFlow if_held(GameState *g, uint16_t buttons)
{
    return unless_jump((input_held(g) & buttons) != 0);
}
EVENT_OPERATION(if_held, buttons)

// $C0:66A5
/* If an assignment's buttons are held (or, for any, anything at all, the
 * direction pad included). */
static EventFlow if_assignment_held(GameState *g, uint16_t buttons)
{
    return unless_jump((input_mapped(g, HELD) & buttons) != 0);
}
EVENT_OPERATION(if_assignment_held, buttons)

// $C0:6732
/* If a button was pressed since a script last asked: the press is taken, so
 * the next question needs a new one. */
static EventFlow if_pressed(GameState *g, uint16_t buttons)
{
    uint16_t latched = field_input_latched(g);
    if (!(latched & buttons))
        return take_jump();
    set_field_input_latched(g, (uint16_t)(latched & ~buttons));
    return next_command();
}
EVENT_OPERATION(if_pressed, buttons)

/* Each has one operand, its jump. */
EVENT_OPCODES(event_input_opcodes,
    { 0x2D, OPERATION(if_assignment_held), .buttons = CONSTANT(ANYTHING), .length = 2, .jump = 1 },
    { 0x30, OPERATION(if_assignment_held), .buttons = CONSTANT(FOURTH_ASSIGNMENT), .length = 2, .jump = 1 },
    { 0x31, OPERATION(if_assignment_held), .buttons = CONSTANT(FIRST_ASSIGNMENT), .length = 2, .jump = 1 },
    { 0x34, OPERATION(if_held), .buttons = CONSTANT(BUTTON_A), .length = 2, .jump = 1 },
    { 0x35, OPERATION(if_held), .buttons = CONSTANT(BUTTON_B), .length = 2, .jump = 1 },
    { 0x36, OPERATION(if_held), .buttons = CONSTANT(BUTTON_X), .length = 2, .jump = 1 },
    { 0x37, OPERATION(if_held), .buttons = CONSTANT(BUTTON_Y), .length = 2, .jump = 1 },
    { 0x38, OPERATION(if_held), .buttons = CONSTANT(BUTTON_L), .length = 2, .jump = 1 },
    { 0x39, OPERATION(if_held), .buttons = CONSTANT(BUTTON_R), .length = 2, .jump = 1 },
    { 0x3B, OPERATION(if_pressed), .buttons = CONSTANT(FOURTH_ASSIGNMENT), .length = 2, .jump = 1 },
    { 0x3C, OPERATION(if_pressed), .buttons = CONSTANT(FIRST_ASSIGNMENT), .length = 2, .jump = 1 },
    { 0x3F, OPERATION(if_pressed), .buttons = CONSTANT(PRESSED(BUTTON_A)), .length = 2, .jump = 1 },
    { 0x40, OPERATION(if_pressed), .buttons = CONSTANT(PRESSED(BUTTON_B)), .length = 2, .jump = 1 },
    { 0x41, OPERATION(if_pressed), .buttons = CONSTANT(PRESSED(BUTTON_X)), .length = 2, .jump = 1 },
    { 0x42, OPERATION(if_pressed), .buttons = CONSTANT(PRESSED(BUTTON_Y)), .length = 2, .jump = 1 },
    { 0x43, OPERATION(if_pressed), .buttons = CONSTANT(PRESSED(BUTTON_L)), .length = 2, .jump = 1 },
    { 0x44, OPERATION(if_pressed), .buttons = CONSTANT(PRESSED(BUTTON_R)), .length = 2, .jump = 1 });
