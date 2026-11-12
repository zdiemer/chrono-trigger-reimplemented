/* The settings screen's sound output sent to the sound driver, and what its
 * steps need of the game's state before they run: the cursor on an option,
 * a gauge layout or a button row, and the description typed no further than
 * its row. The steps (game/menu/settings_screen.c) go no further where one
 * fails. */
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

/* ---- The sound output -------------------------------------------------------------------------- */

enum { SOUND_DRIVER = 0xC70004 };

/* The sound driver told the mode the player chose (stereo or mono). */
enum { SOUND_OUTPUT = 0xF3 };

void menu_settings_send_sound_output(GameState *g, Platform *pf)
{
    au_command(pf, PF_CALL(0xC2C033, SOUND_DRIVER),
               (AudioCommand){ .command = SOUND_OUTPUT, .p1 = menu_settings_sound_output(g), .count = 1 });
}

/* ---- The steps --------------------------------------------------------------------------------- */

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
