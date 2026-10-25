/* Naming a character: menu screen 13.
 *
 * As a character joins the party the player names it, and the Epoch too
 * as it first flies: the names are the seven characters' and then the
 * Epoch's. The screen shows the named one's portrait, the name above a grid of the letters the game
 * writes names in (eight rows of ten: capitals, small letters, figures and
 * marks), and a cursor on the grid. Choosing a letter types it into the
 * name where the name's own cursor stands; a name has five letters at most,
 * and a sixth typed replaces the fifth. Going back takes the last letter
 * off. Start accepts the name: its spaces before and after it dropped, it
 * is refused, with the refusal's buzz, where another character already has
 * it, and quietly where nothing is left of it; otherwise it becomes the
 * character's and the menus close.
 *
 * The name starts as the character's own, its letters typed over from the
 * first: the name's cursor stands at the start however long the name is,
 * and the letters past those typed stay as they were. A letter taken back
 * leaves its place with no letter in it (NO_LETTER), which the name keeps.
 *
 * A new game's first naming, Crono's, first asks how the battles go:
 * active, the enemies acting while the player chooses, or waiting for the
 * player in the menus (the setting's first bit). Then the screen fades out
 * and in again on the naming, which waits for a button before it takes
 * the cursor. */
#include "menu/naming.h"

#include "charge.h"
#include "core/program.h"
#include "cpu_io.h"
#include "menu/fades.h"
#include "menu/cursor.h"
#include "menu/description.h"
#include "menu/display.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/text.h"
#include "menu/tiles.h"
#include "menu/typeset.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "ram.h"
#include "vars.h"

/* ---- Names --------------------------------------------------------------------------------------- */

/* A name is five letters and a zero after them; a shorter one ends with
 * the zero. A space is a letter. A place of the name being typed that a
 * letter was taken back from holds no letter. */
enum { NAME_LETTERS = 5, NAME_BYTES = 6, NO_LETTER = 0xFF, SPACE = 0xEF, END_OF_NAME = 0x00 };
typedef uint8_t Letter;

/* Who is named: a character, or the Epoch after the seven. Their names are
 * kept one after another, the Epoch's last. */
enum { CHARACTERS = CHARACTER_NAMES_COUNT, EPOCH = CHARACTERS, NAMES = CHARACTERS + 1 };

static uint8_t named(const GameState *g) { return (uint8_t)menu_screen_argument(g); }

static Letter name_letter(const GameState *g, uint8_t who, unsigned place)
{
    return who == EPOCH ? epoch_name_byte(g, place) : character_names_byte(g, who, place);
}

static void set_name_letter(GameState *g, uint8_t who, unsigned place, Letter l)
{
    if (who == EPOCH)
        set_epoch_name_byte(g, place, l);
    else
        set_character_names_byte(g, who, place, l);
}

static Letter typed(const GameState *g, unsigned place) { return menu_name_typed_byte(g, place); }

/* ---- The letters' grid --------------------------------------------------------------------------- */

/* The grid's letters, by row and column; on the screen each letter a tile
 * with a blank after it, sixteen pixels apart each way. */
enum { GRID_ROWS = 8, GRID_COLUMNS = 10, GRID_STEP = 16, GRID_LEFT = MENU_NAME_GRID_LEFT, GRID_TOP = MENU_NAME_GRID_TOP };

/* The cursor is the screen's first place, which moves about the grid. */
enum { GRID_CURSOR = 0 };

/* Writing the grid out as a string: each letter with a blank tile after
 * it, a new line after each row, a zero at the end. */
enum { BLANK_TILE = 0xFF, NEW_LINE = 0x01, END_OF_TEXT = 0x00 };

static void put_grid_text(GameState *g, unsigned *at, uint8_t v) { set_menu_name_grid_text_byte(g, (*at)++, v); }

// $C2:E545
void menu_naming_write_out_the_grid(GameState *g)
{
    unsigned at = 0;
    for (unsigned row = 0; row < GRID_ROWS; row++) {
        for (unsigned column = 0; column < GRID_COLUMNS; column++) {
            put_grid_text(g, &at, menu_name_grid_letter(g, row, column));
            put_grid_text(g, &at, BLANK_TILE);
        }
        put_grid_text(g, &at, NEW_LINE);
    }
    put_grid_text(g, &at, END_OF_TEXT);
    ORIGINAL_EFFECT(set_menu_scratch_grid_letters_left(g, 0));
    ORIGINAL_EFFECT(set_menu_scratch_grid_rows_left(g, 0));
}

/* For the original's code and shadow mode: it works the cursor's place out
 * with the multiplier, which it leaves holding it. */
static void leave_place_multiplied(GameState *g, uint8_t by, uint8_t times)
{
    hw_set_multiplicand(g, by);
    (void)hw_multiply_by(g, times);
}

static void put_cursor_on_row(GameState *g, uint8_t row)
{
    set_menu_name_grid_row(g, row);
    uint8_t line = (uint8_t)(GRID_TOP + row * GRID_STEP);
    set_menu_cursor_places_y(g, GRID_CURSOR, line);
    ORIGINAL_EFFECT(set_menu_name_grid_line(g, (uint16_t)(line << 8)));
    ORIGINAL_EFFECT(leave_place_multiplied(g, GRID_STEP, row));
}

static void put_cursor_on_column(GameState *g, uint8_t column)
{
    set_menu_name_grid_column(g, column);
    set_menu_cursor_places_x(g, GRID_CURSOR, (uint8_t)(GRID_LEFT + column * GRID_STEP));
    ORIGINAL_EFFECT(leave_place_multiplied(g, GRID_STEP, column));
}

// $C2:E2A0, $C2:E2AA, $C2:E2CC, $C2:E2D6
/* A push moves the cursor a letter that way, round the grid's edges. */
static void move_on_the_grid(GameState *g, MenuCursorDirection way)
{
    uint8_t row = menu_name_grid_row(g), column = menu_name_grid_column(g);
    switch (way) {
    case MENU_CURSOR_WENT_UP: put_cursor_on_row(g, row == 0 ? GRID_ROWS - 1 : (uint8_t)(row - 1)); break;
    case MENU_CURSOR_WENT_DOWN: put_cursor_on_row(g, row + 1 >= GRID_ROWS ? 0 : (uint8_t)(row + 1)); break;
    case MENU_CURSOR_WENT_LEFT: put_cursor_on_column(g, column == 0 ? GRID_COLUMNS - 1 : (uint8_t)(column - 1)); break;
    case MENU_CURSOR_WENT_RIGHT: put_cursor_on_column(g, column + 1 >= GRID_COLUMNS ? 0 : (uint8_t)(column + 1)); break;
    case MENU_CURSOR_NOT_PUSHED: break;
    }
}

/* Whether the frame's push moved the cursor: its place against where it
 * stood as the frame began. */
uint16_t menu_naming_cursor_place(const GameState *g)
{
    return (uint16_t)(menu_cursor_places_x(g, GRID_CURSOR) | menu_cursor_places_y(g, GRID_CURSOR) << 8);
}

bool menu_naming_move_on_the_grid(GameState *g)
{
    uint16_t before = menu_naming_cursor_place(g);
    set_menu_name_cursor_before(g, before);
    ORIGINAL_EFFECT(hw_set_multiplicand(g, GRID_STEP));
    uint8_t way = menu_cursor_direction(g);
    if (way & MENU_CURSOR_NOT_PUSHED)
        return false;
    move_on_the_grid(g, (MenuCursorDirection)way);
    return menu_naming_cursor_place(g) != before;
}

/* ---- Typing ------------------------------------------------------------------------------------- */

/* The name's cursor: two arrows above and below the place the next letter
 * goes, eight pixels a letter from the name's start, at the fifth letter
 * once the name is full. */
enum { NAME_CURSOR_ABOVE = 96, NAME_CURSOR_BELOW = 97, NAME_LEFT = 0x9C, LETTER_PIXELS = 8 };

// $C2:E34A
void menu_naming_place_the_names_cursor(GameState *g)
{
    uint8_t place = menu_name_letters(g) < NAME_LETTERS ? (uint8_t)menu_name_letters(g) : NAME_LETTERS - 1;
    uint8_t x = (uint8_t)(NAME_LEFT + place * LETTER_PIXELS);
    set_oam_buffer_x(g, NAME_CURSOR_ABOVE, x);
    set_oam_buffer_x(g, NAME_CURSOR_BELOW, x);
}

// $C2:E316
/* The letter under the cursor typed where the name's cursor stands, or
 * over the fifth once the name is full. */
static void type_the_letter(GameState *g)
{
    uint8_t row = menu_name_grid_row(g), column = menu_name_grid_column(g);
    ORIGINAL_EFFECT(leave_place_multiplied(g, GRID_COLUMNS, row));
    Letter letter = menu_name_grid_letter(g, row, column);
    unsigned place = menu_name_letters(g) < NAME_LETTERS ? menu_name_letters(g) : NAME_LETTERS - 1;
    set_menu_name_typed_byte(g, place, letter);
    set_menu_name_letters(g, (uint16_t)(place + 1));
}

/* The last letter typed taken back: its place left with none. */
static void take_back_a_letter(GameState *g)
{
    uint16_t letters = menu_name_letters(g);
    set_menu_name_typed_byte(g, letters - 1u, NO_LETTER);
    set_menu_name_letters(g, (uint16_t)(letters - 1));
}

/* The name's window, which shows the name as it is typed. */
enum { NAME_WINDOW = 0xC41F };

NamingTyped menu_naming_type(GameState *g)
{
    NamingTyped t = NAMING_NOTHING_TYPED;
    if (menu_asked(g, MENU_CHOOSE)) {
        type_the_letter(g);
        t = NAMING_LETTER_TYPED;
    } else if (menu_asked(g, MENU_GO_BACK) && menu_name_letters(g) != 0) {
        take_back_a_letter(g);
        t = NAMING_LETTER_TAKEN_BACK;
    }
    return t;
}

void menu_naming_draw_the_name(GameState *g, Platform *pf) { menu_draw_windows(g, pf, NAME_WINDOW); }

void menu_naming_show_the_name(GameState *g)
{
    TIMED_AS(g, 0xC2E34A, menu_naming_place_the_names_cursor(g));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

/* ---- Accepting the name ------------------------------------------------------------------------- */

static Letter kept(const GameState *g, unsigned place) { return menu_name_kept_byte(g, place); }

// $C2:E390
/* The name as typed with its spaces after it dropped (and the places past
 * its end), and then its spaces before it. */
static void drop_the_spaces(GameState *g)
{
    for (unsigned place = 0; place < NAME_BYTES; place++) {
        set_menu_name_kept_byte(g, place, END_OF_NAME);
        set_menu_name_trimmed_byte(g, place, typed(g, place));
    }
    ORIGINAL_EFFECT(charge_block_move(g, 2 * NAME_BYTES - 2));
    for (unsigned place = NAME_BYTES; place-- > 0;) {
        Letter l = menu_name_trimmed_byte(g, place);
        if (l == SPACE)
            set_menu_name_trimmed_byte(g, place, END_OF_NAME);
        else if (l != END_OF_NAME)
            break;
    }
    unsigned to = 0;
    Letter space_dropped = SPACE;
    for (unsigned place = 0; place < NAME_LETTERS; place++) {
        Letter l = menu_name_trimmed_byte(g, place);
        if (l == END_OF_NAME)
            break;
        if (l == space_dropped)
            continue;
        set_menu_name_kept_byte(g, to++, l);
        space_dropped = END_OF_NAME;
    }
    ORIGINAL_EFFECT(set_menu_scratch_name_space_before(g, space_dropped));
}

/* Whether a character's name is the one kept: the same letters to the end
 * of either, or all five the same. */
static bool same_name(const GameState *g, uint8_t who)
{
    for (unsigned place = 0; place < NAME_LETTERS; place++) {
        Letter l = kept(g, place);
        if (l != name_letter(g, who, place))
            return false;
        if (l == END_OF_NAME)
            return true;
    }
    return true;
}

// $C2:E3E0
/* Whether another character has the name already. The character's own name
 * has had its first letter cleared, so it is never the one found. */
static bool someone_has_it(GameState *g)
{
    for (uint8_t who = 0; who < NAMES; who++)
        if (same_name(g, who)) {
            ORIGINAL_EFFECT(set_menu_scratch_name_place_kept(g, (uint8_t)(who * NAME_BYTES)));
            return true;
        }
    ORIGINAL_EFFECT(set_menu_scratch_name_place_kept(g, NAMES * NAME_BYTES));
    return false;
}

// $C2:E363
/* Accepting: the character's own name loses its first letter, so that the
 * check never finds it, and the name typed is kept with its spaces
 * dropped; then it is checked (the bridge buzzes where someone has it). */
void menu_naming_keep_the_name(GameState *g)
{
    set_name_letter(g, named(g), 0, END_OF_NAME);
    drop_the_spaces(g);
}

bool menu_naming_someone_has_it(GameState *g) { return someone_has_it(g); }

/* Given where it is anybody's: the name kept becomes the character's,
 * where anything is left of it. Whether it was given. */
bool menu_naming_give_the_name(GameState *g)
{
    if (kept(g, 0) == END_OF_NAME)
        return false;
    for (unsigned place = 0; place < NAME_BYTES; place++)
        set_name_letter(g, named(g), place, kept(g, place));
    ORIGINAL_EFFECT(charge_block_move(g, NAME_BYTES));
    return true;
}

/* Where the name being typed goes: the record the naming kept as it
 * opened is the named one's. */
bool menu_naming_keeps_the_named_ones_record(const GameState *g)
{
    return named(g) < NAMES && menu_name_record_at(g) == RAM_ABS(RAM_CHARACTER_NAMES) + named(g) * NAME_BYTES;
}

/* The name was given where the character's name has a first letter again
 * (accepting cleared it). */
bool menu_naming_name_given(const GameState *g) { return name_letter(g, named(g), 0) != END_OF_NAME; }

/* ---- Opening the naming ------------------------------------------------------------------------- */

/* The naming's windows; the grid's place on the front layer, written in
 * the text's own attribute; the screen's scrolls. */
enum { NAMING_WINDOWS = 0xC3BC, GRID_AT = 0x30CC, TEXT_IN_BANK = 0x7E0000, FRONT_LAYER = 0, FRONT_LAYER_DOWN = 3 };
enum { TEXT_LAYER = 2, TEXT_LAYER_AT_REST = 0x01F0 };

/* The portrait at the name's left, in its own palette row; the Epoch's
 * needs tiles of its own sent first. */
enum { PORTRAIT_AT = 0x2E96, PORTRAIT_ATTRIBUTE = 0x16, UPLOAD_EPOCH_PICTURE_TILES = 0xFC68 };

/* The name's cursor's two arrows, the lower flipped. */
enum { NAME_CURSOR_ABOVE_Y = 0x16, NAME_CURSOR_BELOW_Y = 0x2E, NAME_CURSOR_TILE = 0x22 };
enum { NAME_CURSOR_ABOVE_ATTRIBUTES = 0xA0, NAME_CURSOR_BELOW_ATTRIBUTES = 0x20 };

/* The text layer's second row filled with a blank, and the tiles the
 * screen's label is set in cleared. */
enum { BLANK_ROW_TILE = 0x61FF, TILES_A_ROW = 32, LABEL_TILES_AT = 0x5E00, LABEL_TILE_BYTES = 0x400 };

/* The label above the name: a string of the menus' own, laid out on two
 * strips of the text layer. */
enum { LABEL_STRINGS = 0xFFCF3B, LABEL_STRING = 0, LABEL_TOP_AT = 0x50CA, LABEL_BOTTOM_AT = 0x514A, LABEL_COLUMNS = 24 };
enum { LABEL_TOP_TILE = 0x00, LABEL_BOTTOM_TILE = 0x41 };

/* The cursor's own sprite, its tile. */
enum { CURSOR_TILE = 0x20 };

/* The screen's uploads: the layers whole, the window line tables, the text
 * layer, and the label's tiles. */
enum { UPLOAD_LABEL_TILES = 0xE53E };

/* Its per-line effects: the naming's; or, for the game's first, one more,
 * the scroll bar's place hidden a frame less. */
enum { NAMING_LINE_EFFECTS = 0x1D, FIRST_NAMING_LINE_EFFECTS = 0x7F };

static void set_name_cursor(GameState *g, unsigned sprite, uint8_t y, uint8_t attributes)
{
    set_oam_buffer_x(g, sprite, NAME_LEFT);
    set_oam_buffer_y(g, sprite, y);
    set_oam_buffer_tile(g, sprite, NAME_CURSOR_TILE);
    set_oam_buffer_attributes(g, sprite, attributes);
}

/* For the original's code and shadow mode: the portrait's attribute in the
 * scratch word its drawing reads, the window line table's counts. */
static void leave_portrait_and_lines(GameState *g)
{
    set_menu_scratch_portrait_tile(g, (uint16_t)((menu_scratch_portrait_tile(g) & 0x00FF) | PORTRAIT_ATTRIBUTE << 8));
}

// $C2:E40E
void menu_naming_open(GameState *g, Platform *pf)
{
    set_menu_cursor_index(g, GRID_CURSOR);
    menu_draw_windows(g, pf, NAMING_WINDOWS);
    TIMED_AS(g, 0xC2E545, menu_naming_write_out_the_grid(g));
    set_menu_text_attribute(g, 0);
    uint32_t grid = TEXT_IN_BANK | (uint16_t)RAM_MENU_NAME_GRID_TEXT;
    uint16_t pen = menu_draw_string_on(g, &grid, GRID_AT, 0);
    (void)menu_draw_string(g, grid + 1, pen, 0);
    set_menu_scroll_y(g, FRONT_LAYER, FRONT_LAYER_DOWN);
    set_menu_name_grid_column(g, 0);
    set_menu_name_grid_row(g, 0);
    ORIGINAL_EFFECT(set_menu_name_grid_line(g, GRID_TOP << 8));
    uint8_t character = named(g);
    set_menu_portrait_character(g, character);
    if (character >= EPOCH)
        menu_queue_fixed_upload(g, UPLOAD_EPOCH_PICTURE_TILES);
    ORIGINAL_EFFECT(leave_portrait_and_lines(g));
    set_menu_draw_origin(g, PORTRAIT_AT);
    TIMED_AS(g, 0xC2EC38, menu_draw_portrait(g, PORTRAIT_AT, character, PORTRAIT_ATTRIBUTE));
    set_name_cursor(g, NAME_CURSOR_ABOVE, NAME_CURSOR_ABOVE_Y, NAME_CURSOR_ABOVE_ATTRIBUTES);
    set_name_cursor(g, NAME_CURSOR_BELOW, NAME_CURSOR_BELOW_Y, NAME_CURSOR_BELOW_ATTRIBUTES);
    ORIGINAL_EFFECT(leave_place_multiplied(g, character, NAME_BYTES));
    set_menu_name_record_at(g, (uint16_t)(RAM_ABS(RAM_CHARACTER_NAMES) + character * NAME_BYTES));
    for (unsigned place = 0; place < NAME_BYTES; place++)
        set_menu_name_typed_byte(g, place, name_letter(g, character, place));
    set_menu_name_letters(g, 0);
    for (unsigned tile = 0; tile < TILES_A_ROW; tile++)
        set_menu_text_tilemap(g, TILES_A_ROW + tile, BLANK_ROW_TILE);
    set_menu_scroll_y(g, TEXT_LAYER, TEXT_LAYER_AT_REST);
    for (unsigned word = 0; word < LABEL_TILE_BYTES / 2; word++)
        set_menu_glyph_tiles(g, word, 0);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * NAME_BYTES + 2 * TILES_A_ROW + LABEL_TILE_BYTES));
    menu_draw_windows(g, pf, NAME_WINDOW);
    TIMED_AS(g, 0xC286DD, menu_build_math_table(g, pf));
    set_menu_text_strings(g, LABEL_STRINGS);
    set_menu_text_string(g, LABEL_STRING);
    set_menu_load_destination(g, LABEL_TILES_AT);
    TIMED_AS(g, 0xC2F90C, (void)menu_set_label(g, LABEL_STRINGS, LABEL_STRING, LABEL_TILES_AT));
    TIMED_AS(g, 0xC2FB97, menu_lay_out_label(g, LABEL_TOP_AT, LABEL_COLUMNS, LABEL_TOP_TILE));
    TIMED_AS(g, 0xC2FB97, menu_lay_out_label(g, LABEL_BOTTOM_AT, LABEL_COLUMNS, LABEL_BOTTOM_TILE));
    set_menu_cursor_sprite(g, (uint16_t)((menu_cursor_sprite(g) & 0xFF00) | CURSOR_TILE));
    TIMED_AS(g, 0xFFFC04, menu_put_window_line_table(g, pf));
    ORIGINAL_EFFECT(menu_leave_window_line_table_counts(g));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER_WHOLE);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER_WHOLE);
    menu_queue_fixed_upload(g, MENU_UPLOAD_WINDOW_LINES);
    menu_queue_fixed_upload(g, MENU_UPLOAD_TEXT_LAYER);
    menu_queue_fixed_upload(g, UPLOAD_LABEL_TILES);
    uint8_t effects = NAMING_LINE_EFFECTS;
    if (names_given(g) == 0) {
        set_menu_scroll_bar_hidden(g, (uint8_t)(menu_scroll_bar_hidden(g) - 1));
        effects = FIRST_NAMING_LINE_EFFECTS;
    }
    set_menu_line_effects(g, effects);
    set_names_given(g, (uint16_t)((names_given(g) & 0xFF00) | effects));
    TIMED_AS(g, 0xC2E34A, menu_naming_place_the_names_cursor(g));
}

/* ---- Which naming opens -------------------------------------------------------------------------- */

/* The naming notes the character among those named; with none named yet
 * and Crono's the game's first, the battle mode is asked first. */
bool menu_naming_asks_the_battle_mode_first(GameState *g)
{
    set_names_given(g, (uint16_t)(names_given(g) | named(g)));
    return (uint8_t)names_given(g) == 0;
}

/* A button pressed: the naming takes the cursor, with its line effects. */
enum { NAMING = 1 };

bool menu_naming_waits(const GameState *g) { return input_pressed(g) == 0; }

void menu_naming_take_the_cursor(GameState *g)
{
    set_menu_line_effects(g, NAMING_LINE_EFFECTS);
    set_menu_scroll_bar_hidden(g, 0);
    set_menu_screen_step(g, NAMING);
}

/* Start accepts the name. */
enum { START = 0x01 };

bool menu_naming_started(const GameState *g) { return (input_pressed(g) & START) != 0; }

/* ---- The battle mode ----------------------------------------------------------------------------- */

/* The battle mode's screen: its tiles from the ROM over the front layer's
 * second screen, a block and then twelve half rows of the rows below it;
 * its windows, the colour math, its colours in the fifth palette row; the
 * text layer a line down; and its layers queued. */
enum { MODE_SCREEN_AT = 0x400, MODE_SCREEN_TILES = 0x240, MODE_ROWS = 12, MODE_ROW_TILES = 8 };
enum { MODE_WINDOWS = 0xC429, MODE_COLOURS_AT = 4 * 16, MODE_COLOURS = 16, MODE_LINE_EFFECTS = 0x05, UPLOAD_MODE_SCREEN = 0xFC61 };

// $C2:E576
void menu_naming_open_the_battle_mode(GameState *g, Platform *pf)
{
    for (unsigned tile = 0; tile < MODE_SCREEN_TILES; tile++)
        set_menu_front_tilemap(g, MODE_SCREEN_AT + tile, menu_battle_mode_screen_tile(g, tile));
    for (unsigned row = 0; row < MODE_ROWS; row++)
        for (unsigned tile = 0; tile < MODE_ROW_TILES; tile++)
            set_menu_front_tilemap(g, MODE_SCREEN_AT + MODE_SCREEN_TILES + row * 2 * MODE_ROW_TILES + tile,
                                   menu_battle_mode_screen_tile(g, MODE_SCREEN_TILES + row * MODE_ROW_TILES + tile));
    ORIGINAL_EFFECT(charge_block_move(g, 2 * (MODE_SCREEN_TILES + MODE_ROWS * MODE_ROW_TILES)));
    ORIGINAL_EFFECT(set_menu_scratch_mode_rows_left(g, 0));
    menu_draw_windows(g, pf, MODE_WINDOWS);
    TIMED_AS(g, 0xC286DD, menu_build_math_table(g, pf));
    for (unsigned i = 0; i < MODE_COLOURS; i++)
        set_menu_palette(g, MODE_COLOURS_AT + i, menu_battle_mode_colour(g, i));
    ORIGINAL_EFFECT(charge_block_move(g, 2 * MODE_COLOURS));
    set_menu_scroll_y(g, TEXT_LAYER, 1);
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    set_menu_line_effects(g, MODE_LINE_EFFECTS);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER_WHOLE);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER_WHOLE);
    menu_queue_fixed_upload(g, UPLOAD_MODE_SCREEN);
}

/* The mode on the cursor is described, two glyphs a frame, on a row of
 * its own: the menus' strings from the second. */
enum { MODE_TEXTS = 0xFFCF3B, MODE_LINE = 0x5248, MODE_ROW_VRAM = 0x6A20, MODE_ROW_BYTES = 0x200, TWO_GLYPHS_A_FRAME = 2 };

// $C2:E5D3
void menu_naming_describe_the_battle_mode(GameState *g)
{
    uint8_t text = (uint8_t)(menu_cursor_index(g) + 1);
    if (text == menu_description_item(g))
        return;
    set_menu_description_item(g, text);
    set_menu_text_string(g, text);
    set_menu_description_line(g, MODE_LINE);
    set_menu_description_row_vram(g, MODE_ROW_VRAM);
    set_menu_text_strings(g, MODE_TEXTS);
    set_menu_description_pace(g, TWO_GLYPHS_A_FRAME);
    set_menu_description_row_bytes(g, MODE_ROW_BYTES);
    TIMED_AS(g, 0xC2FA49, menu_start_typing(g));
}

/* Active, the first choice, sets the setting's bit; waiting clears it. On
 * to the naming as the screen fades out, the screen's tasks stopped. */
enum { ACTIVE_BIT = 0x40, WAITING_CHOICE = 1, OPENING_THE_NAMING = 3 };
enum { SCREENS_TASKS = 1u << MENU_DESCRIPTION_TASK | 1u << MENU_SCROLL_BAR_TASK | 1u << MENU_RECORDS_TASK | 1u << MENU_HELD_CURSORS_TASK };

void menu_naming_choose_the_battle_mode(GameState *g)
{
    uint8_t first = game_settings(g, 0);
    set_game_settings(g, 0, (menu_cursor_index(g) & WAITING_CHOICE) ? (uint8_t)(first & ~ACTIVE_BIT) : (uint8_t)(first | ACTIVE_BIT));
    set_menu_screen_step(g, OPENING_THE_NAMING);
    TIMED_AS(g, 0xC28255, menu_stop_tasks(g, SCREENS_TASKS));
}

/* ---- What the steps take ---------------------------------------------------------------------- */

bool menu_naming_names_one(const GameState *g) { return (uint8_t)menu_screen_argument(g) < NAMES; }

bool menu_naming_is_whole(const GameState *g)
{
    return menu_naming_names_one(g) && menu_naming_keeps_the_named_ones_record(g) && menu_name_grid_row(g) < GRID_ROWS
           && menu_name_grid_column(g) < GRID_COLUMNS && menu_name_letters(g) <= NAME_LETTERS;
}

/* ---- The screen's steps --------------------------------------------------------------------------- */

/* The steps, as the screen's table has them (menu/screens.c). */
enum { BATTLE_MODE_STEP = 2 };

// $C2:E201
/* The naming drawn, then on to its wait as it fades in. */
static bool open_the_naming(GameState *g, Platform *pf, const NamingSteps *n, void *ctx)
{
    TIMED_AS(g, 0xC2E40E, menu_naming_open(g, pf));
    set_menu_screen_step(g, (uint8_t)(menu_screen_step(g) + 1));
    return n->ends(ctx, NAMING_FADES_IN);
}

// $C2:E1ED
/* At a new game's first naming, the battle mode's screen first; otherwise
 * the naming. */
static bool open_naming_screen(GameState *g, Platform *pf, const NamingSteps *n, void *ctx)
{
    if (!menu_naming_asks_the_battle_mode_first(g))
        return open_the_naming(g, pf, n, ctx);
    TIMED_AS(g, 0xC2E576, menu_naming_open_the_battle_mode(g, pf));
    set_menu_screen_step(g, BATTLE_MODE_STEP);
    return n->ends(ctx, NAMING_FADES_IN);
}

// $C2:E209
/* Any button takes the cursor to the naming, with a tick. */
static bool wait_to_name(GameState *g, Platform *pf, const NamingSteps *n, void *ctx)
{
    (void)pf;
    if (menu_naming_waits(g))
        return true;
    n->note(ctx, NAMING_NOTE_PRESSED_LOADED, 0);
    if (!n->sound(ctx, 0xC2E20E, MENU_SOUND_CURSOR))
        return false;
    menu_naming_take_the_cursor(g);
    return true;
}

// $C2:E236
/* The mode on the cursor described; choosing one sets it and goes on to
 * the naming as the screen fades out; a tick where the cursor moved. */
static bool choose_battle_mode(GameState *g, Platform *pf, const NamingSteps *n, void *ctx)
{
    (void)pf;
    n->read_cursor(ctx);
    uint8_t shown = menu_description_item(g);
    TIMED_AS(g, 0xC2E5D3, menu_naming_describe_the_battle_mode(g));
    n->note(ctx, NAMING_NOTE_MODE_DESCRIBED, shown);
    n->note(ctx, NAMING_NOTE_BUTTONS_TESTED, 0);
    if (menu_asked(g, MENU_CHOOSE)) {
        menu_naming_choose_the_battle_mode(g);
        return n->ends(ctx, NAMING_FADES_OUT);
    }
    bool moved = menu_cursor_index(g) != menu_cursor_frame_start(g);
    n->note(ctx, NAMING_NOTE_CURSOR_COMPARED, 0);
    return !moved || n->sound(ctx, 0xC2E248, MENU_SOUND_CURSOR);
}

// $C2:E2F4
/* A letter typed or taken back, with a tick; the name drawn again. */
static bool type_on_the_name_grid(GameState *g, Platform *pf, const NamingSteps *n, void *ctx)
{
    NamingTyped t = menu_naming_type(g);
    if (t == NAMING_LETTER_TAKEN_BACK)
        n->note(ctx, NAMING_NOTE_LETTERS_LOADED, (uint8_t)menu_name_letters(g));
    if (t != NAMING_NOTHING_TYPED && !n->sound(ctx, 0xC2E30D, MENU_SOUND_CURSOR))
        return false;
    menu_naming_draw_the_name(g, pf);
    return true;
}

// $C2:E267
/* The cursor moved about the grid, with a tick where it did; the typing;
 * the name and its cursor shown. */
static bool move_on_the_name_grid(GameState *g, Platform *pf, const NamingSteps *n, void *ctx)
{
    (void)pf;
    bool moved = menu_naming_move_on_the_grid(g);
    n->note(ctx, NAMING_NOTE_MOVED_ON_THE_GRID, 0);
    if (moved && !n->sound(ctx, 0xC2E287, MENU_SOUND_CURSOR))
        return false;
    n->note(ctx, NAMING_NOTE_BUTTONS_TESTED, 0);
    if (!n->routine(ctx, NAMING_TYPE_ON_THE_GRID))
        return false;
    menu_naming_show_the_name(g);
    n->note(ctx, NAMING_NOTE_NAME_QUEUED, 0);
    return true;
}

// $C2:E3E0
/* The name kept checked against every character's: where someone has it,
 * the refusal's buzz. */
static bool check_the_name(GameState *g, Platform *pf, const NamingSteps *n, void *ctx)
{
    (void)pf;
    bool someone = menu_naming_someone_has_it(g);
    if (someone) {
        n->note(ctx, NAMING_NOTE_NAME_FOUND, 0);
        if (!n->sound(ctx, 0xC2E3FB, MENU_SOUND_REFUSAL))
            return false;
    }
    n->note(ctx, NAMING_NOTE_ANSWERED, !someone);
    return true;
}

// $C2:E363
/* The name kept and checked, and given where it can be. */
static bool accept_the_name(GameState *g, Platform *pf, const NamingSteps *n, void *ctx)
{
    (void)pf;
    menu_naming_keep_the_name(g);
    n->note(ctx, NAMING_NOTE_NAME_KEPT, 0);
    if (!n->routine(ctx, NAMING_CHECK_THE_NAME))
        return false;
    bool given = !menu_naming_someone_has_it(g) && menu_naming_give_the_name(g);
    n->note(ctx, NAMING_NOTE_ANSWERED, given);
    return true;
}

// $C2:E21E
/* A frame of the naming; Start accepts the name, and the menus close once
 * it is given. */
static bool name_the_character(GameState *g, Platform *pf, const NamingSteps *n, void *ctx)
{
    (void)pf;
    n->read_cursor(ctx);
    if (!n->routine(ctx, NAMING_MOVE_ON_THE_GRID))
        return false;
    if (!menu_naming_started(g))
        return true;
    n->note(ctx, NAMING_NOTE_START_PRESSED, 0);
    if (!n->routine(ctx, NAMING_ACCEPT_THE_NAME))
        return false;
    return !menu_naming_name_given(g) || n->ends(ctx, NAMING_LEAVES_THE_MENUS);
}

typedef bool (*NamingStepRun)(GameState *g, Platform *pf, const NamingSteps *n, void *ctx);

/* The steps and the routines, with what each takes of the state as the port
 * does (its entry's guard on the machine): a named one, the naming whole, the
 * cursor movable. */
typedef enum { TAKES_NOTHING_MORE = 0, TAKES_A_NAMED_ONE = 1, TAKES_THE_NAMING_WHOLE = 2, TAKES_A_MOVABLE_CURSOR = 4 } NamingTakes;

static const struct {
    uint32_t routine;
    NamingStepRun run;
    NamingTakes takes;
} steps[] = {
    [NAMING_OPEN_THE_SCREEN] = { 0xC2E1ED, open_naming_screen, TAKES_A_NAMED_ONE },
    [NAMING_NAME_THE_CHARACTER] = { 0xC2E21E, name_the_character, TAKES_THE_NAMING_WHOLE | TAKES_A_MOVABLE_CURSOR },
    [NAMING_CHOOSE_BATTLE_MODE] = { 0xC2E236, choose_battle_mode, TAKES_A_MOVABLE_CURSOR },
    [NAMING_OPEN_THE_NAMING] = { 0xC2E201, open_the_naming, TAKES_A_NAMED_ONE },
    [NAMING_WAIT_TO_NAME] = { 0xC2E209, wait_to_name, TAKES_NOTHING_MORE },
};

static bool takes(const GameState *g, NamingTakes what)
{
    return (!(what & TAKES_A_NAMED_ONE) || menu_naming_names_one(g)) &&
           (!(what & TAKES_THE_NAMING_WHOLE) || menu_naming_is_whole(g)) &&
           (!(what & TAKES_A_MOVABLE_CURSOR) || menu_cursor_can_move(g));
}

static const struct {
    uint32_t routine;
    NamingStepRun run;
} routines[] = {
    [NAMING_MOVE_ON_THE_GRID] = { 0xC2E267, move_on_the_name_grid },
    [NAMING_TYPE_ON_THE_GRID] = { 0xC2E2F4, type_on_the_name_grid },
    [NAMING_ACCEPT_THE_NAME] = { 0xC2E363, accept_the_name },
    [NAMING_CHECK_THE_NAME] = { 0xC2E3E0, check_the_name },
};

bool menu_naming_step_with(GameState *g, Platform *pf, NamingStep step, const NamingSteps *n, void *ctx)
{
    return steps[step].run(g, pf, n, ctx);
}

bool menu_naming_routine(GameState *g, Platform *pf, NamingRoutine routine, const NamingSteps *n, void *ctx)
{
    return routines[routine].run(g, pf, n, ctx);
}

uint32_t menu_naming_routine_address(NamingRoutine routine) { return routines[routine].routine; }

/* ---- The steps as C ------------------------------------------------------------------------------- */

typedef struct {
    GameState *game;
    Platform *pf;
} NamingAsC;

static void read_cursor(void *ctx)
{
    NamingAsC *c = ctx;
    TIMED_AS(c->game, 0xC2E984, menu_read_cursor(c->game));
}

static bool sound(void *ctx, uint32_t site, MenuSound sound)
{
    (void)site;
    NamingAsC *c = ctx;
    TIMED_AS(c->game, sound == MENU_SOUND_REFUSAL ? 0xC2EACC : 0xC2EAC2, menu_sound_played(c->game, c->pf, sound));
    return true;
}

static bool routine(void *ctx, NamingRoutine routine);

static void note(void *ctx, NamingNote note, uint8_t value) { (void)ctx, (void)note, (void)value; }

/* The steps' ends, as the menus' own as C: the fade in's task started, the
 * screen faded out, or the menus closed, which never comes back. */
static bool ends(void *ctx, NamingEnd end)
{
    NamingAsC *c = ctx;
    switch (end) {
    case NAMING_FADES_IN: TIMED_AS(c->game, 0xC283B2, menu_start_the_fade_in(c->game)); break;
    case NAMING_FADES_OUT: TIMED_AS(c->game, 0xC283CA, menu_fade_to_black(c->game)); break;
    case NAMING_LEAVES_THE_MENUS: menu_close(c->game, c->pf, false); break;
    }
    return true;
}

static const NamingSteps as_c = { read_cursor, sound, routine, note, ends };

static bool routine(void *ctx, NamingRoutine routine)
{
    NamingAsC *c = ctx;
    if (!menu_naming_is_whole(c->game)) {
        program_needs_the_machine(c->game, routines[routine].routine, "a naming the port doesn't take");
        return false;
    }
    bool back = false;
    TIMED_AS(c->game, routines[routine].routine, back = routines[routine].run(c->game, c->pf, &as_c, c));
    return back;
}

bool menu_naming_step(GameState *g, Platform *pf, NamingStep step)
{
    if (!takes(g, steps[step].takes)) {
        program_needs_the_machine(g, steps[step].routine, "a naming the port doesn't take");
        return false;
    }
    NamingAsC c = { g, pf };
    return steps[step].run(g, pf, &as_c, &c);
}
