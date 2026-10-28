/* The items screen (the menus' screen 2), and the main menu's panel for it.
 *
 * The screen shows the party's items ten rows at a time, each row an item's
 * name, how many the party has and what kind of item it is; a description
 * window above the list follows the cursor. Its steps (menu_screen_step):
 *
 * - the two commands above the list: the list itself, and a sort that puts
 *   the party's items in order (the items used up and the empty places
 *   dropped, the same items gathered into one place);
 * - the list: choosing an item picks it up, a second cursor staying on it;
 * - an item picked up: choosing another place swaps the two, choosing the
 *   item itself uses it. An item the menu can use (a tonic, a tab) opens a
 *   window of the members to use it on, chosen and used again until going
 *   back; a weapon or armour shows who can equip it, and its attack or
 *   defence, until a button closes the window.
 *
 * The list pages with the shoulder assignments and slides a row at a time
 * past its ends, the second cursor moving with the rows. The main menu shows
 * the list beside it while its cursor is on the items (the panel).
 *
 * This file is what the game does; item_screen_bridge.c runs it on the
 * original's machine, waiting for the next frame between the parts here
 * where the screen does. */
#include "menu/item_screen.h"
#include "menu/tech_screen.h"
#include "menu/healing.h"

#include "charge.h"
#include "core/program.h"
#include "cpu_io.h"
#include "menu/cursor.h"
#include "menu/description.h"
#include "menu/display.h"
#include "menu/figures.h"
#include "menu/held_cursors.h"
#include "menu/items.h"
#include "menu/lists.h"
#include "menu/main_menu.h"
#include "menu/members.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/sprites.h"
#include "menu/text.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "vars.h"
#include "wram.h"

/* The frame's input, as the cursor's reading leaves it (menu/cursor.h). */
static bool chose(const GameState *g) { return menu_asked(g, MENU_CHOOSE); }
static bool went_back(const GameState *g) { return menu_asked(g, MENU_GO_BACK); }

static void go_to(GameState *g, ItemsStep step) { set_menu_screen_step(g, (uint8_t)step); }

/* The cursor's places: the list's rows first, then the two commands. */
enum { COMMAND_LIST = 0x0A, COMMAND_SORT = 0x0B };

/* An item a kind of: one the menu can use (where the place allows it), and
 * one the party keeps rather than uses (the trade goods and the story's
 * items), which the sort puts after the others. */
enum { USABLE_FROM_THE_MENU = 0x02, KEPT_RATHER_THAN_USED = 0x08 };

static bool usable_from_the_menu(const GameState *g, uint8_t item) { return (menu_item_kinds(g, item) & USABLE_FROM_THE_MENU) != 0; }

/* No item in a place of the party's items, and none picked up. */
enum { NO_ITEM = 0, NOTHING_PICKED = 0xFFFF };

/* Asking for things again: the description window drawn again whatever it
 * shows, the scroll bar hidden. The original counts each down from where it
 * stands, which from rest is the request ($FF, its top bit set). */
static void ask_for_the_description_again(GameState *g) { set_menu_description_stale(g, (uint8_t)(menu_description_stale(g) - 1)); }
static void hide_the_scroll_bar(GameState *g) { set_menu_scroll_bar_hidden(g, (uint8_t)(menu_scroll_bar_hidden(g) - 1)); }
static void show_the_scroll_bar(GameState *g) { set_menu_scroll_bar_hidden(g, 0); }

/* The place in the party's items a row of the list shows: the list's first
 * place plus the row, a byte. */
static uint8_t place_of_row(const GameState *g, uint8_t row) { return (uint8_t)(menu_item_list_top(g) + row); }

/* ---- Opening the screen ------------------------------------------------------------------------ */

enum { SCREEN_LINE_EFFECTS = 0x39 };

/* The scroll bar's place, and the track the frame divides among the list's
 * places past its rows ($C2:EA81 divides it). */
enum { BAR_PLACE = 0x50E8, BAR_TRACK = 0x6800 };

// $C2:AA40
void menu_items_open(GameState *g) { go_to(g, ITEMS_ON_THE_LIST); }

// $C2:ADD0
void menu_items_lay_out(GameState *g, Platform *pf)
{
    menu_draw_windows(g, pf, MENU_WINDOWS_ITEMS_SCREEN);
    set_menu_scroll_bar_place(g, BAR_PLACE);
    set_menu_text_attribute(g, 0);
    set_menu_list_first_place(g, 0);
    TIMED_AS(g, 0xC2EA81, menu_frame_the_list(g, MENU_ITEMS, MENU_ITEMS_ROWS, BAR_TRACK));
}

void menu_items_show_bar_at_the_list(GameState *g) { set_menu_scroll_bar_thumb(g, menu_item_list_top(g)); }

void menu_items_put_the_cursor_back(GameState *g) { set_menu_cursor_index(g, menu_item_list_row(g)); }

void menu_items_show_screen(GameState *g)
{
    set_menu_line_effects(g, SCREEN_LINE_EFFECTS);
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER_WHOLE);
    TIMED_AS(g, 0xC2E923, menu_clear_sprites_but_cursor(g));
}

/* ---- The commands ------------------------------------------------------------------------------ */

// $C2:AA4A
ItemsCommandChoice menu_items_choose_command(const GameState *g)
{
    if (chose(g)) {
        switch (menu_cursor_index(g)) {
        case COMMAND_LIST: return COMMAND_LIST_CHOSEN;
        case COMMAND_SORT: return COMMAND_SORT_CHOSEN;
        default: return COMMAND_NOTHING_CHOSEN;
        }
    }
    return went_back(g) ? COMMANDS_LEFT : COMMAND_POINTED_AT;
}

void menu_items_take_command(GameState *g, ItemsCommandChoice c)
{
    if (c == COMMAND_LIST_CHOSEN) {
        show_the_scroll_bar(g);
        set_menu_cursor_index(g, menu_item_list_row(g));
        set_menu_description_stale(g, 0xFF);
        go_to(g, ITEMS_ON_THE_LIST);
    } else if (c == COMMAND_SORT_CHOSEN) {
        go_to(g, ITEMS_SORTING);
    }
}

/* ---- The list ---------------------------------------------------------------------------------- */

void menu_items_put_nothing_in_hand(GameState *g) { set_menu_item_picked(g, NOTHING_PICKED); }

// $C2:AA86
ItemsListChoice menu_items_choose_on_the_list(const GameState *g)
{
    if (chose(g))
        return LIST_ITEM_PICKED_UP;
    return went_back(g) ? LIST_LEFT : LIST_POINTED_AT;
}

/* The item on the cursor picked up; a second cursor held on its row as the
 * list stands now. */
void menu_items_pick_up(GameState *g)
{
    set_menu_item_picked(g, menu_item_list_place(g));
    go_to(g, ITEMS_MOVING_ONE);
    set_menu_held_cursor(g, menu_item_list_row(g));
    set_menu_held_cursor_top(g, menu_item_list_top(g));
    TIMED_AS(g, 0xC289E4, menu_hold_the_cursor_on_the_list(g));
}

/* Back to the commands, the cursor on the list's; the second cursor's
 * sprite put out of sight. */
enum { HELD_CURSOR_SPRITE = 0, OUT_OF_SIGHT = 0xE0 };

void menu_items_back_to_the_commands(GameState *g)
{
    ask_for_the_description_again(g);
    go_to(g, ITEMS_ON_THE_COMMANDS);
    hide_the_scroll_bar(g);
    set_menu_cursor_index(g, COMMAND_LIST);
    set_oam_buffer_y(g, HELD_CURSOR_SPRITE, OUT_OF_SIGHT);
}

// $C2:AAC8
ItemsMoveChoice menu_items_choose_where(const GameState *g)
{
    if (chose(g))
        return menu_item_picked(g) == menu_item_list_place(g) ? MOVE_ON_ITSELF : MOVE_PUT_DOWN;
    return went_back(g) ? MOVE_LEFT : MOVE_POINTED_AT;
}

/* The picked item and the one on the cursor change places, and how many of
 * each with them. */
void menu_items_swap_with_the_picked(GameState *g)
{
    uint16_t picked = menu_item_picked(g), place = menu_item_list_place(g);
    uint8_t item = inventory_items(g, picked), count = inventory_counts(g, picked);
    set_inventory_items(g, picked, inventory_items(g, place));
    set_inventory_items(g, place, item);
    set_inventory_counts(g, picked, inventory_counts(g, place));
    set_inventory_counts(g, place, count);
}

/* The list drawn again with the swap, and back on it. */

void menu_items_back_to_the_list(GameState *g)
{
    TIMED_AS(g, 0xC2AE43, menu_items_draw_rows(g));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    go_to(g, ITEMS_ON_THE_LIST);
}

void menu_items_back_to_the_list_unchanged(GameState *g) { go_to(g, ITEMS_ON_THE_LIST); }

/* The equipment's ids come before the rest's (menu_item_category_start). */
enum { FIRST_OF_THE_REST = 0xBC };

ItemsUse menu_items_use_of_the_picked(GameState *g)
{
    uint8_t item = inventory_items(g, menu_item_picked(g));
    set_menu_item_operand(g, item);
    if (usable_from_the_menu(g, item))
        return USING_FROM_THE_MENU;
    return item != NO_ITEM && item < FIRST_OF_THE_REST ? USING_SHOWS_USERS : USING_DOES_NOTHING;
}

void menu_items_start_showing_users(GameState *g)
{
    hide_the_scroll_bar(g);
    go_to(g, ITEMS_SHOWING_USERS);
}

/* ---- Who can equip an item --------------------------------------------------------------------- */

/* The window's two per-line tables: a stretch of lines cleared, then two
 * runs of a value a band. */
enum { USERS_LINES_AT = 0x4E00, USERS_LINES_BYTES = 0x80, USERS_FIRST_BAND = 0x4E1E, USERS_SECOND_BAND = 0x4E5E };
enum { USERS_BAND_WORDS = 0x0E, USERS_FIRST_VALUE = 0x6090, USERS_SECOND_VALUE = 0x60AF };

// $C2:AFB5
static void lay_out_window_lines(GameState *g)
{
    wram_fill8(g, 0x7E0000u | USERS_LINES_AT, USERS_LINES_BYTES, 0);
    wram_fill16(g, 0x7E0000u | USERS_FIRST_BAND, USERS_BAND_WORDS, USERS_FIRST_VALUE);
    wram_fill16(g, 0x7E0000u | USERS_SECOND_BAND, USERS_BAND_WORDS, USERS_SECOND_VALUE);
    ORIGINAL_EFFECT(charge_block_move(g, USERS_LINES_BYTES + 4 * USERS_BAND_WORDS));
}

/* The names of who can equip the item and has joined the party, the first
 * character's first, one after another. The original finds each name's
 * place with the multiplier, which it leaves with the last's; its scratch
 * cells keep the bits it shifted out and how far it counted. */
enum { NAME_BYTES = 6, CHARACTERS_BITS = 8, FIRST_CHARACTER_BIT = 0x80 };

static void leave_users_counted(GameState *g, uint8_t users)
{
    hw_set_multiplicand(g, NAME_BYTES);
    uint8_t counted = 0;
    for (uint8_t character = 0, rest = users; rest; character++, rest = (uint8_t)(rest << 1))
        if (rest & FIRST_CHARACTER_BIT)
            (void)hw_multiply_by(g, character);
    for (uint8_t rest = users; rest; rest = (uint8_t)(rest << 1))
        counted++;
    set_menu_scratch_users_bits(g, 0);
    set_menu_scratch_users_counted(g, counted);
}

// $C2:AEB6
static void list_users(GameState *g, uint8_t item)
{
    for (unsigned i = 0; i < MENU_ITEM_USERS_NAMES_COUNT; i++)
        for (unsigned at = 0; at < NAME_BYTES; at++)
            set_menu_item_users_names_byte(g, i, at, 0);
    uint8_t users = (uint8_t)(menu_item_users(g, item) & party_characters_joined(g));
    unsigned listed = 0;
    for (uint8_t character = 0; character < CHARACTERS_BITS; character++)
        if (users & (FIRST_CHARACTER_BIT >> character)) {
            for (unsigned at = 0; at < NAME_BYTES; at++)
                set_menu_item_users_names_byte(g, listed, at, character_names_byte(g, character, at));
            listed++;
        }
    ORIGINAL_EFFECT(charge_block_move(g, MENU_ITEM_USERS_NAMES_COUNT * NAME_BYTES + listed * NAME_BYTES));
    ORIGINAL_EFFECT(leave_users_counted(g, users));
}

/* The item's attack or defence, and its label from the category's: the
 * label copied whole, then an end. An accessory has no power of its own: the
 * original reads the byte past the table the menus build, as it stands
 * (docs/TODO.md). */
enum { LABEL_BYTES = 11, LABEL_END = 0xFF };

// $C2:AF0B
static void note_power(GameState *g, uint8_t item)
{
    set_menu_item_power(g, menu_power_of_item(g, item));
    MenuItemPlace p = { 0, 0 };
    TIMED_AS(g, 0xC28881, p = menu_item_place(g, item));
    uint16_t label = menu_item_power_label_at(g, p.category);
    for (unsigned i = 0; i < LABEL_BYTES; i++)
        set_menu_item_power_label_byte(g, i, menu_window_byte(g, (uint16_t)(label + i)));
    set_menu_item_power_label_byte(g, LABEL_BYTES, LABEL_END);
    set_menu_item_power_label_byte(g, LABEL_BYTES + 1, 0);
    ORIGINAL_EFFECT(charge_block_move(g, LABEL_BYTES));
}

enum { USERS_WINDOW = 0xC0D5, USERS_LINE_EFFECT = 0x04 };

// $C2:AE91
void menu_items_show_users(GameState *g, Platform *pf)
{
    uint8_t item = menu_item_operand(g);
    lay_out_window_lines(g);
    list_users(g, item);
    note_power(g, item);
    menu_draw_windows(g, pf, USERS_WINDOW);
    set_menu_line_effects(g, (uint8_t)(menu_line_effects(g) | USERS_LINE_EFFECT));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER_LOWER_HALF);
    menu_queue_fixed_upload(g, MENU_UPLOAD_WINDOW_LINES);
}

// $C2:AC1D
/* The window stays while the cursor is held; any button closes it. */
bool menu_items_users_closed(const GameState *g) { return chose(g) || went_back(g); }

void menu_items_close_users(GameState *g)
{
    set_menu_line_effects(g, (uint8_t)(menu_line_effects(g) & ~USERS_LINE_EFFECT));
    go_to(g, ITEMS_ON_THE_LIST);
    show_the_scroll_bar(g);
}

/* ---- Sorting the party's items ----------------------------------------------------------------- */

/* The sort gathers how many the party has of each item (a place's count
 * added to the item's, held under 100: the original adds in a byte, so a sum
 * past 255 wraps first), then writes the items back in their categories'
 * order: the rest (the tonics and their like) first, then what the party
 * keeps rather than uses, then the weapons, the armour, the helmets and the
 * accessories. The places past the last are cleared, unless every place was
 * filled (or none). Among the cleared cells are the first equipment list's
 * places past two ($0F02), which the original clears with the tables. */
enum { MOST_OF_AN_ITEM = 99, CLEARED_LIST_FROM = 2, CLEARED_LIST_PLACES = 32, PLACES = 0x100 };
enum { SORT_ORDER = MENU_ITEM_CATEGORIES };

static void place_sorted(GameState *g, unsigned *place, uint8_t item)
{
    set_inventory_items_unchecked(g, *place, item);
    set_inventory_counts_unchecked(g, *place, menu_sort_counts(g, item));
    *place = (*place + 1) % PLACES;
}

static bool kept_rather_than_used(const GameState *g, uint8_t item) { return (menu_item_kinds(g, item) & KEPT_RATHER_THAN_USED) != 0; }

static void place_category(GameState *g, unsigned *place, unsigned order)
{
    uint8_t first = menu_sort_category_first(g, order);
    for (unsigned i = 0; i < menu_sort_category_items(g, order); i++) {
        uint8_t item = (uint8_t)(first + i);
        if (!kept_rather_than_used(g, item) && menu_sort_held(g, item) != NO_ITEM)
            place_sorted(g, place, menu_sort_held(g, item));
    }
}

// $C2:AB62
void menu_items_sort(GameState *g)
{
    for (unsigned item = 0; item < MENU_SORT_HELD_COUNT; item++) {
        set_menu_sort_held(g, item, 0);
        set_menu_sort_counts(g, item, 0);
    }
    for (unsigned i = 0; i < CLEARED_LIST_PLACES; i++)
        set_menu_list_items(g, CLEARED_LIST_FROM + i, 0);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * MENU_SORT_HELD_COUNT + CLEARED_LIST_PLACES));
    ORIGINAL_EFFECT(set_menu_scratch_save_member(g, 0));
    for (unsigned at = 0; at < PLACES; at++) {
        uint8_t item = inventory_items_unchecked(g, at);
        if (item == NO_ITEM)
            continue;
        set_menu_sort_held(g, item, item);
        uint8_t sum = (uint8_t)(inventory_counts_unchecked(g, at) + menu_sort_counts(g, item));
        set_menu_sort_counts(g, item, sum < MOST_OF_AN_ITEM + 1 ? sum : MOST_OF_AN_ITEM);
    }
    unsigned place = 0;
    place_category(g, &place, 0);
    for (unsigned item = 0; item < MENU_ITEMS; item++)
        if (menu_sort_held(g, item) != NO_ITEM && kept_rather_than_used(g, (uint8_t)item)) {
            place_sorted(g, &place, (uint8_t)item);
            set_menu_sort_held(g, item, 0);
        }
    for (unsigned order = 1; order < SORT_ORDER; order++)
        place_category(g, &place, order);
    if (place != 0)
        for (; place < PLACES; place++) {
            set_inventory_items_unchecked(g, place, NO_ITEM);
            set_inventory_counts_unchecked(g, place, 0);
        }
}

void menu_items_after_sorting(GameState *g)
{
    TIMED_AS(g, 0xC2AE43, menu_items_draw_rows(g));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    set_menu_cursor_index(g, COMMAND_LIST);
    go_to(g, ITEMS_ON_THE_COMMANDS);
}

/* ---- Pointing along the list ------------------------------------------------------------------- */

// $C2:AC3A
void menu_items_turn_page(GameState *g, uint8_t top)
{
    set_menu_item_list_top(g, top);
    TIMED_AS(g, 0xC2AE43, menu_items_draw_rows(g));
}

/* The cursor's place in the party's items, as a byte: one before the first
 * at the top's row 0, the last at the bottom's. */
enum { FIRST_PAST_THE_ENDS = 0xF0 };

bool menu_items_slide_brings_a_row(const GameState *g)
{
    if (!menu_list_scrolls_a_row(g))
        return false;
    uint8_t before_the_cursor = (uint8_t)(menu_cursor_index(g) + menu_item_list_top(g) - 1);
    return before_the_cursor < FIRST_PAST_THE_ENDS;
}

/* The slide moves the list's eleven rows (with the one coming in), and the
 * per-line table its block move brings their lines in from. */
enum { LIST_LINE_TABLE = 0x9186, LIST_LINE_TABLE_BYTES = 0x25, LIST_ROWS_SLID = 0x0B };

void menu_items_set_up_slide(GameState *g)
{
    set_menu_block_move_dest(g, LIST_LINE_TABLE);
    set_menu_block_move_size(g, LIST_LINE_TABLE_BYTES);
    set_menu_slide_rows(g, LIST_ROWS_SLID);
}

/* The list's first place a row on (or back), the bar with it, and the row
 * coming in drawn below the list's last row (above its first, for a slide
 * up: the rows' buffer has a row more than the list shows, which the move at
 * the slide's end turns round). The second cursor starts a row off its item,
 * the way the rows move. */
enum { INCOMING_ROW_AT = 0x344A, HELD_CURSOR_ROW_LINES = 12 };

void menu_items_bring_in_a_row(GameState *g)
{
    uint8_t top = menu_item_list_top(g);
    bool down = menu_pushed(g, MENU_PUSHED_DOWN);
    set_menu_item_list_top(g, down ? (uint8_t)(top + 1) : (uint8_t)(top - 1));
    set_menu_held_cursor_shift(g, down ? HELD_CURSOR_ROW_LINES : (uint16_t)-HELD_CURSOR_ROW_LINES);
    set_menu_scroll_bar_thumb(g, menu_item_list_top(g));
    TIMED_AS(g, 0xC2AE65, menu_items_draw_row(g, INCOMING_ROW_AT, menu_cursor_index(g)));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

/* A held push repeating fast skips the slide's frames. */
enum { REPEATING_FAST = 0x80 };

bool menu_items_slide_takes_frames(GameState *g)
{
    if (menu_fast_repeat(g) & REPEATING_FAST)
        return false;
    set_menu_scroll_moved(g, 0);
    return true;
}

/* A frame of the slide: the rows a step on, the second cursor a step back
 * to its row. Whether frames are left. */
bool menu_items_slide_a_frame(GameState *g)
{
    uint16_t step = menu_scroll_step(g);
    set_menu_scroll_moved(g, (uint16_t)(menu_scroll_moved(g) + step));
    set_menu_held_cursor_shift(g, (uint16_t)(menu_held_cursor_shift(g) - step));
    uint16_t frames = (uint16_t)(menu_scroll_frames(g) - 1);
    set_menu_scroll_frames(g, frames);
    return frames != 0;
}

/* The slide over: the second cursor on its row (the shift's low byte, its
 * high one as it stands), the upload queue emptied (the frames sent it). */
void menu_items_end_slide_frames(GameState *g)
{
    set_menu_held_cursor_shift(g, (uint16_t)(menu_held_cursor_shift(g) & 0xFF00));
    set_menu_vram_queue_bytes(g, 0);
}

/* Then the block move set to bring the rows' new lines in, the rows moved a
 * row along in their buffer, and the list at rest. */
enum { LIST_ROWS_AT = 0x2F00, ROW_BYTES = 0x80 };

void menu_items_finish_slide(GameState *g)
{
    menu_put_the_list_bands_back(g);
    menu_move_list_rows(g, LIST_ROWS_AT, MENU_ITEMS_ROWS * ROW_BYTES, ROW_BYTES, menu_list_slid_up(g));
    set_menu_scroll_step(g, 0);
}

// $C2:AD2E
uint8_t menu_items_note_the_cursors_item(GameState *g)
{
    uint8_t place = place_of_row(g, menu_cursor_index(g));
    set_menu_item_list_place(g, place);
    set_menu_item_operand_count(g, inventory_counts(g, place));
    set_menu_item_operand(g, inventory_items(g, place));
    return menu_item_operand(g);
}

bool menu_items_keep_the_cursors_row(GameState *g)
{
    set_menu_item_list_row(g, menu_cursor_index(g));
    return menu_cursor_index(g) != menu_cursor_frame_start(g);
}

void menu_items_queue_rows(GameState *g) { menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER); }

/* ---- Drawing the list -------------------------------------------------------------------------- */

/* A row: the name (ten glyphs after the item's icon), a colon in the text's
 * colours and the count in two digits, nine tiles cleared and the kind's
 * label over them. An empty place is a row of blanks. */
enum { NAME_SETTINGS = 0x0B, COUNT_FORMAT = 0x11, LABEL_SETTINGS = 0x09, LABEL_TILES = 9, TILE_BYTES = 2 };
enum { BLANK_ROW_SETTINGS = 0x18, USABLE_ATTRIBUTE = 0x08 };
enum { FIRST_ROW_AT = 0x2F4A };

static void leave_row_drawn(GameState *g, uint8_t item, uint8_t count)
{
    set_menu_item_row_item(g, item);
    set_menu_item_row_count(g, count);
}

// $C2:AE65, $C2:AE75, $C2:B04B
void menu_items_draw_row(GameState *g, uint16_t at, uint8_t row)
{
    uint8_t place = place_of_row(g, row);
    uint8_t item = inventory_items(g, place), count = inventory_counts(g, place);
    ORIGINAL_EFFECT(leave_row_drawn(g, item, count));
    if (usable_from_the_menu(g, item))
        set_menu_text_attribute(g, (uint8_t)(menu_text_attribute(g) | USABLE_ATTRIBUTE));
    if (item == NO_ITEM) {
        (void)menu_draw_blanks(g, at, BLANK_ROW_SETTINGS);
        return;
    }
    uint16_t pen = at;
    pen = menu_draw_string(g, MENU_ITEM_TABLES_BANK | menu_item_name_places(g, item), pen, NAME_SETTINGS);
    pen = menu_draw_colon(g, pen);
    pen = menu_draw_number_value(g, count, COUNT_FORMAT, pen);
    pen = (uint16_t)(pen + TILE_BYTES);
    wram_fill8(g, MENU_NAMES_BANK | pen, LABEL_TILES * TILE_BYTES, 0);
    ORIGINAL_EFFECT(charge_block_move(g, LABEL_TILES * TILE_BYTES));
    (void)menu_draw_string(g, menu_item_kind_label(g, item), pen, LABEL_SETTINGS);
}

// $C2:AE43
void menu_items_draw_rows(GameState *g)
{
    for (uint8_t row = 0; row < MENU_ITEMS_ROWS; row++)
        TIMED_AS(g, 0xC2AE65, menu_items_draw_row(g, (uint16_t)(FIRST_ROW_AT + row * ROW_BYTES), row));
}

// $C2:AE20
/* The row the cursor was left on (a row of the list's ten, in its low four
 * bits), drawn again and queued. */
enum { ROW_BITS = 0x0F };

void menu_items_redraw_the_cursors_row(GameState *g)
{
    uint8_t row = menu_item_list_row(g);
    TIMED_AS(g, 0xC2AE65, menu_items_draw_row(g, (uint16_t)(FIRST_ROW_AT + (row & ROW_BITS) * ROW_BYTES), row));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
}

/* ---- The main menu's panel --------------------------------------------------------------------- */

/* The panel's window, the text's colours at rest, the description forgotten
 * (none shown, and drawn again), the list's colours (eight into row 5's
 * first half, four into row 2's second), and the main menu's fourth task held
 * two frames longer. */
enum { PANEL_WINDOW = 0xC05D, NO_DESCRIPTION = 0xFF };
enum { LIST_COLOURS = 8, FIRST_LIST_COLOUR = 0x50, HIGHLIGHT_COLOURS = 4, FIRST_HIGHLIGHT_COLOUR = 0x20 };
enum { PANEL_ROWS_AT = 0x4E80, PANEL_ROWS_BYTES = 0x200 };

// $C2:AD50
void menu_items_panel_lay_out(GameState *g, Platform *pf)
{
    menu_draw_windows(g, pf, PANEL_WINDOW);
    set_menu_fill_tile(g, 0);
    set_menu_text_attribute(g, 0);
    set_menu_description_item(g, NO_DESCRIPTION);
    set_menu_description_stale(g, 0xFF);
    for (unsigned i = 0; i < LIST_COLOURS; i++)
        set_menu_palette(g, FIRST_LIST_COLOUR + i, menu_rom_colour(g, MENU_ROM_ITEMS_LIST_COLOURS, i));
    for (unsigned i = 0; i < HIGHLIGHT_COLOURS; i++)
        set_menu_palette(g, FIRST_HIGHLIGHT_COLOUR + i, menu_rom_colour(g, MENU_ROM_ITEMS_HIGHLIGHT_COLOURS, i));
    ORIGINAL_EFFECT(charge_block_move(g, 2 * (LIST_COLOURS + HIGHLIGHT_COLOURS)));
    set_menu_tasks_countdown(g, MENU_CURSOR_TASK, (uint8_t)(menu_tasks_countdown(g, MENU_CURSOR_TASK) + 2));
}

/* The rows, after a frame: the panel's second layer cleared, then the list
 * drawn unless the story's second note keeps it hidden. */
void menu_items_panel_draw_rows(GameState *g)
{
    wram_fill8(g, 0x7E0000u | PANEL_ROWS_AT, PANEL_ROWS_BYTES, 0);
    ORIGINAL_EFFECT(charge_block_move(g, PANEL_ROWS_BYTES));
    if (menu_story_items(g, 1) == 0)
        TIMED_AS(g, 0xC2AE43, menu_items_draw_rows(g));
    menu_queue_fixed_upload(g, MENU_UPLOAD_TEXT_LAYER);
}

/* After another frame: the colour math table built, the third layer a line
 * down, the panel's line effects, the sprites cleared, and its layers
 * queued. */
enum { PANEL_LINE_EFFECTS = 0x19, THIRD_LAYER = 2, PANEL_THIRD_LAYER_Y = 1 };

void menu_items_panel_show(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC286DD, menu_build_math_table(g, pf));
    set_menu_scroll_y(g, THIRD_LAYER, PANEL_THIRD_LAYER_Y);
    set_menu_line_effects(g, PANEL_LINE_EFFECTS);
    TIMED_AS(g, 0xC2984A, menu_clear_sprites(g));
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER);
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
}

// $C2:AD50
/* The panel: its window and colours, a frame, the rows, a frame, the colour
 * math and the sprites. */
bool menu_show_items_panel_with(GameState *g, Platform *pf, const MainMenuSteps *n, void *ctx)
{
    menu_items_panel_lay_out(g, pf);
    if (!n->wait(ctx, MAIN_MENU_AT_THE_ITEMS_LAID_OUT))
        return false;
    menu_items_panel_draw_rows(g);
    if (!n->wait(ctx, MAIN_MENU_AT_THE_ITEMS_DRAWN))
        return false;
    menu_items_panel_show(g, pf);
    return true;
}

// $C2:ADC3
void menu_items_panel_put_away(GameState *g)
{
    TIMED_AS(g, 0xC29121, menu_put_items_back(g));
    set_menu_line_effects(g, PANEL_LINE_EFFECTS);
}

/* ---- Using an item ----------------------------------------------------------------------------- */

/* What an item does is in its figures: the first byte's first set bit from
 * the top says how it is used (none of the five, a tab); the second byte
 * whether it restores HP (bit 7) or MP (bit 6); the third's low four bits
 * the restoring's multiplier, 15 meaning all of it. Its targets, the rest's
 * table of a byte an item, count from 3 for one member. */
enum { FIGURES_USE = 0, FIGURES_RESTORES = 1, FIGURES_FACTOR = 2 };
enum { TARGETS_BITS = 0x03, TARGETS_SHIFT = 2, FACTOR_BITS = 0x0F, FACTOR_ALL = 0x0F, ALL_OF_IT = 0x40 };
enum { USE_KINDS = 5 };

static const ItemUseKind USE_BY_FIRST_BIT[USE_KINDS] = {
    USE_RESTORES, USE_DOES_NOTHING, USE_DOES_NOTHING, USE_RESTORES_BY_CHANCE, USE_RESTS_THE_PARTY,
};

static ItemUseKind use_of(uint8_t bits)
{
    for (unsigned i = 0; i < USE_KINDS; i++)
        if (bits & (0x80 >> i))
            return USE_BY_FIRST_BIT[i];
    return USE_RAISES_A_FIGURE;
}

// $C2:B0AB, $C2:B0FA
ItemUseKind menu_items_look_up_use(GameState *g, uint8_t item)
{
    MenuItemPlace p = { 0, 0 };
    TIMED_AS(g, 0xC28881, p = menu_item_place(g, item));
    uint8_t effect = (uint8_t)(((menu_item_targets(g, p.index) + 1) & TARGETS_BITS) << TARGETS_SHIFT);
    uint16_t figures = 0;
    TIMED_AS(g, 0xC288D8, figures = menu_item_figures_at_of_last(g));
    set_menu_use_effect(g, (uint8_t)(effect | menu_item_table_byte(g, (uint16_t)(figures + FIGURES_RESTORES))));
    uint8_t factor = (uint8_t)(menu_item_table_byte(g, (uint16_t)(figures + FIGURES_FACTOR)) & FACTOR_BITS);
    set_menu_heal_factor(g, factor == FACTOR_ALL ? ALL_OF_IT : factor);
    return use_of(menu_item_table_byte(g, (uint16_t)(figures + FIGURES_USE)));
}

/* The restoring's strength: a fixed amount for each step of the item's
 * factor, 50 HP where it restores HP, else 10 MP. */
enum { FIXED_AMOUNT = 0x80, HP_A_STEP = 50, MP_A_STEP = 10 };
enum { HP_STRENGTH = FIXED_AMOUNT | HP_A_STEP, MP_STRENGTH = FIXED_AMOUNT | MP_A_STEP };

/* The effect's low bits, which the figures carry besides what the restoring
 * reads and the outcomes by chance set as the figures would. */
enum { FIGURES_BIT_1 = 0x02, FIGURES_BIT_0 = 0x01 };

// $C2:B17F
void menu_items_set_restore_strength(GameState *g)
{
    set_menu_heal_power(g, (menu_use_effect(g) & MENU_USE_RESTORES_HP) ? HP_STRENGTH : MP_STRENGTH);
}

/* An item used by chance draws what it does each time: a random number
 * under 100 (the frame's random byte, scaled through the multiplier) picks
 * one of four outcomes by the chances' limits: all of HP and MP, all of HP,
 * 30 MP, 50 HP. */
enum { CHANCES = 4, CHANCE_SCALE = 100 };

typedef struct {
    uint8_t effect, factor, power;
    bool sets_power;
} Chance;

static const Chance CHANCE_OUTCOMES[CHANCES] = {
    { MENU_USE_RESTORES_HP | MENU_USE_RESTORES_MP | FIGURES_BIT_1, ALL_OF_IT, 0, false },
    { MENU_USE_RESTORES_HP | FIGURES_BIT_1, ALL_OF_IT, 0, false },
    { MENU_USE_RESTORES_MP | FIGURES_BIT_0, 3, MP_STRENGTH, true },
    { MENU_USE_RESTORES_HP, 1, HP_STRENGTH, true },
};

static void leave_chance_drawn(GameState *g, uint8_t random)
{
    hw_set_multiplicand(g, CHANCE_SCALE);
    (void)hw_multiply_by(g, random);
}

// $C2:B10D
void menu_items_draw_a_chance(GameState *g)
{
    uint8_t random = menu_random_byte(g, menu_frame_counter(g));
    ORIGINAL_EFFECT(leave_chance_drawn(g, random));
    uint8_t drawn = (uint8_t)((CHANCE_SCALE * random) >> 8);
    unsigned outcome = 0;
    while (outcome < CHANCES - 1 && drawn >= menu_chance_limit(g, outcome))
        outcome++;
    const Chance *c = &CHANCE_OUTCOMES[outcome];
    set_menu_use_effect(g, c->effect);
    set_menu_heal_factor(g, c->factor);
    if (c->sets_power)
        set_menu_heal_power(g, c->power);
}

/* The party's rest: all of HP and MP for every member. */
enum { REST = MENU_USE_RESTORES_HP | MENU_USE_RESTORES_MP | MENU_USE_FOR_EVERY_MEMBER | FIGURES_BIT_1 };

// $C2:B1F8
void menu_items_set_party_rest(GameState *g) { set_menu_use_effect(g, REST); }

/* A tab's figure, by the item: the power tab's, the magic tab's, else the
 * speed tab's (the figures' indexes into $FF:CF2B's offsets and $FF:CF32's
 * caps). */
enum { POWER = 1, MAGIC = 6, SPEED = 2 };

// $C2:B200
void menu_items_set_figure_raised(GameState *g, uint8_t item)
{
    set_menu_use_effect(g, item == MENU_POWER_TAB ? POWER : item == MENU_MAGIC_TAB ? MAGIC : SPEED);
}

/* The member window: its rows from cursor place 12, which the second cursor
 * holds too, the scroll bar hidden the while. */
enum { FIRST_MEMBER_PLACE = 0x0C };

void menu_items_start_member_choice(GameState *g)
{
    set_menu_cursor_index(g, FIRST_MEMBER_PLACE);
    set_menu_held_cursor(g, FIRST_MEMBER_PLACE);
    set_menu_use_first_member(g, FIRST_MEMBER_PLACE);
    set_menu_scroll_bar_hidden(g, FIRST_MEMBER_PLACE);
}

/* The window over the list, the seventh record's sprite unset (the records'
 * reset, $C2:F566, comes next), and the window's per-line tables. */
enum { MEMBER_WINDOW = 0xC0E4, LAST_RECORD = 7, NO_SPRITE = 0xFF };

void menu_items_lay_out_member_window_lines(GameState *g)
{
    lay_out_window_lines(g);
    menu_queue_fixed_upload(g, MENU_UPLOAD_WINDOW_LINES);
}

// $C2:AF42
void menu_items_open_member_window(GameState *g, Platform *pf)
{
    menu_draw_windows(g, pf, MEMBER_WINDOW);
    set_menu_record_sprite(g, LAST_RECORD, NO_SPRITE);
}

/* The members' rows in the window, each the member's face and name (its row
 * of the window list), from the window's first row down, each five tile rows
 * on from where the last row's list left the drawing origin; each member's
 * sprite stands beside its row, shown, and acting hurt where its row says
 * the member's HP is low. The window's layer is queued. */
enum { MEMBER_ROWS_AT = 0x3580, MEMBER_ROW_BYTES = 0x140, MEMBER_ROW_LIST = 0xBE0E };
enum { HURT_ANIMATION = 0x12, STANDING = 0, RECORD_BYTES = 0x40 };

/* The record of the character a member row shows: the original works out
 * its offset from the first ($C2:F626) with the multiplier. */
static void leave_record_found(GameState *g, uint8_t character)
{
    hw_set_multiplicand(g, character);
    set_menu_record_offset(g, hw_multiply_by(g, RECORD_BYTES));
}

void menu_items_draw_members(GameState *g, Platform *pf)
{
    set_menu_fill_tile(g, 0);
    set_menu_text_attribute(g, 0);
    set_menu_draw_origin(g, MEMBER_ROWS_AT);
    uint8_t member = 0;
    do {
        bool loaded = false;
        TIMED_AS(g, 0xC28820, loaded = menu_load_member(g, member));
        if (!loaded)
            break;
        TIMED_AS(g, 0xC2A1EF, menu_note_member_face_and_hp(g));
        menu_draw_windows(g, pf, MEMBER_ROW_LIST);
        uint8_t character = menu_status_record_bytes_byte(g, 0);
        ORIGINAL_EFFECT(leave_record_found(g, character));
        MenuSpritePlace place = menu_member_sprite_place(g, member);
        set_menu_record_y(g, character, place.y);
        set_menu_record_x(g, character, (uint16_t)((menu_record_x(g, character) & 0xFF00) | place.x));
        set_menu_draw_origin(g, (uint16_t)(menu_draw_origin(g) + MEMBER_ROW_BYTES));
        set_menu_record_hidden(g, character, 0);
        set_menu_record_animation(g, character, menu_member_marked(g) ? HURT_ANIMATION : STANDING);
        member++;
    } while (member < menu_active_members(g));
    set_menu_screen_argument(g, member);
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER_LOWER_HALF);
}

/* The window's sprites held back a frame while the records' task loads
 * them (a block of tiles a frame), then let through two at a time. */
enum { RECORDS = 7, HELD_BACK = 0x01, BLOCKS_WHILE_LOADING = 1, BLOCKS_AFTER = 2 };

void menu_items_hide_member_sprites(GameState *g)
{
    set_menu_record_block_limit(g, BLOCKS_WHILE_LOADING);
    for (unsigned r = 0; r < RECORDS; r++)
        set_menu_record_hidden(g, r, (uint8_t)(menu_record_hidden(g, r) | HELD_BACK));
}

enum { MEMBER_LINE_EFFECT = 0x04 };

void menu_items_show_member_sprites(GameState *g)
{
    set_menu_record_block_limit(g, BLOCKS_AFTER);
    for (unsigned r = 0; r < RECORDS; r++)
        set_menu_record_hidden(g, r, (uint8_t)(menu_record_hidden(g, r) & ~HELD_BACK));
    set_menu_use_member(g, 0);
    set_menu_use_helps(g, 0);
    set_menu_line_effects(g, (uint8_t)(menu_line_effects(g) | MEMBER_LINE_EFFECT));
}

/* The members' window's input: going back closes it, choosing uses the item
 * on the member pointed at. */
ItemsMemberChoice menu_items_choose_member(const GameState *g)
{
    if (went_back(g))
        return MEMBERS_LEFT;
    return chose(g) ? MEMBER_CHOSEN_TO_USE_ON : MEMBER_POINTED_AT_TO_USE_ON;
}

bool menu_items_restores_every_member(const GameState *g) { return (menu_use_effect(g) & MENU_USE_FOR_EVERY_MEMBER) != 0; }

// $C2:B24A
/* A tab's member: the cursor taken round the active members' rows (up past
 * the first to the last, down past the last to the first). */
void menu_items_take_the_cursor_round(GameState *g)
{
    uint8_t first = menu_use_first_member(g), active = menu_active_members(g);
    if ((uint8_t)(menu_cursor_index(g) - first) >= active)
        set_menu_cursor_index(g, menu_pushed(g, MENU_PUSHED_UP) ? (uint8_t)(active - 1 + first) : first);
}

// $C2:B275
/* The member pointed at noted: whether it is another than before. */
bool menu_items_point_at_a_member(GameState *g)
{
    set_menu_use_helps(g, 0);
    uint8_t member = (uint8_t)(menu_cursor_index(g) - menu_use_first_member(g));
    menu_work_on_member(g, member);
    bool another = member != menu_use_member(g);
    set_menu_use_member(g, member);
    return another;
}

/* A character's record has its figures from $0B and their bases from $2F;
 * a tab's figure is a byte of each (menu_figure_offset, menu_base_figure_
 * offset), the figure capped (menu_figure_cap) and the base at 99. */
enum { FIGURES_AT = 0x0B, BASES_AT = 0x2F, MOST_OF_A_BASE = 99, TAB_HELPS = 0x02 };

/* Then the member loaded and the tab's figure looked at: it helps where the
 * figure is short of its cap. */
void menu_items_look_at_the_member(GameState *g)
{
    TIMED_AS(g, 0xC28820, (void)menu_load_member(g, menu_use_member(g)));
    uint8_t figure = menu_use_effect(g);
    if (figure != 0
        && menu_status_record_bytes_byte(g, FIGURES_AT + menu_figure_offset(g, figure)) < menu_figure_cap(g, figure))
        set_menu_use_helps(g, TAB_HELPS);
}

/* The sprites let through two blocks a frame again. */
void menu_items_after_looking(GameState *g) { set_menu_record_block_limit(g, BLOCKS_AFTER); }

bool menu_items_use_helps(const GameState *g) { return menu_use_helps(g) != 0; }

/* A tab used: its sparkle's colours, and the amounts the members' rows show
 * (their HP's three then their MP's): none but a 1 at the member's place,
 * the original's index taken in two bits. Their showing ($C2:8F6C) comes
 * next. */
enum { SPARKLE_COLOURS = 4, FIRST_SPARKLE_COLOUR = 0x84, SHOWN_NONE = 0xFFFF, SHOWN_RAISED = 1, SHOWN_PLACE_BITS = 0x03 };

void menu_items_show_the_raise(GameState *g)
{
    for (unsigned i = 0; i < SPARKLE_COLOURS; i++)
        set_menu_palette(g, FIRST_SPARKLE_COLOUR + i, menu_rom_colour(g, MENU_ROM_PANEL_HIGHLIGHT_COLOURS, i));
    for (unsigned i = 0; i < MENU_HEAL_SHOWN_COUNT; i++)
        set_menu_heal_shown(g, i, SHOWN_NONE);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * SPARKLE_COLOURS + 2 * MENU_HEAL_SHOWN_COUNT));
    set_menu_heal_shown(g, menu_use_member(g) & SHOWN_PLACE_BITS, SHOWN_RAISED);
}

/* The figure raised by one in the member's record (a word, as the original
 * adds it, into the byte after where it is 255), and its base up to 99; the
 * member loaded before and again after. */
void menu_items_raise_the_figure(GameState *g)
{
    uint8_t member = menu_use_member(g);
    menu_work_on_member(g, member);
    TIMED_AS(g, 0xC28820, (void)menu_load_member(g, member));
    uint8_t figure = menu_use_effect(g), character = menu_member_character(g);
    unsigned at = FIGURES_AT + menu_figure_offset(g, figure);
    uint16_t raised = (uint16_t)((character_records_record_byte(g, character, at)
                                  | character_records_record_byte(g, character, at + 1) << 8)
                                 + 1);
    set_character_records_record_byte(g, character, at, (uint8_t)raised);
    set_character_records_record_byte(g, character, at + 1, (uint8_t)(raised >> 8));
    unsigned base = BASES_AT + menu_base_figure_offset(g, figure);
    uint8_t was = character_records_record_byte(g, character, base);
    if (was < MOST_OF_A_BASE)
        set_character_records_record_byte(g, character, base, (uint8_t)(was + 1));
    TIMED_AS(g, 0xC28820, (void)menu_load_member(g, member));
}

/* An item used by chance draws its next use's outcome as one is used. */
enum { CHANCE_ITEM = 0xC9 };

/* For the original's code and shadow mode: the item and how many the
 * inventory's take is handed in the menus' cells. */
static void leave_one_to_take(GameState *g, uint8_t item)
{
    set_menu_item_operand_quantity(g, 1);
    set_menu_item_operand(g, item);
}

// $C2:B31C
void menu_items_use_one(GameState *g)
{
    uint8_t item = inventory_items(g, menu_item_picked(g));
    ORIGINAL_EFFECT(leave_one_to_take(g, item));
    if (item == CHANCE_ITEM)
        menu_items_draw_a_chance(g);
    TIMED_AS(g, 0xC287D5, (void)menu_take_items(g, item, 1));
    menu_items_redraw_the_cursors_row(g);
    if (inventory_counts(g, menu_item_picked(g)) == 0)
        set_menu_use_effect(g, 0);
}

uint8_t menu_items_picked_item(const GameState *g) { return inventory_items(g, menu_item_picked(g)); }

/* After the members' rows are drawn again, their sprites one block a frame. */
void menu_items_after_one_used(GameState *g) { set_menu_record_block_limit(g, BLOCKS_WHILE_LOADING); }

/* The member window closed: the cursor back on the list's row, its line
 * effect off, the scroll bar shown again. Then (once the records are reset
 * and the second cursor's task stopped) its layer cleared and queued. */
enum { MEMBER_WINDOW_LAYER_AT = 0x5080, MEMBER_WINDOW_LAYER_BYTES = 0x400 };

void menu_items_close_member_window(GameState *g)
{
    set_menu_cursor_index(g, menu_item_list_row(g));
    set_menu_line_effects(g, (uint8_t)(menu_line_effects(g) & ~MEMBER_LINE_EFFECT));
    show_the_scroll_bar(g);
}

void menu_items_clear_member_window(GameState *g)
{
    wram_fill8(g, 0x7E0000u | MEMBER_WINDOW_LAYER_AT, MEMBER_WINDOW_LAYER_BYTES, 0);
    ORIGINAL_EFFECT(charge_block_move(g, MEMBER_WINDOW_LAYER_BYTES));
    menu_queue_fixed_upload(g, MENU_UPLOAD_BACK_LAYER);
}

/* ---- What the steps can run on (the guards') --------------------------------------------------- */

/* The list's places in the party's items: its first no further than its
 * last ten, the cursor on one of its rows; every place an item of the
 * game's or none, as the menus' opening leaves the party's items. */
enum { LAST_TOP = MENU_ITEMS - MENU_ITEMS_ROWS };

static bool items_are_the_games(const GameState *g)
{
    for (unsigned place = 0; place < INVENTORY_ITEMS_COUNT; place++)
        if (inventory_items(g, place) >= MENU_ITEMS)
            return false;
    return true;
}

bool menu_items_rows_can_be_drawn(const GameState *g) { return menu_item_list_top(g) <= LAST_TOP && items_are_the_games(g); }

/* The panel: room for its three uploads, its rows and its windows. */
bool menu_items_panel_can_show(const GameState *g)
{
    return menu_upload_queue_takes(g, 3) && menu_items_rows_can_be_drawn(g) && menu_window_list_drawable(g, MENU_WINDOWS_ITEMS_PANEL);
}

bool menu_items_list_is_pointable(const GameState *g)
{
    return menu_items_rows_can_be_drawn(g) && menu_cursor_index(g) < MENU_ITEMS_ROWS && menu_item_list_row(g) < MENU_ITEMS_ROWS;
}

/* The opening: room for its upload and its colours, its windows, the list's
 * row one of the cursor's places. */
bool menu_items_can_open(const GameState *g)
{
    return menu_upload_queue_takes(g, 1) && menu_palette_changes_have_room(g)
           && menu_window_list_drawable(g, MENU_WINDOWS_ITEMS_SCREEN) && menu_item_list_row(g) < MENU_CURSOR_PLACES_COUNT;
}

bool menu_items_can_point_along_the_list(const GameState *g)
{
    return menu_upload_queue_takes(g, 2) && menu_items_list_is_pointable(g);
}

bool menu_items_can_sort(const GameState *g) { return menu_upload_queue_takes(g, 1) && menu_items_rows_can_be_drawn(g); }

/* ---- The steps ----------------------------------------------------------------------------------- */

/* The description of an item (menu/description.h); item 0, an empty
 * place's, has a blank one. */
enum { BLANK_DESCRIPTION = 0x00 };
void menu_items_describe(GameState *g, uint8_t item) { TIMED_AS(g, 0xC2F2F3, menu_describe_item(g, item)); }

// $C2:AD2E
void menu_items_describe_the_cursors_item(GameState *g) { menu_items_describe(g, menu_items_note_the_cursors_item(g)); }

// $C2:AA40, $C2:ADD0
/* A tick, on to the list, and the screen set up: the party's items set
 * aside first, the list's frame, the scroll bar's task and the sprites'
 * colours between its parts. */
bool menu_open_items_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    if (!n->tick(ctx, ITEMS_AT_THE_OPENING))
        return false;
    menu_items_open(g);
    n->note(ctx, ITEMS_NOTE_OPENED, 0);
    TIMED_AS(g, 0xC290FC, menu_set_items_aside(g));
    menu_items_lay_out(g, pf);
    n->note(ctx, ITEMS_NOTE_LIST_FRAMED, 0);
    menu_items_show_bar_at_the_list(g);
    TIMED_AS(g, 0xC28B93, menu_start_the_scroll_bar(g));
    menu_items_put_the_cursor_back(g);
    TIMED_AS(g, 0xC2F5A7, menu_load_character_colours(g));
    menu_items_show_screen(g);
    n->note(ctx, ITEMS_NOTE_SET_UP, 0);
    return true;
}

// $C2:AA4A
/* A command chosen: a tick, then the list or the sort; going back, the main
 * menu; the cursor moved, a tick. */
bool menu_choose_item_command_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    (void)pf;
    n->read_cursor(ctx);
    ItemsCommandChoice c = menu_items_choose_command(g);
    n->note(ctx, ITEMS_NOTE_BUTTONS_TESTED, 0);
    switch (c) {
    case COMMANDS_LEFT: return n->back_to_the_main_menu(ctx);
    case COMMAND_POINTED_AT:
        n->note(ctx, ITEMS_NOTE_CURSOR_COMPARED, 0);
        return menu_cursor_frame_start(g) == menu_cursor_index(g) || n->tick(ctx, ITEMS_AT_A_COMMAND_POINTED);
    case COMMAND_LIST_CHOSEN:
    case COMMAND_SORT_CHOSEN:
    case COMMAND_NOTHING_CHOSEN: break;
    }
    if (!n->tick(ctx, ITEMS_AT_A_COMMAND_CHOSEN))
        return false;
    menu_items_take_command(g, c);
    return true;
}

// $C2:AC59
/* The item described, the cursor's row kept, a tick where the cursor moved,
 * and the list queued. */
bool menu_point_at_listed_item_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    (void)pf;
    if (!n->describe(ctx))
        return false;
    bool moved = menu_items_keep_the_cursors_row(g);
    n->note(ctx, ITEMS_NOTE_PLACE_COMPARED, 0);
    if (moved && !n->tick(ctx, ITEMS_AT_A_LISTED_ITEM_POINTED))
        return false;
    menu_items_queue_rows(g);
    n->note(ctx, ITEMS_NOTE_ROWS_QUEUED, 0);
    return true;
}

// $C2:AC3A
/* The cursor moved along the list: a page turned and drawn again; or kept
 * within the rows, the list slid a row where the push brings one. */
bool menu_point_along_the_item_list_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    n->note(ctx, ITEMS_NOTE_POINTING, 0);
    uint8_t top = menu_item_list_top(g), to = top;
    TIMED_AS(g, 0xC293BF, to = menu_turn_page(g, top));
    n->note(ctx, ITEMS_NOTE_PAGING_ANSWERED, to != top);
    if (to != top) {
        menu_items_turn_page(g, to);
        n->note(ctx, ITEMS_NOTE_ROWS_REDRAWN, 0);
        bool moved = menu_items_keep_the_cursors_row(g);
        n->note(ctx, ITEMS_NOTE_PLACE_COMPARED, 0);
        if (moved && !n->tick(ctx, ITEMS_AT_A_LISTED_ITEM_POINTED))
            return false;
        menu_items_queue_rows(g);
        n->note(ctx, ITEMS_NOTE_ROWS_QUEUED, 0);
        return true;
    }
    TIMED_AS(g, 0xC2EA53, menu_keep_the_cursor_on_the_list(g));
    n->note(ctx, ITEMS_NOTE_CURSOR_KEPT, 0);
    if (menu_items_slide_brings_a_row(g))
        return n->slide_the_list(ctx);
    n->note(ctx, ITEMS_NOTE_ENDS_COMPARED, 0);
    return menu_point_at_listed_item_with(g, pf, n, ctx);
}

// $C2:AA86
/* An item picked up: a tick, and the second cursor's task. Back: a tick,
 * the commands, and the description cleared. */
bool menu_choose_on_item_list_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    menu_items_put_nothing_in_hand(g);
    n->read_cursor(ctx);
    ItemsListChoice c = menu_items_choose_on_the_list(g);
    n->note(ctx, ITEMS_NOTE_BUTTONS_TESTED, 0);
    switch (c) {
    case LIST_ITEM_PICKED_UP:
        if (!n->tick(ctx, ITEMS_AT_AN_ITEM_PICKED_UP))
            return false;
        menu_items_pick_up(g);
        n->note(ctx, ITEMS_NOTE_PICKED_UP, 0);
        return true;
    case LIST_LEFT:
        if (!n->tick(ctx, ITEMS_AT_THE_LIST_LEFT))
            return false;
        menu_items_back_to_the_commands(g);
        menu_items_describe(g, BLANK_DESCRIPTION);
        return true;
    case LIST_POINTED_AT: break;
    }
    return menu_point_along_the_item_list_with(g, pf, n, ctx);
}

/* The tasks the move stops: the second cursor's (the fourth's bit). */
enum { HELD_CURSOR_TASK = 0x40 };

// $C2:AAC8
/* With an item picked up: put down elsewhere (a tick, the two swapped) or
 * back, a frame, the list drawn again; put down on itself, its use. Then the
 * second cursor's task stopped. Otherwise the cursor moves along the list. */
bool menu_move_picked_item_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    n->read_cursor(ctx);
    ItemsMoveChoice c = menu_items_choose_where(g);
    n->note(ctx, ITEMS_NOTE_BUTTONS_TESTED, 0);
    switch (c) {
    case MOVE_POINTED_AT: return menu_point_along_the_item_list_with(g, pf, n, ctx);
    case MOVE_PUT_DOWN:
        if (!n->tick(ctx, ITEMS_AT_THE_PICKED_ITEM_PUT))
            return false;
        menu_items_swap_with_the_picked(g);
        /* fall through */
    case MOVE_LEFT:
        if (!n->wait(ctx, ITEMS_AT_THE_LIST_PUT_BACK))
            return false;
        menu_items_back_to_the_list(g);
        n->note(ctx, ITEMS_NOTE_ROWS_QUEUED, 0);
        break;
    case MOVE_ON_ITSELF:
        if (!n->tick(ctx, ITEMS_AT_THE_PICKED_ITEM_PUT))
            return false;
        switch (menu_items_use_of_the_picked(g)) {
        case USING_FROM_THE_MENU:
            n->note(ctx, ITEMS_NOTE_EFFECT_TESTED, 0);
            if (!n->run(ctx, ITEMS_USES_THE_ITEM))
                return false;
            menu_items_back_to_the_list_unchanged(g);
            break;
        case USING_SHOWS_USERS:
            menu_items_start_showing_users(g);
            TIMED_AS(g, 0xC2AE91, menu_items_show_users(g, pf));
            break;
        case USING_DOES_NOTHING: menu_items_back_to_the_list_unchanged(g); break;
        }
        break;
    }
    TIMED_AS(g, 0xC28255, menu_stop_tasks(g, HELD_CURSOR_TASK));
    return true;
}

/* ---- An item used from the menu ------------------------------------------------------------------- */

// $C2:B0AB
/* The item's use looked up, and used its way: a tonic's healing, a healing
 * by chance, the party's rest, a tab's figure; or nothing. */
bool menu_use_item_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    (void)pf;
    ItemUseKind kind = menu_items_look_up_use(g, menu_item_operand(g));
    n->note(ctx, ITEMS_NOTE_USE_FOUND, 0);
    switch (kind) {
    case USE_RESTORES: return n->run(ctx, ITEMS_USES_A_RESTORING_ITEM);
    case USE_RESTORES_BY_CHANCE: return n->run(ctx, ITEMS_USES_AN_ITEM_BY_CHANCE);
    case USE_RESTS_THE_PARTY: return n->run(ctx, ITEMS_RESTS_THE_PARTY);
    case USE_RAISES_A_FIGURE: return n->run(ctx, ITEMS_USES_A_TAB);
    case USE_DOES_NOTHING: break;
    }
    return true;
}

/* The member window closed with a tick: the records reset, the second
 * cursor's task stopped, the window's layer cleared and queued. */
static bool leave_the_member_window_with(GameState *g, const ItemsSteps *n, void *ctx)
{
    if (!n->tick(ctx, ITEMS_AT_THE_MEMBER_WINDOW_LEFT))
        return false;
    menu_items_close_member_window(g);
    TIMED_AS(g, 0xC2F566, menu_reset_character_records(g));
    TIMED_AS(g, 0xC28255, menu_stop_tasks(g, HELD_CURSOR_TASK));
    n->note(ctx, ITEMS_NOTE_HELD_CURSOR_STOPPED, 0);
    menu_items_clear_member_window(g);
    return true;
}

/* The member window an item is used from, the same for a tonic and a tab:
 * opened, then a frame at a time the cursor among the members (a tonic's
 * pointing is the techs screen's healing's, a tab's looks at the member's
 * figure), and choosing uses the item where it helps and buzzes where it
 * doesn't, until the player goes back. */
static bool choose_members_to_use_on_with(GameState *g, ItemUseKind kind, const ItemsSteps *n, void *ctx)
{
    bool tab = kind == USE_RAISES_A_FIGURE;
    ItemsPlace members = tab ? ITEMS_AT_THE_TAB_S_MEMBERS : ITEMS_AT_THE_TONIC_S_MEMBERS;
    menu_items_start_member_choice(g);
    n->note(ctx, ITEMS_NOTE_MEMBER_CHOICE_STARTED, (uint8_t)kind);
    if (!n->run(ctx, tab ? ITEMS_OPENS_THE_MEMBER_WINDOW_FOR_A_TAB : ITEMS_OPENS_THE_MEMBER_WINDOW_FOR_A_TONIC))
        return false;
    if (!tab && menu_items_restores_every_member(g))
        TIMED_AS(g, 0xC28AD5, menu_put_cursors_on_every_member(g));
    for (;;) {
        if (!n->wait(ctx, members))
            return false;
        n->read_cursor(ctx);
        if (!n->run(ctx, tab ? ITEMS_POINTS_AT_A_TAB_MEMBER : ITEMS_POINTS_AT_A_MEMBER_TO_RESTORE))
            return false;
        ItemsMemberChoice c = menu_items_choose_member(g);
        n->note(ctx, ITEMS_NOTE_BUTTONS_TESTED, 0);
        if (c == MEMBERS_LEFT)
            return leave_the_member_window_with(g, n, ctx);
        if (c == MEMBER_POINTED_AT_TO_USE_ON)
            continue;
        n->note(ctx, ITEMS_NOTE_HELP_LOADED, 0);
        if (!menu_items_use_helps(g)) {
            if (!n->refuse(ctx, members))
                return false;
            continue;
        }
        bool used = tab ? n->run(ctx, ITEMS_RAISES_THE_FIGURE) && n->run(ctx, ITEMS_USES_ONE_MORE_TAB)
                        : n->run(ctx, ITEMS_HEALS_AND_SHOWS) && n->run(ctx, ITEMS_USES_ONE_MORE_TONIC);
        if (!used)
            return false;
    }
}

// $C2:B17F
/* A tonic: its strength, then the members. */
bool menu_use_restoring_item_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    (void)pf;
    menu_items_set_restore_strength(g);
    return choose_members_to_use_on_with(g, USE_RESTORES, n, ctx);
}

// $C2:B107
/* An item by chance: its outcome drawn, then as a tonic. */
bool menu_use_item_by_chance_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    menu_items_draw_a_chance(g);
    return menu_use_restoring_item_with(g, pf, n, ctx);
}

// $C2:B1F8
/* The party's rest, then the members. */
bool menu_rest_the_party_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    (void)pf;
    menu_items_set_party_rest(g);
    return choose_members_to_use_on_with(g, USE_RESTS_THE_PARTY, n, ctx);
}

// $C2:B200
/* A tab: its figure, then the members it raises it for. */
bool menu_use_tab_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    (void)pf;
    n->registers_at(ctx, ITEMS_AT_THE_TAB_S_FIGURE);
    menu_items_set_figure_raised(g, menu_item_operand(g));
    n->registers_at(ctx, ITEMS_AT_THE_TAB_S_MEMBERS);
    return choose_members_to_use_on_with(g, USE_RAISES_A_FIGURE, n, ctx);
}

// $C2:AF42
/* The member window opened over the list: the window, the records reset,
 * its lines; a frame; the members drawn and their records started; a frame;
 * their sprites shown. */
bool menu_open_member_window_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    menu_items_open_member_window(g, pf);
    TIMED_AS(g, 0xC2F566, menu_reset_character_records(g));
    menu_items_lay_out_member_window_lines(g);
    if (!n->wait(ctx, ITEMS_AT_THE_MEMBER_WINDOW_OPENING))
        return false;
    menu_items_draw_members(g, pf);
    menu_items_hide_member_sprites(g);
    TIMED_AS(g, 0xC2F643, menu_start_the_records(g));
    if (!n->wait(ctx, ITEMS_AT_THE_MEMBER_WINDOW_DRAWN))
        return false;
    menu_items_show_member_sprites(g);
    return true;
}

// $C2:B275
/* A tab's member pointed at, a tick where it is another, and looked at. */
bool menu_look_at_tab_member_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    (void)pf;
    uint8_t before = menu_use_member(g);
    bool another = menu_items_point_at_a_member(g);
    n->note(ctx, ITEMS_NOTE_MEMBER_COMPARED, before);
    if (another && !n->tick(ctx, ITEMS_AT_A_TAB_MEMBER_POINTED))
        return false;
    menu_items_look_at_the_member(g);
    n->note(ctx, ITEMS_NOTE_MEMBER_LOOKED_AT, 0);
    return true;
}

// $C2:B24A
/* The cursor taken round the members, and the member looked at. */
bool menu_point_at_tab_member_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    (void)pf;
    menu_items_take_the_cursor_round(g);
    if (!n->run(ctx, ITEMS_LOOKS_AT_THE_TAB_MEMBER))
        return false;
    menu_items_after_looking(g);
    n->note(ctx, ITEMS_NOTE_BLOCKS_SET, 0);
    return true;
}

// $C2:B2A7
/* A tab used: its sparkle and amount shown, the figure raised, and the
 * member's figures worked out again. */
bool menu_raise_figure_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    (void)pf;
    menu_items_show_the_raise(g);
    if (!n->run(ctx, ITEMS_SHOWS_THE_RAISE_S_AMOUNTS))
        return false;
    n->registers_at(ctx, ITEMS_AT_THE_FIGURE_RAISED);
    menu_items_raise_the_figure(g);
    TIMED_AS(g, 0xC29137, menu_note_member_figures(g));
    n->note(ctx, ITEMS_NOTE_FIGURES_NOTED, 0);
    return true;
}

// $C2:B31C
void menu_items_use_one_more(GameState *g, Platform *pf)
{
    menu_items_use_one(g);
    menu_items_describe(g, menu_items_picked_item(g));
    menu_items_draw_members(g, pf);
    menu_items_after_one_used(g);
}

// $C2:AC1D
/* Who can equip the item shown until a button: then a tick, and back. */
bool menu_close_item_users_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    (void)pf;
    n->read_cursor_held(ctx);
    if (!menu_items_users_closed(g))
        return true;
    n->note(ctx, ITEMS_NOTE_USERS_BUTTONS_LOADED, 0);
    if (!n->tick(ctx, ITEMS_AT_THE_USERS_CLOSED))
        return false;
    menu_items_close_users(g);
    return true;
}

// $C2:AB4A
/* The sort, a frame, and the list drawn again. */
bool menu_sort_items_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx)
{
    (void)pf;
    TIMED_AS(g, 0xC2AB62, menu_items_sort(g));
    if (!n->wait(ctx, ITEMS_AT_THE_SORT))
        return false;
    menu_items_after_sorting(g);
    return true;
}

/* ---- The steps as C ------------------------------------------------------------------------------- */

typedef struct {
    GameState *game;
    Platform *pf;
} ItemsAsC;

static void read_cursor(void *ctx) { menu_read_the_cursor_for_a_step(((ItemsAsC *)ctx)->game); }
static void read_cursor_held(void *ctx) { menu_hold_and_read_the_cursor(((ItemsAsC *)ctx)->game); }

static bool tick(void *ctx, ItemsPlace at)
{
    (void)at;
    ItemsAsC *c = ctx;
    menu_tick(c->game, c->pf);
    return true;
}

static bool wait(void *ctx, ItemsPlace at)
{
    (void)at;
    ItemsAsC *c = ctx;
    menu_task_waits(c->game, 1);
    return true;
}

/* The item on the cursor described, where its entry's guard would take the
 * state: the cursor kept on the list before it can stand where the guard
 * wouldn't. */
static bool describe(void *ctx)
{
    ItemsAsC *c = ctx;
    if (!menu_items_can_point_along_the_list(c->game)) {
        program_needs_the_machine(c->game, 0xC2AD2E, "an items list the port doesn't take");
        return false;
    }
    TIMED_AS(c->game, 0xC2AD2E, menu_items_describe_the_cursors_item(c->game));
    return true;
}

static bool slide_the_list(void *ctx)
{
    ItemsAsC *c = ctx;
    program_needs_the_machine(c->game, 0xC2AC3A, "the items list slid a row");
    return false;
}

static bool back_to_the_main_menu(void *ctx)
{
    ItemsAsC *c = ctx;
    return menu_back_to_the_main_menu(c->game, c->pf);
}

static void note(void *ctx, ItemsNote note, uint8_t value) { (void)ctx, (void)note, (void)value; }

static bool refuse(void *ctx, ItemsPlace at)
{
    (void)at;
    ItemsAsC *c = ctx;
    menu_buzz(c->game, c->pf);
    return true;
}

static void registers_at(void *ctx, ItemsPlace at) { (void)ctx, (void)at; }

static const ItemsSteps as_c;

static bool not_taken(ItemsAsC *c, uint32_t routine)
{
    program_needs_the_machine(c->game, routine, "an item's use the port doesn't take");
    return false;
}

/* A routine of the use as C where its entry's guard would take the state,
 * timed as its own. */
static bool sequence(ItemsAsC *c, uint32_t routine, bool (*takes)(const GameState *), ItemsSequence *run)
{
    if (!takes(c->game))
        return not_taken(c, routine);
    bool done = false;
    TIMED_AS(c->game, routine, done = run(c->game, c->pf, &as_c, c));
    return done;
}

static bool run(void *ctx, ItemsRoutine r)
{
    ItemsAsC *c = ctx;
    GameState *g = c->game;
    bool done = false;
    switch (r) {
    case ITEMS_USES_THE_ITEM: return sequence(c, 0xC2B0AB, menu_items_use_can_run, menu_use_item_with);
    case ITEMS_USES_A_RESTORING_ITEM: return sequence(c, 0xC2B17F, menu_items_use_can_run, menu_use_restoring_item_with);
    case ITEMS_USES_AN_ITEM_BY_CHANCE: return sequence(c, 0xC2B107, menu_items_use_can_run, menu_use_item_by_chance_with);
    case ITEMS_RESTS_THE_PARTY: return sequence(c, 0xC2B1F8, menu_items_use_can_run, menu_rest_the_party_with);
    case ITEMS_USES_A_TAB: return sequence(c, 0xC2B200, menu_items_use_can_run, menu_use_tab_with);
    case ITEMS_OPENS_THE_MEMBER_WINDOW_FOR_A_TONIC:
    case ITEMS_OPENS_THE_MEMBER_WINDOW_FOR_A_TAB:
        return sequence(c, 0xC2AF42, menu_items_member_window_can_run, menu_open_member_window_with);
    case ITEMS_POINTS_AT_A_MEMBER_TO_RESTORE:
        TIMED_AS(g, 0xC2B83E, done = menu_point_at_member_to_restore(g, c->pf));
        return done;
    case ITEMS_POINTS_AT_A_TAB_MEMBER:
        return sequence(c, 0xC2B24A, menu_items_tab_member_can_be_pointed, menu_point_at_tab_member_with);
    case ITEMS_LOOKS_AT_THE_TAB_MEMBER:
        return sequence(c, 0xC2B275, menu_items_tab_member_can_be_pointed, menu_look_at_tab_member_with);
    case ITEMS_HEALS_AND_SHOWS: TIMED_AS(g, 0xC28F55, done = menu_heal_and_show(g, c->pf)); return done;
    case ITEMS_RAISES_THE_FIGURE: return sequence(c, 0xC2B2A7, menu_items_figure_can_be_raised, menu_raise_figure_with);
    case ITEMS_SHOWS_THE_RAISE_S_AMOUNTS: TIMED_AS(g, 0xC28F6C, done = menu_show_amounts(g, c->pf)); return done;
    case ITEMS_USES_ONE_MORE_TONIC:
    case ITEMS_USES_ONE_MORE_TAB:
        if (!menu_items_member_window_can_run(g))
            return not_taken(c, 0xC2B31C);
        TIMED_AS(g, 0xC2B31C, menu_items_use_one_more(g, c->pf));
        return true;
    }
    return false;
}

static const ItemsSteps as_c = {
    read_cursor, read_cursor_held, tick, wait, describe, slide_the_list, back_to_the_main_menu, note, refuse, registers_at,
    run,
};

/* A step as C where its entry's guard would take the state; otherwise the
 * program goes no further there. */
static bool step(GameState *g, Platform *pf, uint32_t routine, bool (*takes)(const GameState *),
                 bool (*run)(GameState *, Platform *, const ItemsSteps *, void *))
{
    if (!takes(g)) {
        program_needs_the_machine(g, routine, "an items screen the port doesn't take");
        return false;
    }
    ItemsAsC c = { g, pf };
    return run(g, pf, &as_c, &c);
}

bool menu_open_items(GameState *g, Platform *pf) { return step(g, pf, 0xC2AA40, menu_items_can_open, menu_open_items_with); }

bool menu_choose_item_command(GameState *g, Platform *pf)
{
    return step(g, pf, 0xC2AA4A, menu_cursor_can_move, menu_choose_item_command_with);
}

static bool list_step_takes(const GameState *g) { return menu_cursor_can_move(g) && menu_items_can_point_along_the_list(g); }

bool menu_choose_on_item_list(GameState *g, Platform *pf)
{
    return step(g, pf, 0xC2AA86, list_step_takes, menu_choose_on_item_list_with);
}

bool menu_move_picked_item(GameState *g, Platform *pf)
{
    return step(g, pf, 0xC2AAC8, menu_items_can_move_the_picked, menu_move_picked_item_with);
}

bool menu_close_item_users(GameState *g, Platform *pf)
{
    return step(g, pf, 0xC2AC1D, menu_cursor_can_move, menu_close_item_users_with);
}

bool menu_sort_items(GameState *g, Platform *pf) { return step(g, pf, 0xC2AB4A, menu_items_can_sort, menu_sort_items_with); }
