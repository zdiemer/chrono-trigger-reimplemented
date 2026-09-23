/* Scripts reading the controller: a cutscene waiting for a button, a
 * minigame reading the pad.
 *
 * Two views of the buttons: as held this frame, in the game's order
 * (A X L R B Y Select Start) or remapped through the player's own button
 * assignments; and as pressed, which the field latches as presses arrive
 * (field_input_latched: remapped in its low byte, in the game's order in its
 * high byte) and a script takes, so one press answers one question.
 *
 * Each is a condition: on after it if the button is down, otherwise its jump. */
#include "event/script.h"

/* The game's order, in the low byte of the held buttons. */
enum { BUTTON_A = 0x80, BUTTON_X = 0x40, BUTTON_L = 0x20, BUTTON_R = 0x10, BUTTON_B = 0x08, BUTTON_Y = 0x04 };

/* A press, as latched: remapped buttons in the low byte, the game's order in
 * the high one. */
#define PRESSED(button) ((uint16_t)((button) << 8))

enum { HELD = 1 };   /* input_mapped: the held buttons, remapped */

// $C0:66C6
/* If a button is held. */
static EventFlow if_held(GameState *g, uint8_t buttons) { return unless_jump((uint8_t)input_held(g) & buttons); }

// $C0:66A5
/* If a remapped button is held (any of them, asking for all). */
static EventFlow if_held_remapped(GameState *g, uint16_t buttons)
{
    return unless_jump((input_mapped(g, HELD) & buttons) != 0);
}

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

static EventFlow run_if_held(GameState *g, const EventArgs *a) { return if_held(g, a->arg[0]); }
static EventFlow run_if_held_remapped(GameState *g, const EventArgs *a) { return if_held_remapped(g, a->arg[0]); }
static EventFlow run_if_pressed(GameState *g, const EventArgs *a) { return if_pressed(g, a->arg[0]); }

/* One operand, the jump. The original's parse stops on it when the button is
 * down, before it when not. */
#define BUTTON_ROW(opcode, run, buttons, routine_) \
    { opcode, run, { CONSTANT(buttons) }, 2, 1, .routine = routine_, .parse = 2, .parse_on_jump = 1 }

EVENT_OPCODES(event_input_opcodes,
    BUTTON_ROW(0x2D, run_if_held_remapped, 0xFFFF, 0xC066A5),
    BUTTON_ROW(0x30, run_if_held_remapped, 0x02, 0xC066B2),
    BUTTON_ROW(0x31, run_if_held_remapped, 0x80, 0xC066BC),
    BUTTON_ROW(0x34, run_if_held, BUTTON_A, 0xC066C6),
    BUTTON_ROW(0x35, run_if_held, BUTTON_B, 0xC066E5),
    BUTTON_ROW(0x36, run_if_held, BUTTON_X, 0xC066EF),
    BUTTON_ROW(0x37, run_if_held, BUTTON_Y, 0xC066F9),
    BUTTON_ROW(0x38, run_if_held, BUTTON_L, 0xC06705),
    BUTTON_ROW(0x39, run_if_held, BUTTON_R, 0xC0670F),
    BUTTON_ROW(0x3B, run_if_pressed, 0x02, 0xC06719),
    BUTTON_ROW(0x3C, run_if_pressed, 0x80, 0xC06724),
    BUTTON_ROW(0x3F, run_if_pressed, PRESSED(BUTTON_A), 0xC06732),
    BUTTON_ROW(0x40, run_if_pressed, PRESSED(BUTTON_B), 0xC06740),
    BUTTON_ROW(0x41, run_if_pressed, PRESSED(BUTTON_X), 0xC0674E),
    BUTTON_ROW(0x42, run_if_pressed, PRESSED(BUTTON_Y), 0xC0675C),
    BUTTON_ROW(0x43, run_if_pressed, PRESSED(BUTTON_L), 0xC0676A),
    BUTTON_ROW(0x44, run_if_pressed, PRESSED(BUTTON_R), 0xC06778));
