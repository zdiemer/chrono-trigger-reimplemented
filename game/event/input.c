/* Event commands: conditions on the controller.
 *
 * They read the controller cells the joypad reader keeps (game/core/input.c):
 * the buttons held this frame, in the game's order (A X L R B Y Select Start),
 * and the same remapped through the player's button assignments. And the
 * field's latches (field_input_latched), which the field main loop ORs the
 * newly pressed buttons into every frame, remapped in its low byte and in the
 * game's order in its high byte: a latched button stays set until a command
 * that tests it takes it.
 *
 * Each takes one operand, a jump distance: if the condition holds the script
 * goes on after it, otherwise it jumps forward by it from the operand. */
#include "event/bridge.h"

/* The game's order, in the low byte of the button words. */
enum { BUTTON_A = 0x80, BUTTON_X = 0x40, BUTTON_L = 0x20, BUTTON_R = 0x10, BUTTON_B = 0x08, BUTTON_Y = 0x04 };

enum { HELD = 1 };   /* input_mapped: the held buttons, remapped */

// $C0:66D0
/* Held: on after the operand, the parse having stepped onto it; otherwise
 * jump by it. */
static EventStep branch(GameState *g, const EventCommand *c, bool holds)
{
    if (holds)
        return next_command(c, 2, 2);
    return jump_from(g, c, 1, operand(g, c, 1), false, 1);
}

static EventStep if_held(GameState *g, const EventCommand *c, uint8_t button)
{
    return branch(g, c, (uint8_t)input_held(g) & button);
}

static EventStep if_mapped_held(GameState *g, const EventCommand *c, uint8_t bit)
{
    return branch(g, c, (uint8_t)input_mapped(g, HELD) & bit);
}

/* Takes a latched button (clearing it) if it is set. */
static bool take_latched(GameState *g, uint16_t bit)
{
    uint16_t latched = field_input_latched(g);
    if (!(latched & bit))
        return false;
    set_field_input_latched(g, (uint16_t)(latched & ~bit));
    return true;
}

static EventStep if_latched_mapped(GameState *g, const EventCommand *c, uint8_t bit)
{
    return branch(g, c, take_latched(g, bit));
}

static EventStep if_pressed(GameState *g, const EventCommand *c, uint8_t button)
{
    return branch(g, c, take_latched(g, (uint16_t)(button << 8)));
}

// $C0:66A5
/* $2D: if any button is held, remapped or on the direction pad. */
static EventStep if_any_input(GameState *g, const EventCommand *c) { return branch(g, c, input_mapped(g, HELD) != 0); }

// $C0:66B2
/* $30: if the remapped held buttons' bit 1 is set. */
static EventStep if_mapped_held_bit1(GameState *g, const EventCommand *c) { return if_mapped_held(g, c, 0x02); }

// $C0:66BC
/* $31: if the remapped held buttons' bit 7 is set. */
static EventStep if_mapped_held_bit7(GameState *g, const EventCommand *c) { return if_mapped_held(g, c, 0x80); }

// $C0:66C6
/* $34: if A is held. */
static EventStep if_a_held(GameState *g, const EventCommand *c) { return if_held(g, c, BUTTON_A); }

// $C0:66E5
/* $35: if B is held. */
static EventStep if_b_held(GameState *g, const EventCommand *c) { return if_held(g, c, BUTTON_B); }

// $C0:66EF
/* $36: if X is held. */
static EventStep if_x_held(GameState *g, const EventCommand *c) { return if_held(g, c, BUTTON_X); }

// $C0:66F9
/* $37: if Y is held. */
static EventStep if_y_held(GameState *g, const EventCommand *c) { return if_held(g, c, BUTTON_Y); }

// $C0:6705
/* $38: if L is held. */
static EventStep if_l_held(GameState *g, const EventCommand *c) { return if_held(g, c, BUTTON_L); }

// $C0:670F
/* $39: if R is held. */
static EventStep if_r_held(GameState *g, const EventCommand *c) { return if_held(g, c, BUTTON_R); }

// $C0:6719
/* $3B: take a latched press of the remapped bit 1. */
static EventStep if_mapped_pressed_bit1(GameState *g, const EventCommand *c) { return if_latched_mapped(g, c, 0x02); }

// $C0:6724
/* $3C: take a latched press of the remapped bit 7. */
static EventStep if_mapped_pressed_bit7(GameState *g, const EventCommand *c) { return if_latched_mapped(g, c, 0x80); }

// $C0:6732
/* $3F: take a latched press of A. */
static EventStep if_a_pressed(GameState *g, const EventCommand *c) { return if_pressed(g, c, BUTTON_A); }

// $C0:6740
/* $40: take a latched press of B. */
static EventStep if_b_pressed(GameState *g, const EventCommand *c) { return if_pressed(g, c, BUTTON_B); }

// $C0:674E
/* $41: take a latched press of X. */
static EventStep if_x_pressed(GameState *g, const EventCommand *c) { return if_pressed(g, c, BUTTON_X); }

// $C0:675C
/* $42: take a latched press of Y. */
static EventStep if_y_pressed(GameState *g, const EventCommand *c) { return if_pressed(g, c, BUTTON_Y); }

// $C0:676A
/* $43: take a latched press of L. */
static EventStep if_l_pressed(GameState *g, const EventCommand *c) { return if_pressed(g, c, BUTTON_L); }

// $C0:6778
/* $44: take a latched press of R. */
static EventStep if_r_pressed(GameState *g, const EventCommand *c) { return if_pressed(g, c, BUTTON_R); }

EVENT_COMMAND(event_cmd_if_any_input, if_any_input)
EVENT_COMMAND(event_cmd_if_mapped_held_bit1, if_mapped_held_bit1)
EVENT_COMMAND(event_cmd_if_mapped_held_bit7, if_mapped_held_bit7)
EVENT_COMMAND(event_cmd_if_a_held, if_a_held)
EVENT_COMMAND(event_cmd_if_b_held, if_b_held)
EVENT_COMMAND(event_cmd_if_x_held, if_x_held)
EVENT_COMMAND(event_cmd_if_y_held, if_y_held)
EVENT_COMMAND(event_cmd_if_l_held, if_l_held)
EVENT_COMMAND(event_cmd_if_r_held, if_r_held)
EVENT_COMMAND(event_cmd_if_mapped_pressed_bit1, if_mapped_pressed_bit1)
EVENT_COMMAND(event_cmd_if_mapped_pressed_bit7, if_mapped_pressed_bit7)
EVENT_COMMAND(event_cmd_if_a_pressed, if_a_pressed)
EVENT_COMMAND(event_cmd_if_b_pressed, if_b_pressed)
EVENT_COMMAND(event_cmd_if_x_pressed, if_x_pressed)
EVENT_COMMAND(event_cmd_if_y_pressed, if_y_pressed)
EVENT_COMMAND(event_cmd_if_l_pressed, if_l_pressed)
EVENT_COMMAND(event_cmd_if_r_pressed, if_r_pressed)
