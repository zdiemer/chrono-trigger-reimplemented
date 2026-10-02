/* The equipment screen's two windows (game/menu/equipment.h): the slots
 * window, which slides in beside the member list once a member is chosen and
 * shows the member's four slots and what is in them, and the list window,
 * which slides in below it once a slot is chosen and lists what of the
 * party's items fits the slot.
 *
 * Both slide with the menus' per-line effects. Each band of lines of the
 * screen shows the screen's layers from a line of its own, so a window
 * comes in by moving the line its band shows from a few lines a frame,
 * while the bands' heights change; the tables of those lines are built in
 * work RAM, and the vblank's block move copies the band table into the place
 * its channel reads (menu_staged_line_table). The member list's rows move
 * aside the same way as the slots window opens, and back as it closes.
 *
 * The slots window's rows each show the slot's item's name; the slot the
 * cursor is on keeps its colours, the others are dimmed (the slots' marks).
 * The list window's rows show what fits, from the list's first row, its
 * name only; it ends at the first place of the list that holds nothing.
 *
 * The windows' slides wait a frame between their steps, so equipment_
 * bridge.c runs them, a frame at a time, with the frames, the list of what
 * fits (the original's $C2:8953), its frame and scroll bar, and the card's
 * figures, still the original's, called out between the steps here. */
#include "menu/equipment.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "menu/members.h"
#include "menu/menu_data.h"
#include "menu/text.h"
#include "menu/tiles.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "vars.h"
#include "wram.h"

static void queue_upload(GameState *g, uint16_t record)
{
    TIMED_AS(g, 0xC28385, menu_queue_upload(g, menu_upload_record(g, record)));
}

static void draw_list(GameState *g, Platform *pf, uint16_t list)
{
    TIMED_AS(g, 0xC2ED31, menu_draw_window_list(g, pf, list));
}

/* ---- The per-line tables ----------------------------------------------------------------------- */

/* The band table is staged where the vblank's block move takes it from and
 * copied into the place its channel reads, whole or past its first band. */
enum { STAGED_TABLE = 0x969A, LIVE_TABLE = 0x9300, LIVE_TABLE_PAST_A_BAND = 0x9303 };

static void schedule_table_copy(GameState *g, uint16_t from, uint16_t to, uint16_t bytes)
{
    set_menu_block_move_source(g, from);
    set_menu_block_move_dest(g, to);
    set_menu_block_move_size(g, bytes);
}

/* A value of the bands' tables: a line of the layer (nine bits) with the
 * channel's bits over it. */
enum { LINE_BITS = 0x01FF, VALUE_BITS = 0x6000 };

static uint16_t line_value(uint16_t line) { return (uint16_t)((line & LINE_BITS) | VALUE_BITS); }

static void show_windows_from(GameState *g, uint16_t value)
{
    for (unsigned band = 0; band < MENU_WINDOW_LINE_VALUES_COUNT; band++)
        set_menu_window_line_values(g, band, value);
}

static void show_rows_from(GameState *g, uint16_t value)
{
    for (unsigned band = 0; band < MENU_ROW_LINE_VALUES_COUNT; band++)
        set_menu_row_line_values(g, band, value);
}

/* The member rows' table and the words past it, which the list window's
 * slide fills as one. */
static void show_rows_and_past_from(GameState *g, uint16_t value)
{
    show_rows_from(g, value);
    for (unsigned band = 0; band < MENU_ROW_LINE_VALUES_PAST_COUNT; band++)
        set_menu_row_line_values_past(g, band, value);
}

/* The bands' values: the layer's vertical scroll, a line up, 16, 32 or 48
 * lines up. */
enum { ONE_UP = 0x01FF, SIXTEEN_UP = 0x01F0, THIRTY_TWO_UP = 0x01E0, FORTY_EIGHT_UP = 0x01D0 };

typedef struct {
    uint8_t lines;
    uint16_t value;
} Band;

/* A band in the table: a count of lines past 127 is two bands, the lines
 * past 127 and then 127, as the channel's count byte takes no more. */
enum { MOST_LINES = 0x7F, LONG_BAND = 0x80, BAND_BYTES = 3 };

static unsigned put_band(GameState *g, unsigned at, uint8_t lines, uint16_t value)
{
    set_menu_staged_line_table_lines(g, at, lines);
    set_menu_staged_line_table_value(g, at, value);
    return at + 1;
}

static unsigned stage_band(GameState *g, unsigned at, Band b)
{
    if (!(b.lines & LONG_BAND))
        return put_band(g, at, b.lines, b.value);
    at = put_band(g, at, (uint8_t)((b.lines + 1) & MOST_LINES), b.value);
    return put_band(g, at, MOST_LINES, b.value);
}

/* The original writes the table through the work RAM port, which it leaves
 * past the table's end, and keeps each band's value in a scratch cell. */
static void leave_bands_staged(GameState *g, unsigned bands, uint16_t last_value)
{
    hw_port_seek(g, (uint16_t)(STAGED_TABLE + bands * BAND_BYTES + 1), 0x7E);
    set_menu_scratch_band_value(g, last_value);
}

static void stage_bands(GameState *g, const Band *bands, unsigned count)
{
    unsigned at = 0;
    for (unsigned i = 0; i < count; i++)
        at = stage_band(g, at, bands[i]);
    set_menu_staged_line_table_lines(g, at, 0);
    ORIGINAL_EFFECT(leave_bands_staged(g, at, bands[count - 1].value));
}

/* ---- The slots window -------------------------------------------------------------------------- */

/* What the member has on, a row a slot from `at`: the weapon, the helmet,
 * the armour, the accessory, each the item's name. The original lines the
 * four up in a scratch list first. */
enum { WEAPON_AT = 0x29, HELMET_AT = 0x27, ARMOUR_AT = 0x28, ACCESSORY_AT = 0x2A, PAST_THE_ACCESSORY = 0x2B };
enum { SLOTS = 4, NAME_SETTINGS = 0x0B, ROW_BYTES = 0x80 };

static const uint8_t SLOT_RECORD_PLACES[SLOTS] = { WEAPON_AT, HELMET_AT, ARMOUR_AT, ACCESSORY_AT };

static void leave_slots_listed(GameState *g)
{
    for (unsigned slot = 0; slot < SLOTS; slot++)
        set_menu_equipped_list(g, slot, menu_status_record_bytes_byte(g, SLOT_RECORD_PLACES[slot]));
    set_menu_equipped_list(g, SLOTS, menu_status_record_bytes_byte(g, PAST_THE_ACCESSORY));
}

static uint16_t draw_item_name(GameState *g, uint8_t item, uint16_t pen, uint8_t settings)
{
    uint16_t after = pen;
    TIMED_AS(g, 0xC2EF65, after = menu_draw_string(g, MENU_ITEM_TABLES_BANK | menu_item_name_places(g, item), pen, settings));
    return after;
}

// $C2:A6F0
void menu_equip_draw_equipped(GameState *g, uint16_t at)
{
    ORIGINAL_EFFECT(leave_slots_listed(g));
    for (unsigned slot = 0; slot < SLOTS; slot++, at = (uint16_t)(at + ROW_BYTES))
        (void)draw_item_name(g, menu_status_record_bytes_byte(g, SLOT_RECORD_PLACES[slot]), at, NAME_SETTINGS);
}

/* The slot the cursor is on: its label over the window (ten glyphs of the
 * labels' table, a slot each), copied to the other layer's row too. */
enum { SLOT_LABEL_AT = 0x3086, SLOT_LABEL_SETTINGS = 0x0A, LABELS_ROW = 0x3040, LABELS_ROW_COPY = 0x3840 };

// $C2:A38B
void menu_equip_take_slot(GameState *g)
{
    uint8_t slot = menu_cursor_index(g);
    set_menu_equip_slot(g, slot);
    ORIGINAL_EFFECT(set_menu_scratch_slot_label(g, (uint16_t)(slot * 2)));
    set_menu_fill_tile(g, 0);
    set_menu_text_attribute(g, 0);
    TIMED_AS(g, 0xC2EF65, (void)menu_draw_string(g, menu_slot_label(g, slot), SLOT_LABEL_AT, SLOT_LABEL_SETTINGS));
    wram_copy(g, 0x7E0000u | LABELS_ROW, 0x7E0000u | LABELS_ROW_COPY, ROW_BYTES);
    ORIGINAL_EFFECT(charge_block_move(g, ROW_BYTES));
}

/* The slots' marks: each slot's row of eleven tiles, its two tile rows, in
 * the slots' colours, but the slot the cursor is on in the text's. */
enum { MARKS_AT = 0x3062, MARKED_TILES = 0x0B, DIMMED = 0x1C, UNDIMMED = 0x00 };

static void leave_slots_marked(GameState *g)
{
    set_menu_scratch_marked_slot(g, menu_equip_slot(g));
    set_menu_scratch_slots_marked(g, SLOTS);
}

/* The drawing origin goes down the rows with the marking, and is left a
 * row past the last. */
// $C2:A3ED
void menu_equip_mark_slots_at(GameState *g, uint16_t at)
{
    for (unsigned slot = 0; slot < SLOTS; slot++, at = (uint16_t)(at + ROW_BYTES))
        TIMED_AS(g, 0xC2ECAC, menu_colour_row_pair(g, at, MARKED_TILES, slot == menu_equip_slot(g) ? UNDIMMED : DIMMED));
    set_menu_draw_origin(g, at);
    ORIGINAL_EFFECT(leave_slots_marked(g));
}

// $C2:A3E2
void menu_equip_mark_slots(GameState *g) { menu_equip_mark_slots_at(g, MARKS_AT); }

/* The member's slots, once chosen: the slots window over the card, the
 * member loaded and its row drawn at the list's top, the window's layer
 * shown from line $40FF, the band table copied whole, the cursor on the
 * first slot and its label, the window's colours and layers queued. */
enum { SLOTS_LIST = 0xBE6D, MEMBER_ROW_AT = 0x2E00, SLOTS_SHOWN = 0x40FF, WHOLE_TABLE_BYTES = 0x25 };
enum { UPLOAD_CARD = 0xFBE3, UPLOAD_BG2 = 0xFBFF, UPLOAD_ROW_LINES = 0xFC45, SLOTS_LINE_EFFECTS = 0xC5 };

static void hide_the_scroll_bar(GameState *g) { set_menu_scroll_bar_hidden(g, (uint8_t)(menu_scroll_bar_hidden(g) - 1)); }

// $C2:A321
void menu_equip_show_slots_of_member(GameState *g, Platform *pf)
{
    draw_list(g, pf, SLOTS_LIST);
    uint8_t member = menu_member_on_the_cursor(g);
    set_menu_screen_argument(g, member);
    TIMED_AS(g, 0xC28820, (void)menu_load_member(g, member));
    set_menu_draw_origin(g, MEMBER_ROW_AT);
    TIMED_AS(g, 0xC2A216, menu_draw_member_row(g, pf));
    show_windows_from(g, SLOTS_SHOWN);
    schedule_table_copy(g, STAGED_TABLE, LIVE_TABLE, WHOLE_TABLE_BYTES);
    set_menu_cursor_index(g, 0);
    TIMED_AS(g, 0xC2A38B, menu_equip_take_slot(g));
    set_menu_palette_changed(g, (uint8_t)(menu_palette_changed(g) + 1));
    hide_the_scroll_bar(g);
    queue_upload(g, UPLOAD_CARD);
    queue_upload(g, UPLOAD_BG2);
    queue_upload(g, UPLOAD_ROW_LINES);
    set_menu_line_effects(g, SLOTS_LINE_EFFECTS);
}

/* The slots window slides in over 24 frames, its bottom band growing six
 * lines a frame up to 144 and the bands above giving way, the middle first;
 * the member list's rows wait eight frames for each row below the cursor's
 * (so the cursor's row reaches the window's top as it opens) and then move
 * up six lines a frame. The screen's other bands show the layer from a line
 * up. */
enum { SLOTS_SLIDE_LIST = 0xBE26, SLIDE_TABLE_BYTES = 0x13, UPLOAD_BG1_SLID = 0xFBEA, UPLOAD_BG2_SLID = 0xFC06 };
enum { SLOTS_START = 0x606F, TOP_START = 0x90, MIDDLE_START = 0x30, BOTTOM_MOST = 0x90, SLIDE_STEP = 6 };
enum { FRAMES_A_ROW = 8, LAST_ROW = MENU_MEMBER_ROWS, SLIDE_FRAMES = 0x18, ROWS_STEP = 6, SCREEN_LINES = 0xD0 };
enum { SLIDING_LINE_EFFECTS = 0xE5 };

static void leave_slide(GameState *g, const MenuWindowSlide *w)
{
    set_menu_window_slide_top_lines(g, w->top);
    set_menu_window_slide_middle_lines(g, w->middle);
    set_menu_window_slide_bottom_lines(g, w->bottom);
    set_menu_window_frames_slid(g, w->frame);
    set_menu_window_rows_wait(g, w->rows_from);
    set_menu_window_rows_step(g, w->rows_step);
}

static void leave_rows_waited(GameState *g, uint8_t rows_below)
{
    hw_set_multiplicand(g, FRAMES_A_ROW);
    (void)hw_multiply_by(g, rows_below);
}

// $C2:A71E
void menu_equip_start_slots_slide(GameState *g, Platform *pf, MenuWindowSlide *w)
{
    draw_list(g, pf, SLOTS_SLIDE_LIST);
    schedule_table_copy(g, STAGED_TABLE, LIVE_TABLE_PAST_A_BAND, SLIDE_TABLE_BYTES);
    queue_upload(g, UPLOAD_BG1_SLID);
    queue_upload(g, UPLOAD_BG2_SLID);
    show_windows_from(g, SLOTS_START);
    uint8_t rows_below = (uint8_t)(LAST_ROW - menu_member_row(g));
    ORIGINAL_EFFECT(leave_rows_waited(g, rows_below));
    hide_the_scroll_bar(g);
    *w = (MenuWindowSlide){ .top = TOP_START, .middle = MIDDLE_START, .bottom = 0, .frame = 0,
                            .rows_from = (uint8_t)(FRAMES_A_ROW * rows_below), .rows_step = 0 };
    ORIGINAL_EFFECT(leave_slide(g, w));
}

/* A frame's band heights, and the bands from the screen's top: the top, the
 * middle while it lasts, the bottom, and what is left of the screen's 208
 * lines, where any is. */
// $C2:A7B7
static void step_slots_bands(GameState *g, MenuWindowSlide *w)
{
    show_windows_from(g, line_value((uint16_t)(menu_window_line_values(g, 0) + SLIDE_STEP)));
    show_rows_from(g, line_value((uint16_t)(menu_row_line_values(g, 0) + w->rows_step)));
    if (w->bottom < BOTTOM_MOST) {
        w->bottom = (uint8_t)(w->bottom + SLIDE_STEP);
        if (w->middle != 0)
            w->middle = (uint8_t)(w->middle - SLIDE_STEP);
        else
            w->top = (uint8_t)(w->top - SLIDE_STEP);
    } else {
        w->top = (uint8_t)(w->top - SLIDE_STEP);
    }
    Band bands[4];
    unsigned n = 0;
    bands[n++] = (Band){ w->top, THIRTY_TWO_UP };
    if (w->middle != 0)
        bands[n++] = (Band){ w->middle, SIXTEEN_UP };
    bands[n++] = (Band){ w->bottom, FORTY_EIGHT_UP };
    uint8_t rest = (uint8_t)(SCREEN_LINES - (uint8_t)(w->top + w->middle + w->bottom));
    if (!(rest & LONG_BAND))
        bands[n++] = (Band){ rest, SIXTEEN_UP };
    stage_bands(g, bands, n);
}

/* A frame of the slide: the cursor hidden for it, the rows' table queued,
 * the bands stepped. Whether frames are left: the rows start moving once
 * their wait is over. */
bool menu_equip_slide_slots_a_frame(GameState *g, MenuWindowSlide *w)
{
    set_menu_cursor_hidden(g, (uint8_t)(menu_cursor_hidden(g) + 1));
    queue_upload(g, UPLOAD_ROW_LINES);
    step_slots_bands(g, w);
    set_menu_line_effects(g, SLIDING_LINE_EFFECTS);
    w->frame++;
    bool more = true;
    if (w->frame >= w->rows_from) {
        if (w->rows_step == 0)
            w->rows_step = ROWS_STEP;
        more = w->frame < SLIDE_FRAMES;
    }
    ORIGINAL_EFFECT(leave_slide(g, w));
    return more;
}

/* ---- Back to the members ----------------------------------------------------------------------- */

/* The slots window going: after a frame, the member list's window again,
 * its labels' rows cleared on both layers, the rows drawn and queued; after
 * another, the rows' tables cleared and their colours, the band table the
 * ROM's at rest, the windows' layer shown from line $60FF and the rows from
 * where the cursor's row stood, eight frames a row above the window's top.
 * Then the window slides out as it came in, the rows moving back down until
 * the cursor's row's wait is over. */
enum { MEMBERS_LIST = 0xBE5B, LABEL_ROWS_AT = 0x3044, SECOND_LABEL_ROWS_AT = 0x3084, LABEL_ROW_BYTES = 0x16 };
enum { ROW_TABLES_AT = 0x5D40, ROW_TABLES_BYTES = 0x80, MEMBERS_SHOWN = 0x60FF, ROW_LINES = 0x30 };
enum { TOP_BACK = 0x30, MIDDLE_BACK = 0x90, BOTTOM_BACK = 0x10, UPLOAD_LIST_CARD = 0xFBE3 };

/* The band table at rest, from the ROM's. */
static void put_bands_at_rest(GameState *g)
{
    for (unsigned band = 0; band < MENU_SLOTS_BANDS_AT_REST; band++) {
        unsigned at = band * BAND_BYTES;
        (void)put_band(g, band, menu_slots_bands_at_rest_byte(g, at),
                       (uint16_t)(menu_slots_bands_at_rest_byte(g, at + 1) | menu_slots_bands_at_rest_byte(g, at + 2) << 8));
    }
    set_menu_staged_line_table_lines(g, MENU_SLOTS_BANDS_AT_REST, menu_slots_bands_at_rest_byte(g, MENU_SLOTS_BANDS_AT_REST * BAND_BYTES));
    ORIGINAL_EFFECT(charge_block_move(g, MENU_SLOTS_BANDS_AT_REST * BAND_BYTES + 1));
}

/* The original takes the scroll bar's hold and the cursor's hiding down
 * together here, as a word: the hiding borrows where the hold was 0. */
static void hide_the_scroll_bar_and_cursor(GameState *g)
{
    uint16_t both = (uint16_t)((menu_scroll_bar_hidden(g) | menu_cursor_hidden(g) << 8) - 1);
    set_menu_scroll_bar_hidden(g, (uint8_t)both);
    set_menu_cursor_hidden(g, (uint8_t)(both >> 8));
}

// $C2:A85C
void menu_equip_clear_slot_labels(GameState *g, Platform *pf)
{
    draw_list(g, pf, MEMBERS_LIST);
    wram_fill8(g, 0x7E0000u | LABEL_ROWS_AT, LABEL_ROW_BYTES, 0);
    wram_fill8(g, 0x7E0000u | SECOND_LABEL_ROWS_AT, LABEL_ROW_BYTES, 0);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * LABEL_ROW_BYTES));
    TIMED_AS(g, 0xC2A1B2, menu_draw_member_rows(g, pf));
    queue_upload(g, UPLOAD_BG1_SLID);
}

void menu_equip_start_members_slide(GameState *g, MenuWindowSlide *w)
{
    wram_fill8(g, 0x7E0000u | ROW_TABLES_AT, ROW_TABLES_BYTES, 0);
    ORIGINAL_EFFECT(charge_block_move(g, ROW_TABLES_BYTES));
    TIMED_AS(g, 0xC29E76, menu_colour_member_rows(g));
    schedule_table_copy(g, STAGED_TABLE, LIVE_TABLE, SLIDE_TABLE_BYTES);
    put_bands_at_rest(g);
    show_windows_from(g, MEMBERS_SHOWN);
    uint8_t row = menu_member_row(g);
    show_rows_from(g, line_value((uint16_t)(row * ROW_LINES - 1)));
    queue_upload(g, UPLOAD_ROW_LINES);
    queue_upload(g, UPLOAD_LIST_CARD);
    hide_the_scroll_bar_and_cursor(g);
    set_menu_line_effects(g, SLIDING_LINE_EFFECTS);
    *w = (MenuWindowSlide){ .top = TOP_BACK, .middle = MIDDLE_BACK, .bottom = BOTTOM_BACK, .frame = 0, .rows_from = SLIDE_FRAMES, .rows_step = 0 };
    if (row != 0) {
        ORIGINAL_EFFECT(leave_rows_waited(g, row));
        w->rows_step = ROWS_STEP;
        w->rows_from = (uint8_t)(FRAMES_A_ROW * row);
    }
    ORIGINAL_EFFECT(leave_slide(g, w));
}

// $C2:A970
static void step_members_bands(GameState *g, MenuWindowSlide *w)
{
    show_windows_from(g, line_value((uint16_t)(menu_window_line_values(g, 0) - SLIDE_STEP)));
    show_rows_from(g, line_value((uint16_t)(menu_row_line_values(g, 0) - w->rows_step)));
    w->top = (uint8_t)(w->top + SLIDE_STEP);
    if (w->bottom != 0)
        w->bottom = (uint8_t)(w->bottom - SLIDE_STEP);
    else
        w->middle = (uint8_t)(w->middle - SLIDE_STEP);
    enum { EDGE_LINES = 0x10, BELOW_THE_EDGE = 0xC0 };
    const Band bands[] = {
        { EDGE_LINES, ONE_UP },
        { w->top, THIRTY_TWO_UP },
        { (uint8_t)(BELOW_THE_EDGE - w->top), FORTY_EIGHT_UP },
        { EDGE_LINES, SIXTEEN_UP },
    };
    stage_bands(g, bands, 4);
}

bool menu_equip_slide_members_back_a_frame(GameState *g, MenuWindowSlide *w)
{
    set_menu_cursor_hidden(g, (uint8_t)(menu_cursor_hidden(g) + 1));
    queue_upload(g, UPLOAD_ROW_LINES);
    step_members_bands(g, w);
    set_menu_line_effects(g, SLIDING_LINE_EFFECTS);
    w->frame++;
    bool more = true;
    if (w->frame >= w->rows_from) {
        w->rows_step = 0;
        more = w->frame < SLIDE_FRAMES;
    }
    ORIGINAL_EFFECT(leave_slide(g, w));
    return more;
}

/* Back on the members: the list at rest, the cursor on the member's row and
 * the member loaded, what it has on drawn on the card, the member list's
 * window and the rows' table at rest, the rows coloured and the card
 * queued. The scroll bar's (the original's $C2:A170) comes between. */
enum { EQUIPPED_AT = 0x30A2, MEMBER_LIST_WINDOW = 0xBDEF, ROWS_AT_REST = 0x61FF, MEMBERS_TABLE_BYTES = 0x13 };

// $C2:A2CD
void menu_equip_back_on_the_members(GameState *g, Platform *pf)
{
    set_menu_scroll_step(g, (uint16_t)(menu_scroll_step(g) & 0xFF00));
    uint8_t row = menu_member_row(g);
    set_menu_cursor_index(g, row);
    uint8_t member = (uint8_t)(row + menu_member_scroll(g));
    set_menu_screen_argument(g, (uint16_t)((menu_screen_argument(g) & 0xFF00) | member));
    TIMED_AS(g, 0xC28820, (void)menu_load_member(g, member));
    TIMED_AS(g, 0xC2A6F0, menu_equip_draw_equipped(g, EQUIPPED_AT));
    draw_list(g, pf, MEMBER_LIST_WINDOW);
}

void menu_equip_rest_the_member_rows(GameState *g)
{
    schedule_table_copy(g, STAGED_TABLE, LIVE_TABLE, MEMBERS_TABLE_BYTES);
    show_rows_from(g, ROWS_AT_REST);
    TIMED_AS(g, 0xC29E76, menu_colour_member_rows(g));
    queue_upload(g, UPLOAD_CARD);
}

/* ---- The list window --------------------------------------------------------------------------- */

/* A row of the list at `at`, for the item a list's routines name
 * (menu_item_operand): its name past the icon, ten glyphs, in front of the
 * layer; or a row of blanks. */
enum { LIST_ROW_ATTRIBUTE = 0x20, LIST_NAME_SETTINGS = 0x0A, NAME_PAST_THE_ICON = 1 };

// $C2:A59A
void menu_equip_draw_list_row(GameState *g, uint16_t at)
{
    set_menu_fill_tile(g, 0);
    set_menu_text_attribute(g, LIST_ROW_ATTRIBUTE);
    uint8_t item = menu_item_operand(g);
    if (item != 0)
        TIMED_AS(g, 0xC2EF65,
                 (void)menu_draw_string(g, MENU_ITEM_TABLES_BANK | (uint16_t)(menu_item_name_places(g, item) + NAME_PAST_THE_ICON),
                                        at, LIST_NAME_SETTINGS));
    else
        TIMED_AS(g, 0xC2EF65, (void)menu_draw_blanks(g, at, LIST_NAME_SETTINGS));
    set_menu_fill_tile(g, 0);
    set_menu_text_attribute(g, 0);
}

/* The list's eight rows from its first; the list ends at its first place
 * that holds nothing, whose blank row the rest repeat. */
enum { LIST_ROWS = 8, LIST_FIRST_ROW_AT = 0x4FC6 };

// $C2:A569
void menu_equip_draw_list(GameState *g)
{
    uint8_t place = menu_equip_list_top(g);
    uint16_t at = LIST_FIRST_ROW_AT;
    for (unsigned row = 0; row < LIST_ROWS; row++, at = (uint16_t)(at + ROW_BYTES)) {
        uint8_t item = menu_list_items(g, place);
        set_menu_item_operand(g, item);
        if (item != 0)
            place++;
        TIMED_AS(g, 0xC2A59A, menu_equip_draw_list_row(g, at));
    }
    ORIGINAL_EFFECT(set_menu_window_slide_rows_left(g, 0));
}

uint16_t menu_equip_list_end(void) { return (uint16_t)(LIST_FIRST_ROW_AT + LIST_ROWS * ROW_BYTES); }

/* The list of what fits, once the original's $C2:8953 has made it: the
 * scroll bar's track divided among its places past its rows (its frame is
 * the original's $C2:EA81), the list's rows queued, the list at its first
 * row, the slot's place in a character's record, the frame's first place
 * the list's first row, eight rows shown. */
enum { LIST_TRACK = 0x1500, FIRST_LIST_PLACE = 4, UPLOAD_LIST_ROWS = 0xFC1B };

void menu_equip_lay_out_list(GameState *g)
{
    hw_set_dividend(g, LIST_TRACK);
    queue_upload(g, UPLOAD_LIST_ROWS);
    set_menu_equip_list_top(g, 0);
    set_menu_equip_record_place(g, menu_slot_record_place(g, menu_equip_slot(g)));
    set_menu_list_first_place(g, FIRST_LIST_PLACE);
    set_menu_scratch_list_rows(g, LIST_ROWS);
}

/* After the frame: the bar's span in lines (four a row, through the
 * multiplier, which it leaves with the product) and its thumb at the top;
 * the bar's task (the original's $C2:8B93) then draws it, and the list is
 * drawn. */
enum { THUMB_LINES_A_ROW = 4 };

void menu_equip_set_list_bar(GameState *g)
{
    uint8_t span = menu_scroll_bar_span(g);
    ORIGINAL_EFFECT(hw_set_multiplicand(g, span));
    ORIGINAL_EFFECT((void)hw_multiply_by(g, THUMB_LINES_A_ROW));
    set_menu_scroll_bar_span(g, (uint8_t)(span * THUMB_LINES_A_ROW));
    set_menu_scroll_bar_thumb(g, 0);
}

/* The list's scroll bar, at the list window's side or (drawn again for
 * another slot or member) a row lower. */
enum { LIST_BAR_AT = 0x4268, LIST_BAR_AGAIN_AT = 0x6268 };

void menu_equip_place_list_bar(GameState *g, bool again) { set_menu_scroll_bar_place(g, again ? LIST_BAR_AGAIN_AT : LIST_BAR_AT); }

/* The list window slides in over eight frames: its layer and the slots'
 * rows four lines a frame, the band over the list shrinking and the list's
 * band growing four lines a frame up to 32. Out again the same way, the
 * bands back at rest after. */
enum { LIST_SLIDE_LIST = 0xBEA4, LIST_SLIDE_TABLE_BYTES = 0x10, LIST_START = 0x60FF, LIST_SLOTS_START = 0x402F };
enum { LIST_SLIDE_STEP = 4, OVER_THE_LIST = 2, THE_LIST = 3, OVER_THE_LIST_LINES = 0x70, LIST_LINES_MOST = 0x20 };

static void leave_list_slide(GameState *g, const MenuListSlide *w)
{
    set_menu_window_slide_list_line(g, w->list_line);
    set_menu_window_slide_slots_line(g, w->slots_line);
    set_menu_window_slide_step(g, (uint16_t)w->step);
}

// $C2:A4AF
void menu_equip_start_list_slide(GameState *g, Platform *pf, MenuListSlide *w)
{
    draw_list(g, pf, LIST_SLIDE_LIST);
    show_windows_from(g, LIST_START);
    schedule_table_copy(g, STAGED_TABLE, LIVE_TABLE, LIST_SLIDE_TABLE_BYTES);
    queue_upload(g, UPLOAD_BG2);
    queue_upload(g, UPLOAD_BG1_SLID);
    *w = (MenuListSlide){ LIST_START, LIST_SLOTS_START, LIST_SLIDE_STEP };
    ORIGINAL_EFFECT(leave_list_slide(g, w));
    set_menu_staged_line_table_lines(g, OVER_THE_LIST, OVER_THE_LIST_LINES);
    set_menu_staged_line_table_lines(g, THE_LIST, 0);
    set_menu_staged_line_table_value(g, THE_LIST, THIRTY_TWO_UP);
}

/* A frame of the list window's slide: its layer and the slots' rows a step
 * on, the rows' table queued, the bands' heights a step on, the scroll bar
 * held. Whether frames are left. */
enum { LIST_WINDOW_LINE_EFFECTS = 0xE5, BAR_HELD = 0xE5 };

// $C2:A518
static void step_list_slide(GameState *g, MenuListSlide *w)
{
    w->list_line = (uint16_t)(w->list_line + w->step);
    show_windows_from(g, w->list_line);
    w->slots_line = (uint16_t)(w->slots_line + w->step);
    show_rows_and_past_from(g, w->slots_line);
    queue_upload(g, UPLOAD_ROW_LINES);
    set_menu_staged_line_table_lines(g, OVER_THE_LIST, (uint8_t)(menu_staged_line_table_lines(g, OVER_THE_LIST) - w->step));
    set_menu_staged_line_table_lines(g, THE_LIST, (uint8_t)(menu_staged_line_table_lines(g, THE_LIST) + w->step));
    set_menu_line_effects(g, LIST_WINDOW_LINE_EFFECTS);
    set_menu_scroll_bar_hidden(g, BAR_HELD);
    ORIGINAL_EFFECT(leave_list_slide(g, w));
}

bool menu_equip_slide_list_in_a_frame(GameState *g, MenuListSlide *w)
{
    step_list_slide(g, w);
    return menu_staged_line_table_lines(g, THE_LIST) < LIST_LINES_MOST;
}

/* The list window going: its window list, the layers queued, the slide set
 * the other way. The main menu's third task is held a frame longer. */
enum { LIST_CLOSE_LIST = 0xBEBD, LIST_BACK = 0x611F, LIST_SLOTS_BACK = 0x404F, LIST_TASK = 2 };
enum { OVER_THE_LIST_AT_REST = 0x6F, LIST_LINES_AT_REST = 1, LIST_VALUE_AT_REST = 0xD0 };

// $C2:A697
void menu_equip_start_list_closing(GameState *g, Platform *pf, MenuListSlide *w)
{
    draw_list(g, pf, LIST_CLOSE_LIST);
    queue_upload(g, UPLOAD_CARD);
    queue_upload(g, UPLOAD_BG2);
    schedule_table_copy(g, STAGED_TABLE, LIVE_TABLE, LIST_SLIDE_TABLE_BYTES);
    *w = (MenuListSlide){ LIST_BACK, LIST_SLOTS_BACK, -LIST_SLIDE_STEP };
    ORIGINAL_EFFECT(leave_list_slide(g, w));
    set_menu_tasks_countdown(g, LIST_TASK, (uint8_t)(menu_tasks_countdown(g, LIST_TASK) + 1));
    hide_the_scroll_bar(g);
}

bool menu_equip_slide_list_out_a_frame(GameState *g, MenuListSlide *w)
{
    step_list_slide(g, w);
    return menu_staged_line_table_lines(g, THE_LIST) != 0;
}

void menu_equip_rest_the_list_bands(GameState *g)
{
    set_menu_staged_line_table_lines(g, OVER_THE_LIST, OVER_THE_LIST_AT_REST);
    set_menu_staged_line_table_lines(g, THE_LIST, LIST_LINES_AT_REST);
    set_menu_staged_line_table_value(g, THE_LIST, (uint16_t)((menu_staged_line_table_value(g, THE_LIST) & 0xFF00) | LIST_VALUE_AT_REST));
}

/* Once the list window is in: its frame's window list, the second layer
 * queued, the scroll bar beside it. */
enum { LIST_WINDOW = 0xBEEC };

// $C2:A41A
void menu_equip_show_list_window(GameState *g, Platform *pf)
{
    draw_list(g, pf, LIST_WINDOW);
    queue_upload(g, UPLOAD_BG2);
    menu_equip_place_list_bar(g, false);
}

/* With the list drawn: the band table copied whole, the main menu's third
 * task held a frame, the list's line effects, and the cursor on the list's
 * first row. The original describes the item at the list's 37th place
 * (where X happens to stand), which the bridge reads. */
enum { LIST_LINE_EFFECTS = 0xDF, DESCRIBED_PLACE = 0x25 };

void menu_equip_open_the_list(GameState *g)
{
    schedule_table_copy(g, STAGED_TABLE, LIVE_TABLE, WHOLE_TABLE_BYTES);
}

uint8_t menu_equip_item_described_on_opening(const GameState *g) { return menu_list_items(g, DESCRIBED_PLACE); }

void menu_equip_settle_the_list(GameState *g)
{
    set_menu_tasks_countdown(g, LIST_TASK, (uint8_t)(menu_tasks_countdown(g, LIST_TASK) + 1));
    set_menu_line_effects(g, LIST_LINE_EFFECTS);
    set_menu_cursor_index(g, FIRST_LIST_PLACE);
}

/* ---- Back to the slots ------------------------------------------------------------------------- */

/* Back from the list: (the card drawn again and the low-HP marks worked out
 * again, both still the original's) the member's row at the list's top and
 * the slots' marks; then, after a frame and the list window's closing and
 * the list drawn again for the slot, the windows' layer shown from line
 * $40FF and the band table brought back from where the list's slide keeps
 * it. */
enum { SLOTS_TABLE_FROM = 0x9710 };

// $C2:A64D
void menu_equip_redraw_slots(GameState *g, Platform *pf)
{
    set_menu_draw_origin(g, MEMBER_ROW_AT);
    TIMED_AS(g, 0xC2A216, menu_draw_member_row(g, pf));
    TIMED_AS(g, 0xC2A3E2, menu_equip_mark_slots(g));
}

void menu_equip_slots_at_rest(GameState *g)
{
    show_windows_from(g, SLOTS_SHOWN);
    schedule_table_copy(g, SLOTS_TABLE_FROM, LIVE_TABLE, WHOLE_TABLE_BYTES);
    set_menu_line_effects(g, SLOTS_LINE_EFFECTS);
}
