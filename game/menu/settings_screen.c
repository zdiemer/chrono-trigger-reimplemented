/* The settings screen, and the main menu's panel for it.
 *
 * The screen is two windows. The left one holds ten options, each an icon
 * and a box: the sound output (stereo or mono), active time, whether the
 * menus keep their cursors, whether the battle keeps each member's command
 * and each member's lists, the speeds, the window design, the battle gauge,
 * the controls (the game's buttons or the player's own) and whether the
 * battle describes techs and items. The right one shows what the options
 * set: the battle's and the messages' speed on two meters of eight, the
 * window design on a third, the gauge's three layouts, and the buttons'
 * six assignments (confirm, cancel, the menu, dash, the map, warp) with the
 * button each is on. Under both, a description window types the option the
 * cursor is on and its value.
 *
 * The on-off options flip as they are chosen, their boxes with them. The
 * speeds, the window design and the gauge take the cursor into the right
 * window instead: onto the speeds' meters (left and right set a speed, and
 * choosing or going back returns), along the design's meter (the windows
 * take each design as the cursor reaches it), or among the gauge's layouts
 * (the gauge's step). While one is being set its box is ticked. The player's
 * own controls take the cursor to the button rows (the buttons' step):
 * left and right step the row's assignment through seven buttons, and the
 * assignment that had the button takes the row's; up and down keep the
 * change, Start puts the game's buttons back, and a push left leaves.
 *
 * The settings live in a cell each as the menus open (game/menu/opening.c
 * unpacks them, and the screen packs them back as it leaves); the boxes keep
 * what they show, and the buttons the rows show while the cursor is on them
 * are a copy of the player's own with the row's change in it, which up and
 * down keep. */
#include "menu/settings_screen.h"

#include "charge.h"
#include "core/program.h"
#include "menu/cursor.h"
#include "menu/description.h"
#include "menu/display.h"
#include "menu/graphics.h"
#include "menu/main_menu.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/opening.h"
#include "menu/tiles.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "vars.h"

/* ---- The settings' cells ------------------------------------------------------------------------ */

/* The settings, in the order of their cells as the menus unpack them, which
 * the boxes follow and the options' table in the ROM indexes. The byte after
 * the last is no setting, but the speeds option reads it for its value. */
typedef enum {
    CELL_SOUND_OUTPUT = 0, /* 1 for mono */
    CELL_OWN_BUTTONS = 1,
    CELL_KEEP_MENU_CURSORS = 2,
    CELL_ACTIVE_TIME = 3,
    CELL_BATTLE_GAUGE = 4,
    CELL_BATTLE_SPEED = 5,
    CELL_WINDOW_DESIGN = 6,
    CELL_KEEP_BATTLE_COMMAND = 7,
    CELL_KEEP_BATTLE_LISTS = 8,
    CELL_DESCRIPTIONS = 9,
    CELL_MESSAGE_SPEED = 10,
    CELL_PAST_THE_SETTINGS = 11,
} SettingCell;

static uint8_t setting(const GameState *g, SettingCell cell)
{
    switch (cell) {
    case CELL_SOUND_OUTPUT: return menu_setting_sound_output(g);
    case CELL_OWN_BUTTONS: return menu_setting_own_buttons(g);
    case CELL_KEEP_MENU_CURSORS: return menu_setting_keep_menu_cursors(g);
    case CELL_ACTIVE_TIME: return menu_setting_active_time(g);
    case CELL_BATTLE_GAUGE: return menu_setting_battle_gauge(g);
    case CELL_BATTLE_SPEED: return menu_setting_battle_speed(g);
    case CELL_WINDOW_DESIGN: return menu_window_design(g);
    case CELL_KEEP_BATTLE_COMMAND: return menu_setting_keep_battle_command(g);
    case CELL_KEEP_BATTLE_LISTS: return menu_setting_keep_battle_lists(g);
    case CELL_DESCRIPTIONS: return menu_setting_descriptions(g);
    case CELL_MESSAGE_SPEED: return menu_setting_message_speed(g);
    case CELL_PAST_THE_SETTINGS: return menu_setting_spare(g);
    }
    return 0;
}

static void set_setting(GameState *g, SettingCell cell, uint8_t v)
{
    switch (cell) {
    case CELL_SOUND_OUTPUT: set_menu_setting_sound_output(g, v); break;
    case CELL_OWN_BUTTONS: set_menu_setting_own_buttons(g, v); break;
    case CELL_KEEP_MENU_CURSORS: set_menu_setting_keep_menu_cursors(g, v); break;
    case CELL_ACTIVE_TIME: set_menu_setting_active_time(g, v); break;
    case CELL_BATTLE_GAUGE: set_menu_setting_battle_gauge(g, v); break;
    case CELL_BATTLE_SPEED: set_menu_setting_battle_speed(g, v); break;
    case CELL_WINDOW_DESIGN: set_menu_window_design(g, v); break;
    case CELL_KEEP_BATTLE_COMMAND: set_menu_setting_keep_battle_command(g, v); break;
    case CELL_KEEP_BATTLE_LISTS: set_menu_setting_keep_battle_lists(g, v); break;
    case CELL_DESCRIPTIONS: set_menu_setting_descriptions(g, v); break;
    case CELL_MESSAGE_SPEED: set_menu_setting_message_speed(g, v); break;
    case CELL_PAST_THE_SETTINGS: set_menu_setting_spare(g, v); break;
    }
}

/* The cell each option sets: an on-off option the one it flips, a meter the
 * one it steps. (The ROM's own table of the options' cells, $C2:C4F9, names
 * the cell an option's value is described from: menu_settings_option_cell.) */
static const SettingCell OPTION_CELLS[SETTINGS_OPTION_COUNT] = {
    [SETTINGS_STEREO] = CELL_SOUND_OUTPUT,
    [SETTINGS_ACTIVE_TIME] = CELL_ACTIVE_TIME,
    [SETTINGS_KEEP_MENU_CURSORS] = CELL_KEEP_MENU_CURSORS,
    [SETTINGS_KEEP_BATTLE_COMMAND] = CELL_KEEP_BATTLE_COMMAND,
    [SETTINGS_KEEP_BATTLE_LISTS] = CELL_KEEP_BATTLE_LISTS,
    [SETTINGS_SPEEDS] = CELL_PAST_THE_SETTINGS,
    [SETTINGS_WINDOW_DESIGN] = CELL_WINDOW_DESIGN,
    [SETTINGS_BATTLE_GAUGE] = CELL_BATTLE_GAUGE,
    [SETTINGS_CONTROLS] = CELL_OWN_BUTTONS,
    [SETTINGS_DESCRIPTIONS] = CELL_DESCRIPTIONS,
    [SETTINGS_BATTLE_SPEED] = CELL_BATTLE_SPEED,
    [SETTINGS_MESSAGE_SPEED] = CELL_MESSAGE_SPEED,
};

/* Which of its steps the screen's task runs next frame. */
static void go_to_step(GameState *g, SettingsStep step) { set_menu_screen_step(g, (uint8_t)step); }

/* ---- The boxes -------------------------------------------------------------------------------- */

/* The ten boxes stand for the settings' first ten cells, one each; where a
 * cell holds a number rather than an on-off (the gauge, the battle speed,
 * the window design) its box says instead whether the cursor is choosing
 * it. */
enum { BOX_CHOOSING_GAUGE = SETTINGS_CHOOSING_GAUGE, BOX_SETTING_SPEEDS = SETTINGS_SETTING_SPEEDS, BOX_CHOOSING_DESIGN = SETTINGS_CHOOSING_DESIGN };

static bool box_ticked(const GameState *g, unsigned box) { return menu_settings_boxes(g, box) != 0; }
static void tick_box(GameState *g, unsigned box, bool on) { set_menu_settings_boxes(g, box, on); }

static bool is_a_choosing_box(unsigned box)
{
    return box == BOX_CHOOSING_GAUGE || box == BOX_SETTING_SPEEDS || box == BOX_CHOOSING_DESIGN;
}

// $C2:C43A
/* The boxes as the settings stand, none of the settings being chosen. */
static void take_boxes_from_the_settings(GameState *g)
{
    for (unsigned box = 0; box < MENU_SETTINGS_BOXES; box++)
        tick_box(g, box, is_a_choosing_box(box) ? false : setting(g, (SettingCell)box) != 0);
    ORIGINAL_EFFECT(charge_block_move(g, MENU_SETTINGS_BOXES));
}

/* The front layer's tilemap, by where in work RAM a tile is, as the ROM's
 * tables give places. */
enum { TILE_BYTES = MENU_TILE_BYTES, ROW_BYTES = MENU_TILEMAP_ROW_BYTES };

static void put_tile(GameState *g, uint16_t at, uint16_t tile) { set_menu_front_tilemap(g, menu_front_tile_of(at), tile); }

/* A box is four tiles, two by two; the ticked box's four follow the empty
 * one's. */
enum { EMPTY_BOX = 0x1530, BOX_TILES = 4 };

static void put_box(GameState *g, uint16_t at, bool ticked)
{
    uint16_t first = (uint16_t)(EMPTY_BOX + (ticked ? BOX_TILES : 0));
    put_tile(g, at, first);
    put_tile(g, (uint16_t)(at + TILE_BYTES), (uint16_t)(first + 1));
    put_tile(g, (uint16_t)(at + ROW_BYTES), (uint16_t)(first + 2));
    put_tile(g, (uint16_t)(at + ROW_BYTES + TILE_BYTES), (uint16_t)(first + 3));
}

// $C2:C3E4
/* Each box drawn as it is ticked; the sound output's ticks for stereo,
 * which its cell holds as 0. */
static void draw_boxes(GameState *g)
{
    for (unsigned box = 0; box < MENU_SETTINGS_BOXES; box++)
        put_box(g, menu_settings_box_place(g, (uint8_t)box), box_ticked(g, box) != menu_settings_box_ticked_for_off(g, (uint8_t)box));
    ORIGINAL_EFFECT(set_menu_scratch_settings_boxes_left(g, 0xFFFF));
}

/* ---- The meters and the gauge --------------------------------------------------------------------- */

/* A meter is a row of eight numbers, dimmed but the one chosen; the gauge's
 * three layouts are a row of three choices three tiles wide. */
enum { DIMMED = 0x0C, LIT = 0x00 };
enum { BATTLE_SPEED_METER = 0x2EA6, MESSAGE_SPEED_METER = 0x2F66, DESIGN_METER = 0x2FE6, METER_STEPS = 8 };
enum { GAUGE_ROW = 0x3064, GAUGE_ROW_TILES = 9, GAUGE_CHOICE_TILES = 3, GAUGE_LAYOUT_BITS = 0x03 };

static uint16_t show_meter(GameState *g, uint16_t at, uint8_t value)
{
    TIMED_AS(g, 0xC2ECAC, menu_colour_row_pair(g, at, METER_STEPS, DIMMED));
    uint16_t lit = (uint16_t)(at + (value % METER_STEPS) * TILE_BYTES);
    TIMED_AS(g, 0xC2ECAC, menu_colour_row_pair(g, lit, 1, LIT));
    return lit;
}

// $C2:C456, $C2:C47A
static void show_meters(GameState *g)
{
    (void)show_meter(g, BATTLE_SPEED_METER, menu_setting_battle_speed(g));
    (void)show_meter(g, MESSAGE_SPEED_METER, menu_setting_message_speed(g));
    uint16_t lit = show_meter(g, DESIGN_METER, menu_window_design(g));
    set_menu_draw_origin(g, lit); /* the drawing goes on from the last tile lit */
    ORIGINAL_EFFECT(set_menu_scratch_settings_meter_cells(g, (uint16_t)(menu_window_design(g) | menu_setting_keep_battle_command(g) << 8)));
}

// $C2:C495
static void show_gauge(GameState *g)
{
    uint8_t layout = (uint8_t)(menu_setting_battle_gauge(g) & GAUGE_LAYOUT_BITS);
    TIMED_AS(g, 0xC2ECAC, menu_colour_row_pair(g, GAUGE_ROW, GAUGE_ROW_TILES, DIMMED));
    uint16_t lit = (uint16_t)(GAUGE_ROW + layout * GAUGE_CHOICE_TILES * TILE_BYTES);
    TIMED_AS(g, 0xC2ECAC, menu_colour_row_pair(g, lit, GAUGE_CHOICE_TILES, LIT));
    set_menu_draw_origin(g, lit);
    ORIGINAL_EFFECT(set_menu_scratch_settings_gauge_offset(g, (uint16_t)(layout * TILE_BYTES)));
}

/* ---- The buttons -------------------------------------------------------------------------------- */

/* Nine assignments: the six rows', the shoulders' two, which page lists one
 * way and the other, and a ninth that is copied with them and nothing
 * reads. */
enum { ASSIGNMENTS = 9, ROWS_ASSIGNMENTS = MENU_SETTINGS_BUTTON_ROWS, PAGE_ONE_WAY = SETTINGS_PAGE_ONE_WAY, PAGE_THE_OTHER = 7, CHANGE_MARKS = 8 };
enum { BUTTON_R = 0x10, BUTTON_L = 0x20 };

/* A button's glyph, by its bit from the top (none, the ninth). */
uint16_t menu_button_glyph_of(const GameState *g, uint8_t button)
{
    unsigned from_the_top = 0;
    while (from_the_top < 8 && !(button & (0x80u >> from_the_top)))
        from_the_top++;
    return menu_button_glyph(g, from_the_top);
}

// $C2:C555
/* The buttons in use: the player's own where the settings say, else the
 * game's. The original copies the game's first either way. */
static void use_the_settings_buttons(GameState *g)
{
    bool own = menu_setting_own_buttons(g) != 0;
    for (unsigned i = 0; i < ASSIGNMENTS; i++) {
        uint8_t button = own ? own_button_assignments(g, i) : menu_default_button_assignment(g, i);
        if (i < ASSIGNMENTS - 1)
            set_button_assignments(g, i, button);
        else
            set_button_assignment_spare(g, button);
    }
    ORIGINAL_EFFECT(charge_block_move(g, own ? 2 * ASSIGNMENTS : ASSIGNMENTS));
}

/* A row is the button's glyph, then an arrow and the button it is changing
 * to; with no change both are blank. */
enum { NO_CHANGE = 0xFFFF, CHANGE_ARROW = 0x0031, ROW_BLANK = 0x00FF, ARROW_AT = 2, CHANGE_AT = 4 };

static uint16_t row_place(const GameState *g, unsigned row)
{
    return (uint16_t)(MENU_FRONT_LAYER_AT + menu_settings_button_row_place(g, row));
}

// $C2:C57A
/* The rows as the buttons in use have them, no change shown. */
static void draw_button_rows(GameState *g)
{
    for (unsigned row = 0; row < ROWS_ASSIGNMENTS; row++) {
        uint16_t at = row_place(g, row);
        put_tile(g, at, menu_button_glyph_of(g, button_assignments(g, row)));
        put_tile(g, (uint16_t)(at + ARROW_AT), ROW_BLANK);
        put_tile(g, (uint16_t)(at + CHANGE_AT), ROW_BLANK);
    }
    ORIGINAL_EFFECT(set_menu_scratch_settings_rows_drawn(g, ROWS_ASSIGNMENTS));
}

void menu_settings_use_buttons(GameState *g)
{
    use_the_settings_buttons(g);
    draw_button_rows(g);
}

/* Whether a row shows a change: its button against the player's own; the
 * cursor's row against none, so always. */
enum { NO_BUTTON_KEPT = 0xFF };

static bool row_changes(const GameState *g, unsigned row, unsigned cursor_row)
{
    uint8_t before = row == cursor_row ? NO_BUTTON_KEPT : own_button_assignments(g, row);
    return menu_settings_buttons(g, row) != before;
}

/* The original compares the rows with a copy of the player's own buttons
 * whose cursor's row it marks, which nothing reads after. */
static void leave_buttons_before(GameState *g, unsigned cursor_row)
{
    for (unsigned i = 0; i < CHANGE_MARKS; i++)
        set_menu_settings_buttons_before(g, i, own_button_assignments(g, i));
    set_menu_settings_buttons_before(g, cursor_row, NO_BUTTON_KEPT);
    charge_block_move(g, CHANGE_MARKS);
}

// $C2:C5C7
/* The rows' changes: the arrow and the new button where a row is changing. */
static void show_row_changes(GameState *g, unsigned cursor_row)
{
    ORIGINAL_EFFECT(leave_buttons_before(g, cursor_row));
    for (unsigned row = 0; row < ROWS_ASSIGNMENTS; row++) {
        uint16_t at = row_place(g, row);
        bool changes = row_changes(g, row, cursor_row);
        put_tile(g, (uint16_t)(at + ARROW_AT), changes ? CHANGE_ARROW : NO_CHANGE);
        put_tile(g, (uint16_t)(at + CHANGE_AT), changes ? menu_button_glyph_of(g, menu_settings_buttons(g, row)) : NO_CHANGE);
    }
    ORIGINAL_EFFECT(set_menu_scratch_settings_rows_left(g, 0xFFFF));
    ORIGINAL_EFFECT(set_menu_scratch_settings_row_button(g, menu_settings_buttons(g, 0)));
    ORIGINAL_EFFECT(set_menu_scratch_settings_row_mark(g, row_changes(g, 0, cursor_row) ? CHANGE_ARROW : NO_CHANGE));
}

static void copy_own_to_rows(GameState *g)
{
    for (unsigned i = 0; i < ASSIGNMENTS; i++)
        set_menu_settings_buttons(g, i, own_button_assignments(g, i));
    ORIGINAL_EFFECT(charge_block_move(g, ASSIGNMENTS));
}

static void keep_rows_as_own(GameState *g)
{
    for (unsigned i = 0; i < ASSIGNMENTS; i++)
        set_own_button_assignments(g, i, menu_settings_buttons(g, i));
    ORIGINAL_EFFECT(charge_block_move(g, ASSIGNMENTS));
}

/* The places of the eighth assignment's list past the nine assignments. The
 * game compares and swaps them as it does the others: the boxes' first two
 * and cells past them, which only giving confirm or cancel L reaches
 * (docs/TODO.md). */
static void swap_stray_places(GameState *g, unsigned assignment, uint8_t button, uint8_t replacement)
{
    unsigned count = menu_settings_conflicts_count(g, assignment);
    for (unsigned i = count; i-- > 0;) {
        uint8_t other = menu_settings_conflict(g, assignment, i);
        if (other >= ASSIGNMENTS && menu_settings_buttons_unchecked(g, other) == button)
            set_menu_settings_buttons_unchecked(g, other, replacement);
    }
}

static void leave_conflicts(GameState *g, unsigned assignment)
{
    unsigned count = menu_settings_conflicts_count(g, assignment);
    for (unsigned i = 0; i < count && i < MENU_SETTINGS_CONFLICTS_COUNT; i++)
        set_menu_settings_conflicts(g, i, menu_settings_conflict(g, assignment, i));
    charge_block_move(g, count);
}

/* An assignment given a button: the assignments that share buttons with it
 * give the button up, taking the assignment's button in use instead. Which
 * one gave it up (the last the list names), or `none_swapped`. */
static unsigned give_button(GameState *g, unsigned row, uint8_t button, unsigned none_swapped)
{
    unsigned assignment = menu_settings_row_assignment(g, row);
    set_menu_settings_buttons(g, assignment, button);
    uint8_t replacement = button_assignments(g, assignment);
    unsigned swapped = none_swapped;
    unsigned count = menu_settings_conflicts_count(g, row);
    for (unsigned i = count; i-- > 0;) {
        uint8_t other = menu_settings_conflict(g, row, i);
        if (other < ASSIGNMENTS && menu_settings_buttons(g, other) == button) {
            set_menu_settings_buttons(g, other, replacement);
            swapped = other;
        }
    }
    ORIGINAL_EFFECT(leave_conflicts(g, row));
    swap_stray_places(g, row, button, replacement);
    ORIGINAL_EFFECT(set_menu_scratch_settings_conflicts_last(g, (uint16_t)(count - 1)));
    return swapped;
}

// $C2:C286
/* The shoulders page lists: where confirm and cancel leave R free the first
 * pages with it, and where they leave L the second; a shoulder that would
 * have both takes the other's button. */
static void keep_the_shoulders_paging(GameState *g)
{
    uint8_t taken = (uint8_t)(menu_settings_buttons(g, 0) | menu_settings_buttons(g, 1));
    if (menu_settings_buttons(g, PAGE_THE_OTHER) == BUTTON_R)
        set_menu_settings_buttons(g, PAGE_THE_OTHER, menu_settings_buttons(g, PAGE_ONE_WAY));
    if (!(taken & BUTTON_R))
        set_menu_settings_buttons(g, PAGE_ONE_WAY, BUTTON_R);
    if (menu_settings_buttons(g, PAGE_ONE_WAY) == BUTTON_L)
        set_menu_settings_buttons(g, PAGE_ONE_WAY, menu_settings_buttons(g, PAGE_THE_OTHER));
    if (!(taken & BUTTON_L))
        set_menu_settings_buttons(g, PAGE_THE_OTHER, BUTTON_L);
}

static unsigned cursor_row(const GameState *g) { return (uint8_t)(menu_cursor_index(g) - SETTINGS_FIRST_BUTTON_ROW); }

/* The button the row's push asks for: the next or the one before among the
 * seven it can be given, from the row's own; none where its own is none of
 * them or the push is neither way. */

static bool button_asked_for(const GameState *g, unsigned row, uint8_t *button)
{
    unsigned choice = 0;
    uint8_t held = menu_settings_buttons(g, row);
    while (menu_settings_button_choice(g, choice) != MENU_SETTINGS_NO_MORE_CHOICES && !(menu_settings_button_choice(g, choice) & held))
        choice++;
    if (menu_settings_button_choice(g, choice) == MENU_SETTINGS_NO_MORE_CHOICES)
        return false;
    MenuCursorDirection way = (MenuCursorDirection)menu_cursor_direction(g);
    if (way == MENU_CURSOR_WENT_RIGHT)
        choice = (choice + 1) % MENU_SETTINGS_BUTTON_CHOICES;
    else if (way == MENU_CURSOR_WENT_LEFT)
        choice = (choice + MENU_SETTINGS_BUTTON_CHOICES - 1) % MENU_SETTINGS_BUTTON_CHOICES;
    else
        return false;
    *button = menu_settings_button_choice(g, choice);
    return true;
}

// $C2:C1C4, $C2:C20F
/* The row given the button asked for: from the player's own buttons, the
 * row's assignment takes it from the one that had it, and that one from the
 * one that had what it is given in turn. */
bool menu_settings_give_row_button(GameState *g)
{
    unsigned row = cursor_row(g);
    uint8_t button;
    if (!button_asked_for(g, row, &button))
        return false;
    copy_own_to_rows(g);
    enum { NONE_SWAPPED = 0 };
    unsigned swapped = give_button(g, row, button, NONE_SWAPPED);
    /* The one that gave the button up settles in turn: the assignments that
     * share its new button give that up for its button in use. (With none
     * swapped, confirm settles.) */
    (void)give_button(g, swapped, menu_settings_buttons(g, swapped), swapped);
    return true;
}

/* Then the shoulders keep paging, and the rows show the changes. */
void menu_settings_show_row_change(GameState *g)
{
    keep_the_shoulders_paging(g);
    show_row_changes(g, cursor_row(g));
}

SettingsButtonsInput menu_settings_take_buttons_input(const GameState *g)
{
    enum { START_PRESSED = 0x01 };
    if (menu_pushed(g, MENU_PUSHED_LEFT))
        return BUTTONS_KEPT_AND_LEFT;
    if (menu_asked(g, MENU_GO_BACK))
        return BUTTONS_LEFT;
    if (input_pressed(g) & START_PRESSED)
        return BUTTONS_RESET;
    if (menu_pushed(g, MENU_PUSHED_UP) || menu_pushed(g, MENU_PUSHED_DOWN))
        return BUTTONS_KEPT;
    return BUTTONS_UNTOUCHED;
}

// $C2:BF6D
void menu_settings_leave_the_buttons(GameState *g)
{
    set_menu_cursor_index(g, menu_settings_cursor(g));
    go_to_step(g, SETTINGS_ON_THE_OPTIONS);
    menu_settings_use_buttons(g);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

void menu_settings_keep_the_rows(GameState *g) { keep_rows_as_own(g); }

// $C2:C183
void menu_settings_reset_buttons(GameState *g)
{
    for (unsigned i = 0; i < ASSIGNMENTS; i++) {
        set_own_button_assignments(g, i, menu_default_button_assignment(g, i));
        set_menu_settings_buttons(g, i, menu_default_button_assignment(g, i));
    }
    ORIGINAL_EFFECT(charge_block_move(g, 2 * ASSIGNMENTS));
    menu_settings_use_buttons(g);
}

void menu_settings_keep_buttons(GameState *g)
{
    keep_rows_as_own(g);
    menu_settings_use_buttons(g);
}

/* ---- The description ------------------------------------------------------------------------------ */

/* The icon at the description window's left, four tiles two by two. */
enum { ICON_AT = 0x3446 };

static void put_icon_blank(GameState *g)
{
    put_tile(g, ICON_AT, 0);
    put_tile(g, ICON_AT + TILE_BYTES, 0);
    put_tile(g, ICON_AT + ROW_BYTES, 0);
    put_tile(g, ICON_AT + ROW_BYTES + TILE_BYTES, 0);
}

static void put_icon(GameState *g, uint16_t first)
{
    put_tile(g, ICON_AT, first);
    put_tile(g, ICON_AT + TILE_BYTES, (uint16_t)(first + 1));
    put_tile(g, ICON_AT + ROW_BYTES, (uint16_t)(first + 2));
    put_tile(g, ICON_AT + ROW_BYTES + TILE_BYTES, (uint16_t)(first + 3));
}

// $C2:C4BC
/* The option's icon, then its description (menu/description.h): the
 * option's text and its value's, or its value's alone where it changed. */
static void describe(GameState *g, SettingsOption option)
{
    put_icon(g, menu_settings_option_icon(g, option));
    set_menu_settings_described_option(g, option);
    SettingCell described = (SettingCell)menu_settings_option_cell(g, option);
    uint8_t value_text = (uint8_t)(setting(g, described) + menu_settings_first_value_text(g, option));
    set_menu_settings_description(g, value_text);
    TIMED_AS(g, 0xC2F943, menu_describe_setting(g, option, value_text));
}

/* ---- Opening and the options --------------------------------------------------------------------- */

enum { SCREEN_WINDOWS = 0xC1F6, TEXT_LAYER = 2, DESCRIPTION_IN_PLACE = 0x0189, NOTHING_DESCRIBED = 0xFF };

// $C2:BF3D, $C2:C3B5
void menu_settings_open(GameState *g, Platform *pf)
{
    go_to_step(g, SETTINGS_ON_THE_OPTIONS);
    set_menu_cursor_index(g, menu_settings_cursor(g));
    set_menu_cursor_left_from(g, menu_settings_cursor(g));
    menu_draw_windows(g, pf, SCREEN_WINDOWS);
    set_menu_scroll_y(g, TEXT_LAYER, DESCRIPTION_IN_PLACE);
    set_menu_description_item(g, NOTHING_DESCRIBED);
    set_menu_description_stale(g, NOTHING_DESCRIBED);
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
    TIMED_AS(g, 0xC2E923, menu_clear_sprites_but_cursor(g));
}

enum { OPTIONS_LINE_EFFECTS = 0x5D };

// $C2:BF45
bool menu_settings_leaving(const GameState *g)
{
    return menu_asked(g, MENU_GO_BACK) && !box_ticked(g, BOX_SETTING_SPEEDS) && !box_ticked(g, BOX_CHOOSING_DESIGN);
}

void menu_settings_hold_line_effects(GameState *g) { set_menu_line_effects(g, OPTIONS_LINE_EFFECTS); }

void menu_settings_leave(GameState *g) { menu_pack_settings(g); }

// $C2:BFD4
SettingsOption menu_settings_take_option(GameState *g)
{
    uint8_t option = menu_cursor_index(g);
    set_menu_settings_cursor(g, option);
    return (SettingsOption)option;
}

uint8_t menu_settings_sound_output(const GameState *g) { return menu_setting_sound_output(g); }

static void flip(GameState *g, SettingsOption option)
{
    SettingCell cell = OPTION_CELLS[option];
    uint8_t flipped = (uint8_t)((setting(g, cell) + 1) & 1);
    set_setting(g, cell, flipped);
    tick_box(g, cell, flipped);
}

// $C2:C017, $C2:C038, $C2:C04A, $C2:C05C, $C2:C06E, $C2:C095
bool menu_settings_flip_option(GameState *g, SettingsOption option)
{
    if (!menu_asked(g, MENU_CHOOSE))
        return false;
    flip(g, option);
    return true;
}

// $C2:C0A7
void menu_settings_take_speeds(GameState *g)
{
    if (!menu_asked(g, MENU_CHOOSE))
        return;
    set_menu_cursor_index(g, SETTINGS_BATTLE_SPEED);
    set_menu_settings_cursor(g, SETTINGS_BATTLE_SPEED);
    tick_box(g, BOX_SETTING_SPEEDS, true);
}

// $C2:C164
/* A push right steps the meter's setting on, a push left back, round the
 * eight. */
static bool step_meter(GameState *g, SettingCell cell)
{
    uint8_t value = setting(g, cell);
    if (menu_pushed(g, MENU_PUSHED_RIGHT))
        value = (uint8_t)(value + 1);
    else if (menu_pushed(g, MENU_PUSHED_LEFT))
        value = (uint8_t)(value - 1);
    else
        return false;
    set_setting(g, cell, (uint8_t)(value % METER_STEPS));
    show_meters(g);
    return true;
}

bool menu_settings_step_meter(GameState *g, SettingsOption meter) { return step_meter(g, OPTION_CELLS[meter]); }

// $C2:C0B9, $C2:C0D1
void menu_settings_leave_the_meters(GameState *g)
{
    if (!menu_asked(g, MENU_CHOOSE) && !menu_asked(g, MENU_GO_BACK))
        return;
    set_menu_cursor_index(g, SETTINGS_SPEEDS);
    set_menu_settings_cursor(g, SETTINGS_SPEEDS);
    tick_box(g, BOX_SETTING_SPEEDS, false);
}

// $C2:C12C
/* While the design is being chosen the windows take each design as the
 * meter reaches it, in the screen's own window style. */
enum { SETTINGS_WINDOW_STYLE = 0x0010 };

bool menu_settings_choosing_design(const GameState *g) { return box_ticked(g, BOX_CHOOSING_DESIGN); }

bool menu_settings_step_design(GameState *g) { return step_meter(g, CELL_WINDOW_DESIGN); }

void menu_settings_show_design(GameState *g)
{
    set_menu_window_style(g, SETTINGS_WINDOW_STYLE);
    TIMED_AS(g, 0xC2C511, menu_load_window_design(g));
}

/* Choosing (or going back) starts or ends the choosing: the cursor points
 * at the design's meter meanwhile, and holds there against pushes. */
enum { ON_THE_DESIGN_OPTION_X = 0x38, ON_THE_DESIGN_OPTION_Y = 0x3B, ON_THE_DESIGN_METER_X = 0x88, ON_THE_DESIGN_METER_Y = 0x37 };

bool menu_settings_toggle_design_choice(GameState *g)
{
    if (!menu_asked(g, MENU_CHOOSE) && !menu_asked(g, MENU_GO_BACK))
        return false;
    bool choosing = !box_ticked(g, BOX_CHOOSING_DESIGN);
    tick_box(g, BOX_CHOOSING_DESIGN, choosing);
    set_menu_cursor_places_x(g, SETTINGS_WINDOW_DESIGN, choosing ? ON_THE_DESIGN_METER_X : ON_THE_DESIGN_OPTION_X);
    set_menu_cursor_places_y(g, SETTINGS_WINDOW_DESIGN, choosing ? ON_THE_DESIGN_METER_Y : ON_THE_DESIGN_OPTION_Y);
    return true;
}

void menu_settings_hold_cursor_on_the_design(GameState *g) { set_menu_cursor_held(g, box_ticked(g, BOX_CHOOSING_DESIGN)); }

// $C2:C080
void menu_settings_take_gauge(GameState *g)
{
    if (!menu_asked(g, MENU_CHOOSE))
        return;
    set_menu_cursor_index(g, (uint8_t)(menu_setting_battle_gauge(g) + SETTINGS_FIRST_GAUGE_PLACE));
    tick_box(g, BOX_CHOOSING_GAUGE, true);
    go_to_step(g, SETTINGS_ON_THE_GAUGE);
}

// $C2:C0E9
/* With the player's own buttons the cursor goes to the first row, the rows
 * the player's own. With the game's, a push right goes to the option right
 * of the controls' place in the window. */
enum { RIGHT_OF_THE_CONTROLS = SETTINGS_KEEP_BATTLE_COMMAND };

static void go_to_the_buttons(GameState *g)
{
    set_menu_cursor_index(g, SETTINGS_FIRST_BUTTON_ROW);
    go_to_step(g, SETTINGS_ON_THE_BUTTONS);
    copy_own_to_rows(g);
}

bool menu_settings_take_controls(GameState *g)
{
    bool to_the_buttons = false;
    if (menu_asked(g, MENU_CHOOSE)) {
        flip(g, SETTINGS_CONTROLS);
        to_the_buttons = menu_setting_own_buttons(g) != 0;
    } else if (menu_settings_option_before(g) == SETTINGS_CONTROLS && menu_pushed(g, MENU_PUSHED_RIGHT)) {
        if (menu_setting_own_buttons(g) != 0)
            to_the_buttons = true;
        else {
            set_menu_cursor_index(g, RIGHT_OF_THE_CONTROLS);
            set_menu_settings_cursor(g, RIGHT_OF_THE_CONTROLS);
        }
    }
    if (to_the_buttons)
        go_to_the_buttons(g);
    menu_settings_use_buttons(g);
    return to_the_buttons;
}

// $C2:BFE4
void menu_settings_show_option(GameState *g)
{
    SettingsOption option = (SettingsOption)menu_settings_cursor(g);
    set_menu_settings_option_before(g, option);
    describe(g, option);
    draw_boxes(g);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

/* ---- The gauge's layouts ------------------------------------------------------------------------- */

// $C2:BF9F
SettingsGaugeChoice menu_settings_choose_gauge_layout(const GameState *g)
{
    if (menu_asked(g, MENU_CHOOSE) || menu_asked(g, MENU_GO_BACK))
        return GAUGE_CHOSEN;
    return menu_cursor_index(g) == menu_cursor_frame_start(g) ? GAUGE_POINTED_AT : GAUGE_MOVED;
}

void menu_settings_back_from_the_gauge(GameState *g)
{
    tick_box(g, BOX_CHOOSING_GAUGE, false);
    set_menu_cursor_index(g, menu_settings_cursor(g));
    go_to_step(g, SETTINGS_ON_THE_OPTIONS);
}

void menu_settings_take_gauge_layout(GameState *g)
{
    set_menu_setting_battle_gauge(g, (uint8_t)(menu_cursor_index(g) - SETTINGS_FIRST_GAUGE_PLACE));
    describe(g, (SettingsOption)menu_settings_cursor(g));
    show_gauge(g);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

/* ---- The panel ------------------------------------------------------------------------------------ */

enum { PANEL_FRAME = 0xC19C, PANEL_WINDOWS = 0xC1B9, PANEL_LINE_EFFECTS = 0x1C };
enum { CAPTION_AT = 0x22, CAPTION_TILES = 12, CAPTION_LINE = 0x21FF, CAPTION_END_AT = 0x2E, CAPTION_END_TILES = 2,
       CAPTION_END = 0x21FB, UPLOAD_CAPTION = 0xFC4C };

// $C2:C2FF
/* The main menu's fourth task held a frame longer while the panel draws;
 * the panel's frame, a line of its caption on the text layer, and the boxes
 * from the settings. */
void menu_settings_panel_frame(GameState *g, Platform *pf)
{
    set_menu_tasks_countdown(g, MENU_CURSOR_TASK, (uint8_t)(menu_tasks_countdown(g, MENU_CURSOR_TASK) + 1));
    menu_draw_windows(g, pf, PANEL_FRAME);
    for (unsigned i = 0; i < CAPTION_TILES; i++)
        set_menu_text_tilemap(g, CAPTION_AT + i, CAPTION_LINE);
    for (unsigned i = 0; i < CAPTION_END_TILES; i++)
        set_menu_text_tilemap(g, CAPTION_END_AT + i, CAPTION_END);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * (CAPTION_TILES + CAPTION_END_TILES)));
    menu_queue_fixed_upload(g, UPLOAD_CAPTION);
    take_boxes_from_the_settings(g);
}

/* The panel's colours: the equipment's four twice into palette row 3,
 * the highlight's four into row 1's second half, and the settings' own
 * twenty-four into rows 5 and 6. */
typedef struct {
    uint16_t from;
    uint8_t first, count;
} PanelColours;

static const PanelColours PANEL_COLOURS[] = {
    { MENU_ROM_EQUIPMENT_COLOURS, 0x30, 4 },
    { MENU_ROM_EQUIPMENT_COLOURS, 0x34, 4 },
    { MENU_ROM_ITEMS_HIGHLIGHT_COLOURS, 0x18, 4 },
    { MENU_ROM_SETTINGS_PANEL_COLOURS, 0x50, 24 },
};
enum { PANEL_COLOUR_RUNS = sizeof PANEL_COLOURS / sizeof PANEL_COLOURS[0] };

void menu_settings_panel_show(GameState *g, Platform *pf)
{
    menu_draw_windows(g, pf, PANEL_WINDOWS);
    draw_boxes(g);
    show_meters(g);
    show_gauge(g);
    draw_button_rows(g);
    for (unsigned run = 0; run < PANEL_COLOUR_RUNS; run++) {
        const PanelColours *c = &PANEL_COLOURS[run];
        for (unsigned i = 0; i < c->count; i++)
            set_menu_palette(g, c->first + i, menu_rom_colour(g, c->from, i));
        ORIGINAL_EFFECT(charge_block_move(g, 2u * c->count));
    }
    TIMED_AS(g, 0xC286DD, menu_build_math_table(g, pf));
    set_menu_line_effects(g, PANEL_LINE_EFFECTS);
    TIMED_AS(g, 0xC2984A, menu_clear_sprites(g));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
}

// $C2:C2FF
/* The panel's frame, a frame's wait, then the rest of it. */
bool menu_show_settings_panel_with(GameState *g, Platform *pf, const MainMenuSteps *n, void *ctx)
{
    menu_settings_panel_frame(g, pf);
    n->note(ctx, MAIN_MENU_NOTE_SETTINGS_FRAMED, 0);
    if (!n->wait(ctx, MAIN_MENU_AT_THE_SETTINGS_FRAMED))
        return false;
    menu_settings_panel_show(g, pf);
    return true;
}

bool menu_settings_panel_can_show(const GameState *g) { return menu_window_list_drawable(g, PANEL_FRAME); }

// $C2:C397
/* The main menu leaving the settings: its line effects, the description's
 * icon taken away. */
void menu_settings_panel_put_away(GameState *g)
{
    set_menu_line_effects(g, PANEL_LINE_EFFECTS);
    put_icon_blank(g);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

/* ---- The opening and the options ---------------------------------------------------------------- */

// $C2:C017
bool menu_settings_set_sound_output(GameState *g, bool (*send)(void *ctx), void *ctx)
{
    return !menu_settings_flip_option(g, SETTINGS_STEREO) || send(ctx);
}

// $C2:BF3D
/* A tick, then the screen. */
bool menu_open_settings_with(GameState *g, Platform *pf, const SettingsSteps *n, void *ctx)
{
    if (!n->tick(ctx, SETTINGS_AT_THE_OPENING))
        return false;
    menu_settings_open(g, pf);
    return true;
}

// $C2:BF45
/* Going back leaves for the main menu, unless a speed or the design is
 * being set: then the option takes it back, as it takes every other input;
 * a tick where the cursor moved. */
bool menu_choose_setting_with(GameState *g, Platform *pf, const SettingsSteps *n, void *ctx)
{
    (void)pf;
    menu_settings_hold_line_effects(g);
    n->read_cursor(ctx);
    if (menu_settings_leaving(g)) {
        n->note(ctx, SETTINGS_NOTE_EDITING_TESTED, 0);
        menu_settings_leave(g);
        n->note(ctx, SETTINGS_NOTE_PACKED, 0);
        return n->back_to_the_main_menu(ctx);
    }
    n->note(ctx, menu_asked(g, MENU_GO_BACK) ? SETTINGS_NOTE_EDITING_TESTED : SETTINGS_NOTE_BUTTONS_TESTED, 0);
    if (!n->take_the_option(ctx))
        return false;
    bool moved = menu_cursor_index(g) != menu_cursor_frame_start(g);
    n->note(ctx, SETTINGS_NOTE_CURSOR_COMPARED, 0);
    return !moved || n->tick(ctx, SETTINGS_AT_THE_CURSOR_MOVED);
}

// $C2:BFD4
/* The option the cursor is on handled, then shown; a tick where the player
 * chose. */
bool menu_take_setting_option_with(GameState *g, Platform *pf, const SettingsSteps *n, void *ctx)
{
    (void)pf;
    SettingsOption option = menu_settings_take_option(g);
    n->note(ctx, SETTINGS_NOTE_OPTION_PICKED, option);
    if (!n->handle(ctx, option))
        return false;
    n->note(ctx, SETTINGS_NOTE_HANDLED, 0);
    menu_settings_show_option(g);
    n->note(ctx, SETTINGS_NOTE_SHOWN, 0);
    return !menu_asked(g, MENU_CHOOSE) || n->tick(ctx, SETTINGS_AT_AN_OPTION_CHOSEN);
}

/* ---- The opening and the options as C ----------------------------------------------------------- */

typedef struct {
    GameState *game;
    Platform *pf;
} SettingsAsC;

static void read_cursor(void *ctx) { menu_read_the_cursor_for_a_step(((SettingsAsC *)ctx)->game); }

static bool tick(void *ctx, SettingsPlace at)
{
    (void)at;
    SettingsAsC *c = ctx;
    menu_tick(c->game, c->pf);
    return true;
}

static const SettingsSteps as_c;

static bool take_the_option(void *ctx)
{
    SettingsAsC *c = ctx;
    if (!menu_settings_option_can_be_taken(c->game)) {
        program_needs_the_machine(c->game, 0xC2BFD4, "a setting the port doesn't take");
        return false;
    }
    bool back = false;
    TIMED_AS(c->game, 0xC2BFD4, back = menu_take_setting_option_with(c->game, c->pf, &as_c, c));
    return back;
}

static bool send_as_c(void *ctx)
{
    SettingsAsC *c = ctx;
    menu_settings_send_sound_output(c->game, c->pf);
    return true;
}

/* Each option's handler as C, timed as its routine; the meters' and the
 * design's step through a routine of their own only the machine runs yet. */
typedef enum { HANDLER_STEREO, HANDLER_FLIP, HANDLER_SPEEDS, HANDLER_GAUGE, HANDLER_CONTROLS, HANDLER_ONLY_ON_THE_MACHINE } HandlerKind;
static const struct {
    uint32_t routine;
    HandlerKind kind;
} handlers[SETTINGS_OPTION_COUNT] = {
    [SETTINGS_STEREO] = { 0xC2C017, HANDLER_STEREO },
    [SETTINGS_ACTIVE_TIME] = { 0xC2C06E, HANDLER_FLIP },
    [SETTINGS_KEEP_MENU_CURSORS] = { 0xC2C038, HANDLER_FLIP },
    [SETTINGS_KEEP_BATTLE_COMMAND] = { 0xC2C04A, HANDLER_FLIP },
    [SETTINGS_KEEP_BATTLE_LISTS] = { 0xC2C05C, HANDLER_FLIP },
    [SETTINGS_SPEEDS] = { 0xC2C0A7, HANDLER_SPEEDS },
    [SETTINGS_WINDOW_DESIGN] = { 0xC2C12C, HANDLER_ONLY_ON_THE_MACHINE },
    [SETTINGS_BATTLE_GAUGE] = { 0xC2C080, HANDLER_GAUGE },
    [SETTINGS_CONTROLS] = { 0xC2C0E9, HANDLER_CONTROLS },
    [SETTINGS_DESCRIPTIONS] = { 0xC2C095, HANDLER_FLIP },
    [SETTINGS_BATTLE_SPEED] = { 0xC2C0B9, HANDLER_ONLY_ON_THE_MACHINE },
    [SETTINGS_MESSAGE_SPEED] = { 0xC2C0D1, HANDLER_ONLY_ON_THE_MACHINE },
};

static void run_the_handler(SettingsAsC *c, SettingsOption option)
{
    GameState *g = c->game;
    switch (handlers[option].kind) {
    case HANDLER_STEREO: (void)menu_settings_set_sound_output(g, send_as_c, c); break;
    case HANDLER_FLIP: (void)menu_settings_flip_option(g, option); break;
    case HANDLER_SPEEDS: menu_settings_take_speeds(g); break;
    case HANDLER_GAUGE: menu_settings_take_gauge(g); break;
    case HANDLER_CONTROLS: (void)menu_settings_take_controls(g); break;
    case HANDLER_ONLY_ON_THE_MACHINE: break;
    }
}

static bool handle(void *ctx, SettingsOption option)
{
    SettingsAsC *c = ctx;
    if (handlers[option].kind == HANDLER_ONLY_ON_THE_MACHINE) {
        program_needs_the_machine(c->game, handlers[option].routine, "a setting's meter only the machine steps");
        return false;
    }
    TIMED_AS(c->game, handlers[option].routine, run_the_handler(c, option));
    return true;
}

static bool back_to_the_main_menu(void *ctx)
{
    SettingsAsC *c = ctx;
    return menu_back_to_the_main_menu(c->game, c->pf);
}

static void note(void *ctx, SettingsNote note, uint8_t value) { (void)ctx, (void)note, (void)value; }

static const SettingsSteps as_c = { read_cursor, tick, take_the_option, handle, back_to_the_main_menu, note };

static bool step(GameState *g, Platform *pf, uint32_t routine, bool (*takes)(const GameState *),
                 bool (*run)(GameState *, Platform *, const SettingsSteps *, void *))
{
    if (!takes(g)) {
        program_needs_the_machine(g, routine, "a settings screen the port doesn't take");
        return false;
    }
    SettingsAsC c = { g, pf };
    return run(g, pf, &as_c, &c);
}

bool menu_open_settings(GameState *g, Platform *pf)
{
    return step(g, pf, 0xC2BF3D, menu_settings_can_open, menu_open_settings_with);
}

bool menu_choose_setting(GameState *g, Platform *pf)
{
    return step(g, pf, 0xC2BF45, menu_settings_can_choose, menu_choose_setting_with);
}
