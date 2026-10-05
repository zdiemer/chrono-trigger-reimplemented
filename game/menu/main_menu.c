/* The main menu: menu screen 0, the one the menus open on from the field.
 *
 * Along its top is a row of six icons, an option each -- the equipment, the
 * items, the techs, the settings, the party exchange and saving -- the one
 * the cursor is on lit, with an arrow above it and one below. Under them is
 * the option's panel, a view of its screen (the equipment's member list
 * with the time played, the party's items, ...), which the option's own
 * screen draws and puts away as the cursor comes and goes
 * (menu/main_menu_bridge.c runs them). Behind it all is a backdrop of the
 * main menu's own tiles. Choosing an option opens its screen, and going back
 * closes the menus; a screen the player goes back from comes back to the
 * main menu's choosing, its panel put away.
 *
 * Where the player doesn't keep the menus' cursors (a setting), the main
 * menu starts every screen's cursors afresh as it opens and as its cursor
 * moves to another option. */
#include "menu/main_menu.h"

#include "charge.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/text.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
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

/* For the original's code and shadow mode: it repeats the tiles with block
 * moves. */
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
    ORIGINAL_EFFECT(charge_the_repeats(g));
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
enum { ARROW_TILE = 0x22, ARROW_BELOW_ATTRIBUTES = 0x3E, ARROW_ABOVE_ATTRIBUTES = 0xBE, ARROW_RIGHT = 1, ARROW_LINES = 16 };

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
    ORIGINAL_EFFECT(charge_block_move(g, 4 * MENU_MAIN_MENU_OPTIONS + 2 * MENU_ARROW_COLOURS));
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
    ORIGINAL_EFFECT(charge_block_move(g, 4 * MENU_MAIN_MENU_COLOURS));
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

/* For the original's code and shadow mode: a cell it clears here, which
 * nothing reads. */
static void leave_spare_cleared(GameState *g) { set_menu_main_menu_spare(g, 0); }

void menu_main_menu_move_to(GameState *g, MainMenuOption option)
{
    set_menu_main_menu_option(g, option);
    if (!keeps_cursors(g))
        start_the_screens_afresh(g);
    set_menu_exchange_cursor_kept(g, 0);
    set_menu_reserve_top(g, 0);
    TIMED_AS(g, 0xC28255, menu_stop_tasks(g, MENU_PANELS_TASKS));
    ORIGINAL_EFFECT(leave_spare_cleared(g));
}

/* An option's screen opens from its own window, with the screen's colours
 * (the palette's ninth row) and its cursor reading the buttons only from
 * its first step's next frame. */
enum { SCREEN_COLOURS = 0x80, SCREEN_COLOURS_COUNT = 16, NO_STEP = 0xFF };

void menu_main_menu_open_option(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC2ED31, menu_draw_window_list(g, pf, MENU_WINDOWS_OPTION_OPENING));
    set_menu_screen(g, (uint8_t)(menu_main_menu_option(g) + 1));
    set_menu_screen_step(g, 0);
    for (unsigned i = 0; i < SCREEN_COLOURS_COUNT; i++)
        set_menu_palette(g, SCREEN_COLOURS + i, menu_rom_colour(g, MENU_ROM_ROW_8_COLOURS, i));
    ORIGINAL_EFFECT(charge_block_move(g, 2 * SCREEN_COLOURS_COUNT));
    set_menu_cursor_step_seen(g, NO_STEP);
}

/* Back from a screen: to the main menu's choosing, the screen's tasks
 * stopped. */
enum { CHOOSING = 1 };

void menu_main_menu_come_back(GameState *g)
{
    set_menu_screen(g, MAIN_MENU_SCREEN);
    set_menu_screen_step(g, CHOOSING);
    TIMED_AS(g, 0xC28255, menu_stop_tasks(g, MENU_PANELS_TASKS));
}
