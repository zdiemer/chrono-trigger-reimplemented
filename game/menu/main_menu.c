/* The main menu: menu screen 0, the one the menus open on from the field.
 *
 * Along its top is a row of six icons, an option each -- the equipment, the
 * items, the techs, the settings, the party exchange and saving -- the one
 * the cursor is on lit, with an arrow above it and one below. Under them is
 * the option's panel, a view of its screen (the equipment's member list
 * with the time played, the party's items, ...), which the option's own
 * screen draws and puts away as the cursor comes and goes. Behind it all is a backdrop of the
 * main menu's own tiles. Choosing an option opens its screen, and going back
 * closes the menus; a screen the player goes back from comes back to the
 * main menu's choosing, its panel put away.
 *
 * Where the player doesn't keep the menus' cursors (a setting), the main
 * menu starts every screen's cursors afresh as it opens and as its cursor
 * moves to another option. */
#include "menu/main_menu.h"

#include "charge.h"
#include "core/program.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "menu/equipment.h"
#include "menu/exchange.h"
#include "menu/fades.h"
#include "menu/item_screen.h"
#include "menu/members.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/saving.h"
#include "menu/settings_screen.h"
#include "menu/tech_screen.h"
#include "menu/text.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "vars.h"

/* ---- The backdrop -------------------------------------------------------------------------------- */

/* The back layer is tiled all over with a pattern of four of the main
 * menu's tiles, two by two, on its first screen of 32 rows of 32. */
enum { TILES_A_ROW = 32, BACKDROP_ROWS = 32, BACKDROP_TILES = 0x1FA0 };

static uint16_t backdrop_tile(unsigned row, unsigned column) { return (uint16_t)(BACKDROP_TILES + (row & 1) * 2 + (column & 1)); }

/* The box: a frame of the main menu's tiles around a filled inside, sixteen
 * tiles across and six rows down; its four middle rows are its second,
 * the whole row across (what lies past the box with it). */
enum { BOX_COLUMNS = 16, BOX_ROWS = 6 };
typedef struct {
    uint16_t left, inside, right;
} BoxRow;
static const BoxRow BOX_TOP = { 0x3FA4, 0x3FA5, 0x3FA6 };
static const BoxRow BOX_MIDDLE = { 0x3FA7, 0x3FAC, 0x3FA8 };
static const BoxRow BOX_BOTTOM = { 0x3FA9, 0x3FAA, 0x3FAB };

static void lay_box_row(GameState *g, unsigned row, BoxRow r)
{
    set_menu_main_menu_box(g, row * TILES_A_ROW, r.left);
    for (unsigned column = 1; column < BOX_COLUMNS - 1; column++)
        set_menu_main_menu_box(g, row * TILES_A_ROW + column, r.inside);
    set_menu_main_menu_box(g, row * TILES_A_ROW + BOX_COLUMNS - 1, r.right);
}

/* The original repeats the tiles with block moves; their time is charged. */
enum { ROW_BYTES = 2 * TILES_A_ROW, BOX_REPEAT_BYTES = 2 * (BOX_COLUMNS - 3) };
static void charge_the_repeats(GameState *g)
{
    charge_block_move(g, 3 * BOX_REPEAT_BYTES + 3 * ROW_BYTES);
    charge_block_move(g, 2 * (ROW_BYTES - 4) + (BACKDROP_ROWS - 2) * ROW_BYTES);
}

// $C2:9AAD
void menu_draw_main_menu_backdrop(GameState *g)
{
    lay_box_row(g, 0, BOX_TOP);
    lay_box_row(g, 1, BOX_MIDDLE);
    lay_box_row(g, BOX_ROWS - 1, BOX_BOTTOM);
    for (unsigned row = 2; row < BOX_ROWS - 1; row++)
        for (unsigned column = 0; column < TILES_A_ROW; column++)
            set_menu_main_menu_box(g, row * TILES_A_ROW + column, menu_main_menu_box(g, TILES_A_ROW + column));
    for (unsigned row = 0; row < BACKDROP_ROWS; row++)
        for (unsigned column = 0; column < TILES_A_ROW; column++)
            set_menu_back_tilemap(g, row * TILES_A_ROW + column, backdrop_tile(row, column));
    charge_the_repeats(g);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER_WHOLE);
}

/* ---- The main menu's window and icons ------------------------------------------------------------ */

/* The per-line effects the main menu runs on (the colour math, slots 0 and
 * 1), and its band table, which the vblank copies into place. */
enum { MAIN_MENU_LINE_EFFECTS = 0x03, BANDS_STAGED = 0x9750, BANDS = 0x9080, BANDS_BYTES = 0x1E };

/* The icons are the first six sprites; the arrows below and above the lit
 * one the next two, the one above flipped upside down. An arrow stands a
 * pixel right of the icon, sixteen lines below it or above it. */
enum { ARROW_BELOW = MENU_MAIN_MENU_OPTIONS, ARROW_ABOVE = MENU_MAIN_MENU_OPTIONS + 1 };
enum {
    ARROW_TILE = 0x22,
    ARROW_BELOW_ATTRIBUTES = 0x3E,
    ARROW_ABOVE_ATTRIBUTES = 0xBE,
    ARROW_RIGHT = 1,
    ARROW_LINES = 16,
};

/* The arrows' colours go to the last three of the palette. */
enum { PALETTE_COLOURS = 256, ICON_COLOURS = 0x80 };

static void put_icon(GameState *g, unsigned sprite, uint8_t x, uint8_t y, uint8_t tile, uint8_t attributes)
{
    set_oam_buffer_x(g, sprite, x);
    set_oam_buffer_y(g, sprite, y);
    set_oam_buffer_tile(g, sprite, tile);
    set_oam_buffer_attributes(g, sprite, attributes);
}

// $C2:9BDE
/* The icons, the cursor's lit, and the arrows at it. */
static void put_the_icons(GameState *g, uint8_t option)
{
    for (unsigned o = 0; o < MENU_MAIN_MENU_OPTIONS; o++) {
        MenuIcon icon = menu_main_menu_icon(g, o);
        put_icon(g, o, icon.x, icon.y, icon.tile, icon.attributes);
    }
    MenuIcon lit = menu_main_menu_icon(g, option);
    set_oam_buffer_tile(g, option, (uint8_t)(lit.tile + MENU_LIT_ICON_TILES));
    uint8_t x = (uint8_t)(lit.x + ARROW_RIGHT);
    put_icon(g, ARROW_BELOW, x, (uint8_t)(lit.y + ARROW_LINES), ARROW_TILE, ARROW_BELOW_ATTRIBUTES);
    put_icon(g, ARROW_ABOVE, x, (uint8_t)(lit.y - ARROW_LINES), ARROW_TILE, ARROW_ABOVE_ATTRIBUTES);
    for (unsigned i = 0; i < MENU_ARROW_COLOURS; i++)
        set_menu_palette(g, PALETTE_COLOURS - MENU_ARROW_COLOURS + i, menu_rom_colour(g, MENU_ROM_ARROW_COLOURS, i));
    charge_block_move(g, 4 * MENU_MAIN_MENU_OPTIONS + 2 * MENU_ARROW_COLOURS);
}

/* The main menu's windows fit the layers and its colours the frame's
 * changes, so it can be drawn. */
bool menu_main_menu_drawable(const GameState *g)
{
    return menu_window_list_drawable(g, MENU_WINDOWS_MAIN_MENU) && menu_palette_changes_have_room(g);
}

// $C2:9B8F
/* The main menu's window, its effects, the cursor on the option, the
 * icons' colours with the lit option's, and the icons. */
void menu_draw_main_menu(GameState *g, Platform *pf)
{
    uint8_t option = menu_main_menu_option(g);
    TIMED_AS(g, 0xC2ED31, menu_draw_window_list(g, pf, MENU_WINDOWS_MAIN_MENU));
    set_menu_line_effects(g, (uint8_t)(menu_line_effects(g) | MAIN_MENU_LINE_EFFECTS));
    set_menu_block_move_source(g, BANDS_STAGED);
    set_menu_block_move_dest(g, BANDS);
    set_menu_block_move_size(g, BANDS_BYTES);
    menu_ask_for_the_block_move(g);
    set_menu_cursor_index(g, option);
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    for (unsigned i = 0; i < MENU_MAIN_MENU_COLOURS; i++) {
        set_menu_palette(g, ICON_COLOURS + i, menu_rom_colour(g, MENU_ROM_MAIN_MENU_COLOURS, i));
        set_menu_palette(g, ICON_COLOURS + MENU_MAIN_MENU_COLOURS + i, menu_option_colour(g, option, i));
    }
    charge_block_move(g, 4 * MENU_MAIN_MENU_COLOURS);
    put_the_icons(g, option);
}

/* ---- The cursors the screens keep ---------------------------------------------------------------- */

static bool keeps_cursors(const GameState *g) { return menu_setting_keep_menu_cursors(g) != 0; }

/* Each screen's cursor back at its start: the items screen's on the list's
 * first row, the equipment's and the techs' on their first member, the
 * settings' on the first option, the techs' on the members' own techs. */
static void start_the_screens_afresh(GameState *g)
{
    set_menu_item_list_row(g, 0);
    set_menu_item_list_top(g, 0);
    set_menu_member_row(g, 0);
    set_menu_member_scroll(g, 0);
    set_menu_settings_cursor(g, 0);
    set_menu_tech_category(g, 0);
    set_menu_tech_member_row(g, 0);
    set_menu_tech_member_scroll(g, 0);
}

/* A member list keeps the row of the member the cursor was on: scrolled
 * into the first three rows where it still fits there, otherwise the
 * first; the list scrolled back to its start. */
enum { MEMBER_ROWS = 3 };

static uint8_t row_unscrolled(uint8_t row, uint8_t scroll)
{
    uint8_t member = (uint8_t)(row + scroll);
    return member < MEMBER_ROWS ? member : 0;
}

// $C2:99CE
bool menu_main_menu_open(GameState *g)
{
    set_menu_exchange_cursor_kept(g, 0);
    set_menu_reserve_top(g, 0);
    if (menu_member_count(g) == 0)
        return false;
    if (!keeps_cursors(g)) {
        set_menu_main_menu_option(g, 0);
        set_menu_member_row(g, 0);
        set_menu_member_scroll(g, 0);
        set_menu_tech_member_scroll(g, 0);
    }
    set_menu_member_row(g, row_unscrolled(menu_member_row(g), menu_member_scroll(g)));
    set_menu_member_scroll(g, 0);
    set_menu_tech_member_row(g, row_unscrolled(menu_tech_member_row(g), menu_tech_member_scroll(g)));
    set_menu_tech_member_scroll(g, 0);
    TIMED_AS(g, 0xC29AAD, menu_draw_main_menu_backdrop(g));
    return true;
}

/* ---- Choosing ------------------------------------------------------------------------------------ */

MainMenuChoice menu_main_menu_choose(const GameState *g)
{
    if (menu_asked(g, MENU_CHOOSE))
        return MAIN_MENU_OPTION_CHOSEN;
    if (menu_asked(g, MENU_GO_BACK))
        return MAIN_MENU_LEFT;
    return menu_cursor_index(g) == menu_main_menu_option(g) ? MAIN_MENU_STAYED : MAIN_MENU_MOVED;
}

/* The equipment's panel shows the game's clock, which goes on as the
 * player looks at it. */
void menu_main_menu_keep_the_time(GameState *g)
{
    if (menu_main_menu_option(g) != MAIN_MENU_EQUIPMENT)
        return;
    menu_draw_the_clock(g);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

void menu_main_menu_move_to(GameState *g, MainMenuOption option)
{
    set_menu_main_menu_option(g, option);
    if (!keeps_cursors(g))
        start_the_screens_afresh(g);
    set_menu_exchange_cursor_kept(g, 0);
    set_menu_reserve_top(g, 0);
    TIMED_AS(g, 0xC28255, menu_stop_tasks(g, MENU_PANELS_TASKS));
}

/* An option's screen opens from its own window, with the screen's colours
 * (the palette's ninth row) and its cursor reading the buttons only from
 * its first step's next frame. */
enum { SCREEN_COLOURS = 0x80, SCREEN_COLOURS_COUNT = 16, NO_STEP = 0xFF };

void menu_main_menu_open_option(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC2ED31, menu_draw_window_list(g, pf, MENU_WINDOWS_OPTION_OPENING));
    set_menu_screen(g, (uint8_t)(MENU_SCREEN_EQUIPMENT + menu_main_menu_option(g)));
    set_menu_screen_step(g, 0);
    for (unsigned i = 0; i < SCREEN_COLOURS_COUNT; i++)
        set_menu_palette(g, SCREEN_COLOURS + i, menu_rom_colour(g, MENU_ROM_ROW_8_COLOURS, i));
    charge_block_move(g, 2 * SCREEN_COLOURS_COUNT);
    set_menu_cursor_step_seen(g, NO_STEP);
}

/* Back from a screen: to the main menu's choosing, the screen's tasks
 * stopped. */
enum { CHOOSING = 1 };

void menu_main_menu_come_back(GameState *g)
{
    set_menu_screen(g, MENU_SCREEN_MAIN_MENU);
    set_menu_screen_step(g, CHOOSING);
    TIMED_AS(g, 0xC28255, menu_stop_tasks(g, MENU_PANELS_TASKS));
}

/* ---- The panels ---------------------------------------------------------------------------------- */

/* Putting a panel away waits for nothing: each screen's own, timed as its
 * routine. */
static void put_the_option_s_panel_away(GameState *g, Platform *pf, MainMenuOption option)
{
    switch (option) {
    case MAIN_MENU_EQUIPMENT: TIMED_AS(g, 0xC2A101, menu_equip_panel_put_away(g)); break;
    case MAIN_MENU_ITEMS: TIMED_AS(g, 0xC2ADC3, menu_items_panel_put_away(g)); break;
    case MAIN_MENU_TECHS: TIMED_AS(g, 0xC2B960, menu_techs_panel_put_away(g, pf)); break;
    case MAIN_MENU_SETTINGS: TIMED_AS(g, 0xC2C397, menu_settings_panel_put_away(g)); break;
    case MAIN_MENU_EXCHANGE: TIMED_AS(g, 0xC2CAE6, menu_exchange_panel_put_away(g)); break;
    case MAIN_MENU_SAVING: TIMED_AS(g, 0xC2D0DB, menu_put_save_panel_away(g)); break;
    }
}

// $C2:9B71
/* The option's panel put away, the main menu over the screen again, and the
 * back layer queued. */
void menu_put_main_menu_panel_away(GameState *g, Platform *pf)
{
    put_the_option_s_panel_away(g, pf, (MainMenuOption)menu_main_menu_option(g));
    TIMED_AS(g, 0xC29B8F, menu_draw_main_menu(g, pf));
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
}

static bool option_panel(GameState *g, Platform *pf, MainMenuOption option);

// $C2:9B54
/* The option's panel, then the main menu over it. */
static bool show_main_menu_panel(GameState *g, Platform *pf)
{
    MainMenuOption option = (MainMenuOption)menu_main_menu_option(g);
    if (!option_panel(g, pf, option))
        return false;
    TIMED_AS(g, 0xC29B8F, menu_draw_main_menu(g, pf));
    return true;
}

/* The panel shown, where the state is one the showing handles. */
static bool show_the_panel(GameState *g, Platform *pf)
{
    if (!menu_main_menu_panel_can_show(g)) {
        program_breaks_a_contract(g, 0xC29B54, "a main menu panel the port doesn't show");
        return false;
    }
    bool shown = false;
    TIMED_AS(g, 0xC29B54, shown = show_main_menu_panel(g, pf));
    return shown;
}

/* ---- The steps ----------------------------------------------------------------------------------- */

// $C2:99CE
/* The menu opened, its panel, then on to choosing as the screen fades in;
 * with nobody in the party the menus close at once. */
static bool open_main_menu(GameState *g, Platform *pf)
{
    if (!menu_main_menu_open(g)) {
        menu_close(g, pf, false);
        return true;
    }
    if (!show_the_panel(g, pf))
        return false;
    set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) + 1));
    TIMED_AS(g, 0xC283B2, menu_start_the_fade_in(g));
    return true;
}

// $C2:9A1B
/* Choosing an option opens its screen and going back closes the menus; the
 * cursor moved to another option shows its panel, with a tick. */
static bool choose_on_main_menu(GameState *g, Platform *pf)
{
    set_menu_cursor_index(g, menu_main_menu_option(g));
    menu_read_the_cursor_for_a_step(g);
    switch (menu_main_menu_choose(g)) {
    case MAIN_MENU_OPTION_CHOSEN: menu_main_menu_open_option(g, pf); return true;
    case MAIN_MENU_LEFT: menu_close(g, pf, false); return true;
    case MAIN_MENU_STAYED: menu_main_menu_keep_the_time(g); return true;
    case MAIN_MENU_MOVED: break;
    }
    menu_main_menu_move_to(g, (MainMenuOption)menu_cursor_index(g));
    if (!show_the_panel(g, pf))
        return false;
    menu_tick(g, pf);
    return true;
}

// $C2:9A98
/* Back from a screen: its panel put away, the main menu chosen on again,
 * and a tick. */
static void back_to_the_main_menu(GameState *g, Platform *pf)
{
    menu_main_menu_come_back(g);
    TIMED_AS(g, 0xC29B71, menu_put_main_menu_panel_away(g, pf));
    menu_tick(g, pf);
}

/* ---- What the steps take ------------------------------------------------------------------------- */

static bool on_an_option(const GameState *g) { return menu_main_menu_option(g) < MENU_MAIN_MENU_OPTIONS; }

/* The menu opens on an option, or starts afresh on the first. */
bool menu_main_menu_can_open(const GameState *g)
{
    return on_an_option(g) || (menu_member_count(g) != 0 && menu_setting_keep_menu_cursors(g) == 0);
}

/* Choosing: the cursor on an option, and every push keeping it among the
 * six. */
bool menu_main_menu_can_choose(const GameState *g)
{
    return on_an_option(g) && menu_cursor_kept_among(g, 0, MENU_MAIN_MENU_OPTIONS, false) && menu_cursor_can_move(g);
}

static bool drawable_on_an_option(const GameState *g) { return on_an_option(g) && menu_main_menu_drawable(g); }

/* The option's panel can be shown by its screen, or put away (only the
 * techs' takes anything for that: room for its upload). */
static bool option_panel_can_show(const GameState *g, MainMenuOption option)
{
    switch (option) {
    case MAIN_MENU_EQUIPMENT: return menu_equipment_panel_can_show(g);
    case MAIN_MENU_ITEMS: return menu_items_panel_can_show(g);
    case MAIN_MENU_TECHS: return menu_techs_panel_can_show(g);
    case MAIN_MENU_SETTINGS: return menu_settings_panel_can_show(g);
    case MAIN_MENU_EXCHANGE: return menu_exchange_panel_can_show(g);
    case MAIN_MENU_SAVING: return menu_save_panel_can_show(g);
    }
    return false;
}

bool menu_main_menu_panel_can_show(const GameState *g)
{
    return drawable_on_an_option(g) && option_panel_can_show(g, (MainMenuOption)menu_main_menu_option(g));
}

bool menu_main_menu_can_come_back(const GameState *g)
{
    return drawable_on_an_option(g) && (menu_main_menu_option(g) != MAIN_MENU_TECHS || menu_techs_panel_can_go(g));
}

/* ---- The options' panels ------------------------------------------------------------------------ */

/* The panels shown, by option (show_the_panel has turned away the
 * others). */
static const struct {
    uint32_t routine;
    bool (*show)(GameState *g, Platform *pf);
} panels[MENU_MAIN_MENU_OPTIONS] = {
    [MAIN_MENU_EQUIPMENT] = { 0xC2A051, menu_show_equipment_panel },
    [MAIN_MENU_ITEMS] = { 0xC2AD50, menu_show_items_panel },
    [MAIN_MENU_TECHS] = { 0xC2B8DA, menu_show_techs_panel },
    [MAIN_MENU_SETTINGS] = { 0xC2C2FF, menu_show_settings_panel },
    [MAIN_MENU_EXCHANGE] = { 0xC2C9E5, menu_show_exchange_panel },
    [MAIN_MENU_SAVING] = { 0xC2D065, menu_show_save_panel },
};

static bool option_panel(GameState *g, Platform *pf, MainMenuOption option)
{
    bool shown = false;
    TIMED_AS(g, panels[option].routine, shown = panels[option].show(g, pf));
    return shown;
}

/* ---- From the main task -------------------------------------------------------------------------- */

/* A step, run only on the state it handles; otherwise the program goes no
 * further there. */
static bool not_taken(GameState *g, uint32_t routine)
{
    program_breaks_a_contract(g, routine, "a main menu the port doesn't take");
    return false;
}

bool menu_open_main_menu(GameState *g, Platform *pf)
{
    return menu_main_menu_can_open(g) ? open_main_menu(g, pf) : not_taken(g, 0xC299CE);
}

bool menu_choose_on_main_menu(GameState *g, Platform *pf)
{
    return menu_main_menu_can_choose(g) ? choose_on_main_menu(g, pf) : not_taken(g, 0xC29A1B);
}

static bool come_back(GameState *g, Platform *pf)
{
    if (!menu_main_menu_can_come_back(g))
        return not_taken(g, 0xC29A98);
    back_to_the_main_menu(g, pf);
    return true;
}

bool menu_back_to_the_main_menu(GameState *g, Platform *pf)
{
    bool back = false;
    TIMED_AS(g, 0xC29A98, back = come_back(g, pf));
    return back;
}
