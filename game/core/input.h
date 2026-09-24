/* The controller as the game sees it (core/input.c): which buttons are held,
 * which were just pressed, which are repeating, and the same through the
 * player's button assignments. */
#ifndef CT_GAME_CORE_INPUT_H
#define CT_GAME_CORE_INPUT_H

#include "platform/input.h"
#include "snes/system.h"

/* A set of buttons in the game's own order, the order of the words it keeps
 * them in (input_held and the rest): the face and shoulder buttons in the low
 * byte, the direction pad in the high. The controller's order is PadBits
 * (platform/input.h). */
typedef uint16_t Buttons;
enum {
    BUTTON_A = 0x0080, BUTTON_X = 0x0040, BUTTON_L = 0x0020, BUTTON_R = 0x0010,
    BUTTON_B = 0x0008, BUTTON_Y = 0x0004, BUTTON_SELECT = 0x0002, BUTTON_START = 0x0001,
    BUTTON_UP = 0x0800, BUTTON_DOWN = 0x0400, BUTTON_LEFT = 0x0200, BUTTON_RIGHT = 0x0100,
};

/* The sets kept through the player's button assignments (input_mapped). */
enum { ASSIGNED_PRESSED, ASSIGNED_HELD, ASSIGNED_REPEATING };

/* The buttons (PadBits) that, held together and alone, reset the console, from any mode that
 * reads the controller (the scenes check them too, scene/frame.c). */
enum { RESET_BUTTONS = IN_BUTTON_L | IN_BUTTON_R | IN_BUTTON_SELECT | IN_BUTTON_START };

/* The registered entries (game/registry.c). */
void input_update(Snes *s);
void input_read_joypad(Snes *s);
void input_map_buttons(Snes *s);
bool input_map_buttons_runs(Snes *s);
void play_time_tick(Snes *s);

#endif
