/* The settings screen on the original's machine: its checked legacy adapters, the
 * bridges that run what game/menu/settings_screen.c decides between the
 * calls that are still the original's, and their guards.
 *
 * Each step reads the cursor and asks settings_screen.c what the frame's
 * input does. The ticks are the cursor's sound, a sibling run as the
 * original's JSR reaches it (menu_run): it saves P as it starts and calls
 * the sound driver, so the flags the original has at each tick are on the
 * stack there, where shadow mode compares them, and A's high byte goes to
 * the driver. The options' handling is an entry of its own, run as the
 * original's JSR from the options' step reaches it, and it runs each
 * option's handler the same way through a C table (the original's JSR
 * through its table at $C2:BFFF): the stereo's handler sends the sound
 * driver the mode, and the meters' and the design's step their meter
 * through a sibling of their own that ticks. Going back from the options
 * calls the main menu's return as C behind its guard, and the panel waits a frame
 * between its frame and the rest (the menus' task switch).
 *
 * Before each of those calls a bridge leaves the registers and flags the
 * original's own instructions left there, worked out from the state (the
 * leaves at the end of the file): scaffolding, which the native build
 * compiles out. */
#include "menu/settings_screen.h"

#include "charge.h"
#include "menu/cursor.h"
#include "menu/main_menu.h"
#include "menu/menu_data.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "platform/audio.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The calls out ------------------------------------------------------------------------------- */

enum {
    MAIN_MENU = 0xC29A98, /* back to the main menu */
    CURSOR_SOUND = 0xC2EAC2, SOUND_DRIVER = 0xC70004,
};

/* The sound driver told the mode the player chose (stereo or mono), from
 * the original's call, as C and on the machine. */
enum { SOUND_OUTPUT = 0xF3 };

void menu_settings_send_sound_output(GameState *g, Platform *pf)
{
    au_command(pf, PF_CALL(0xC2C033, SOUND_DRIVER),
               (AudioCommand){ .command = SOUND_OUTPUT, .p1 = menu_settings_sound_output(g), .count = 1 });
}

/* ---- The guards ---------------------------------------------------------------------------------- */

static bool place_among(uint8_t place, uint8_t first, uint8_t count) { return place >= first && place < first + count; }

bool menu_settings_on_an_option(const GameState *g)
{
    return place_among(menu_cursor_index(g), 0, SETTINGS_OPTION_COUNT) && menu_cursor_kept_among(g, 0, SETTINGS_OPTION_COUNT, false)
           && menu_settings_boxes(g, SETTINGS_CHOOSING_GAUGE) == 0 && menu_settings_cursor(g) < SETTINGS_OPTION_COUNT;
}

bool menu_settings_on_a_layout(const GameState *g)
{
    return place_among(menu_cursor_index(g), SETTINGS_FIRST_GAUGE_PLACE, SETTINGS_GAUGE_LAYOUTS)
           && menu_cursor_kept_among(g, SETTINGS_FIRST_GAUGE_PLACE, SETTINGS_GAUGE_LAYOUTS, false)
           && menu_settings_cursor(g) < SETTINGS_OPTION_COUNT;
}

bool menu_settings_on_a_button_row(const GameState *g)
{
    return place_among(menu_cursor_index(g), SETTINGS_FIRST_BUTTON_ROW, MENU_SETTINGS_BUTTON_ROWS)
           && menu_cursor_kept_among(g, SETTINGS_FIRST_BUTTON_ROW, MENU_SETTINGS_BUTTON_ROWS, true) && menu_zero(g) == 0
           && menu_settings_cursor(g) < SETTINGS_OPTION_COUNT;
}

/* The description's typing got no further than its row, a tile at a time. */
bool menu_settings_describable(const GameState *g)
{
    uint16_t typed = menu_description_line_from(g);
    return typed < 0x40 && !(typed & 1);
}

bool menu_settings_can_open(const GameState *g)
{
    return menu_screen_step(g) == SETTINGS_OPENING && menu_settings_cursor(g) < SETTINGS_OPTION_COUNT
           && menu_window_list_drawable(g, 0xC1F6);
}

static bool options_step(const GameState *g) { return menu_settings_on_an_option(g) && menu_settings_describable(g); }

bool menu_settings_can_choose(const GameState *g) { return options_step(g) && menu_cursor_can_move(g); }

bool menu_settings_option_can_be_taken(const GameState *g)
{
    return place_among(menu_cursor_index(g), 0, SETTINGS_OPTION_COUNT) && menu_settings_boxes(g, SETTINGS_CHOOSING_GAUGE) == 0
           && menu_settings_describable(g);
}

static bool buttons_step(const GameState *g) { return menu_settings_on_a_button_row(g); }

static bool gauge_step(const GameState *g) { return menu_settings_on_a_layout(g) && menu_settings_describable(g); }

bool menu_settings_can_choose_buttons(const GameState *g) { return buttons_step(g); }
bool menu_settings_can_choose_a_layout(const GameState *g) { return gauge_step(g); }
