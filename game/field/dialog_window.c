/* Field mode: the dialog window.
 *
 * A script's line of dialog, or the field's own message (a chest's), is shown
 * in a window across the top or the bottom of the screen: a page of up to
 * four lines of text, set a glyph a frame by the menus' typesetter
 * (menu/typeset.h) into a row of tiles in work RAM, which the vblank sends to
 * the row of the window it belongs to. A dialog goes through these steps, a
 * step a frame (DialogState, field/dialog_window.h):
 *
 * - its text is cleared, a row a frame;
 * - the window opens: it is placed at the top or the bottom of the screen --
 *   where the script said, or away from the speaker -- and grows to its full
 *   height a few lines a frame while the typesetter starts on the line;
 * - the text is set, a glyph a frame, until the typesetter stops at the end
 *   of the line, at a break or at a pause;
 * - the player reads it: for a while, or until the confirm button; a
 *   question's choices take the pad meanwhile (a pointer on the line chosen);
 * - then the text goes on (on the next line, or on a page cleared for it), or
 *   the window closes, shrinking a few lines a frame.
 *
 * The window's height is what the field's line effects draw it from
 * (field/line_effects.c), and a question's answer what its script reads
 * (event/dialog.c). */
#include "field/dialog_window.h"
#include "field/display.h"
#include "field/field.h"
#include "field/object.h"

#include "charge.h"
#include "field/scratch.h"
#include "core/input.h"
#include "cpu_io.h"
#include "event/script.h"
#include "field/bridge.h"
#include "field/field_data.h"
#include "menu/typeset.h"
#include "original_effects.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* ---- Starting and stopping ---------------------------------------------------------------- */

static DialogState state(const GameState *g) { return (DialogState)dialog_state(g); }
static void set_state(GameState *g, DialogState s) { set_dialog_state(g, (uint8_t)s); }

void dialog_begin(GameState *g) { set_state(g, DIALOG_CLEARS_THE_TEXT); }
void dialog_end(GameState *g) { set_state(g, DIALOG_CLOSES); }

static void shut(GameState *g, DialogState then)
{
    if (state(g) == DIALOG_IDLE)
        return;
    set_state(g, then);
    set_dialog_window_position(g, WINDOW_NONE);
    set_dialog_window_state(g, 0);
}

void dialog_shut_to_start_over(GameState *g) { shut(g, DIALOG_CLEARS_THE_TEXT); }
void dialog_shut(GameState *g) { shut(g, DIALOG_IDLE); }
void dialog_forget(GameState *g) { set_state(g, DIALOG_IDLE); }

/* ---- The text --------------------------------------------------------------------------------- *
 *
 * The window holds four rows of text, each a line of glyphs two tiles high,
 * whose tiles are at VRAM $5800 on, a row's $200 words after another's. The
 * typesetter sets a row's glyphs into the staging tiles in work RAM
 * (field_window_staging), cleared to the blank tile for each new row, and the
 * vblank sends them to the row being set: every frame while the row fills,
 * once when the text stops. */
enum { TEXT_ROWS = 4, LAST_TEXT_ROW = TEXT_ROWS - 1 };
enum { TEXT_TILES_VRAM = 0x5800, TEXT_ROW_WORDS = 0x0200, TEXT_ROW_BYTES = 0x0400, BLANK_TILE_BYTES = 0x20 };
typedef enum { SEND_NOTHING = 0, SEND_EVERY_FRAME = 1, SEND_ONCE = 0x81 } TextSending;
enum { TAKEN_ONCE = 0x80 };

static uint8_t text_row(const GameState *g) { return dialog_text_row(g); }
static void send_the_text(GameState *g, TextSending how) { set_dialog_text_sending(g, (uint8_t)how); }

// $C0:20B2
/* The staging tiles cleared to the blank tile, for a new row of text. */
static void clear_the_row(GameState *g)
{
    for (unsigned i = 0; i < BLANK_TILE_BYTES; i++)
        set_field_window_staging(g, i, field_blank_entries_byte(g, i));
    for (unsigned at = BLANK_TILE_BYTES; at < TEXT_ROW_BYTES; at += BLANK_TILE_BYTES)
        wram_copy(g, RAM_FIELD_WINDOW_STAGING, RAM_FIELD_WINDOW_STAGING + at, BLANK_TILE_BYTES);
    ORIGINAL_EFFECT(charge_block_move(g, TEXT_ROW_BYTES));
}

/* The next row to set: the row after this one, or the first of a page. */
static void start_a_row(GameState *g, uint8_t row)
{
    set_dialog_text_row(g, row);
    set_dialog_window_object(g, 0);
    clear_the_row(g);
}

/* ---- What the typesetter stops at --------------------------------------------------------------- */

/* The breaks the typesetter stops at, in pairs from MENU_TEXT_NEW_LINE (each
 * pair the margin's and the indented form, which are the typesetter's
 * business): the next line; the next line once the player has read on; the
 * next page; the next page once the player has read on. */
typedef struct {
    bool next_page, after_reading;
} TextBreak;

static bool text_break(MenuTextStop stop, TextBreak *b)
{
    if (stop < MENU_TEXT_NEW_LINE || stop > MENU_TEXT_LAST_BREAK)
        return false;
    unsigned kind = (unsigned)(stop - MENU_TEXT_NEW_LINE) / 2;
    b->next_page = kind >= 2;
    b->after_reading = (kind & 1) != 0;
    return true;
}

static MenuTextStop stopped_at(const GameState *g) { return (MenuTextStop)menu_text_stop(g); }

/* A pause in the text lasts its count of quarter seconds. */
enum { FRAMES_A_PAUSE_UNIT = 15 };

/* For the original's code and shadow mode: the original works a pause's
 * frames out on the multiplier. */
static void leave_the_pause_product(GameState *g, uint8_t units)
{
    hw_set_multiplicand(g, units);
    (void)hw_multiply_by(g, FRAMES_A_PAUSE_UNIT);
}

/* ---- Setting the text --------------------------------------------------------------------------- */

/* The field's own messages' strings (a chest's), where a script's dialog
 * takes its strings from the table the script named. */
enum { FIELD_MESSAGES = 0xDEFF00 };
enum { MENU_TEXT_START = 0xC257DF, MENU_TEXT_GO_ON = 0xC25823 };

static uint32_t dialog_strings(const GameState *g)
{
    return dialog_opened_by_the_field(g) ? FIELD_MESSAGES : dialog_string_table(g);
}

// $C0:20F2
/* The typesetter starts on the dialog's line, into the staging tiles. */
static void start_the_text(GameState *g)
{
    set_menu_text_string(g, dialog_string(g));
    set_menu_text_strings(g, dialog_strings(g));
    set_menu_text_tiles(g, RAM_FIELD_WINDOW_STAGING);
    set_menu_text_layout(g, MENU_TEXT_FOUR_COLOUR);
    TIMED_AS(g, MENU_TEXT_START, menu_text_start(g));
}

/* Whether the text goes on where the typesetter stopped: not at its end, nor
 * at a pause of no time; at a break, on a new row. */
static bool takes_up_the_text(GameState *g)
{
    MenuTextStop stopped = stopped_at(g);
    TextBreak b;
    if (stopped == MENU_TEXT_ENDED || (stopped == MENU_TEXT_WAITS && menu_text_wait(g) == 0))
        return false;
    if (text_break(stopped, &b)) {
        bool next_row = !b.next_page && text_row(g) != LAST_TEXT_ROW;
        start_a_row(g, next_row ? (uint8_t)(text_row(g) + 1) : 0);
    }
    return true;
}

/* What the window does once the typesetter has stopped this frame: read on
 * where the line ended, a page is full or the player is to read before it
 * goes on (the window stays until the confirm button, or for the pause);
 * otherwise set the rest of the row on the next frames. */
static void after_a_glyph(GameState *g)
{
    MenuTextStop stopped = stopped_at(g);
    TextBreak b;
    if (stopped == MENU_TEXT_ENDED) {
        set_field_dialog_hold(g, DIALOG_HOLD_UNTIL_CONFIRMED);
        set_state(g, DIALOG_IS_READ);
        send_the_text(g, SEND_ONCE);
    } else if (text_break(stopped, &b)) {
        if (b.after_reading) {
            set_field_dialog_hold(g, DIALOG_HOLD_UNTIL_CONFIRMED);
            set_state(g, DIALOG_IS_READ);
        }
        if (b.next_page || text_row(g) == LAST_TEXT_ROW) {
            set_state(g, DIALOG_IS_READ);
            send_the_text(g, SEND_ONCE);
        } else {
            send_the_text(g, SEND_EVERY_FRAME);
        }
    } else if (stopped == MENU_TEXT_WAITS) {
        uint8_t units = menu_text_wait(g);
        set_state(g, DIALOG_IS_READ);
        if (units == 0) {
            send_the_text(g, SEND_ONCE);
        } else {
            ORIGINAL_EFFECT(leave_the_pause_product(g, units));
            set_field_dialog_hold(g, (uint16_t)(units * FRAMES_A_PAUSE_UNIT));
            send_the_text(g, SEND_EVERY_FRAME);
        }
    } else {
        send_the_text(g, SEND_EVERY_FRAME);
    }
}

// $C0:2126
/* A glyph of the text set, where it goes on. */
static void set_a_glyph(GameState *g)
{
    if (!takes_up_the_text(g)) {
        send_the_text(g, SEND_NOTHING);
        return;
    }
    enum { GLYPHS_A_FRAME = 1 };
    set_menu_text_budget(g, GLYPHS_A_FRAME);
    TIMED_AS(g, MENU_TEXT_GO_ON, menu_text_go_on(g));
    after_a_glyph(g);
}

/* ---- The window --------------------------------------------------------------------------------- */

/* The window is a band of the screen its height in lines either side of its
 * middle (field/line_effects.c), opening and closing a few lines a frame. */
enum { WINDOW_FULL = 0x28, WINDOW_LINES_A_FRAME = 4 };

/* Where the window opens: where the script said, or else away from the
 * speaker: at the bottom of the screen for a speaker in its upper half. The
 * speaker's line is read as a byte, so one above the screen (a negative
 * line) counts as low on it. */
enum { SCREEN_LOWER_HALF = 0x80 };

static WindowPosition place_for(const GameState *g)
{
    WindowPlace asked = (WindowPlace)dialog_box_position(g);
    if (asked == WINDOW_TOP)
        return WINDOW_AT_TOP;
    if (asked == WINDOW_BOTTOM)
        return WINDOW_AT_BOTTOM;
    FieldObject speaker = field_linked_object(field_link_stored(dialog_speaker(g)));
    uint8_t line = (uint8_t)object_screen_position(g, speaker).y;
    return line >= SCREEN_LOWER_HALF ? WINDOW_AT_TOP : WINDOW_AT_BOTTOM;
}

static void place_the_window(GameState *g)
{
    set_dialog_window_position(g, (uint8_t)place_for(g));
    set_dialog_window_state(g, 0);
    set_dialog_text_row(g, 0);
    ORIGINAL_EFFECT(set_dialog_window_opening_clears(g, 0));
    send_the_text(g, SEND_NOTHING);
    start_the_text(g);
}

/* The window grows; once it is open the text is set. */
static void grow_the_window(GameState *g)
{
    uint8_t height = dialog_window_state(g);
    for (unsigned n = 0; n < WINDOW_LINES_A_FRAME && height != WINDOW_FULL; n++)
        height++;
    set_dialog_window_state(g, height);
    if (height != WINDOW_FULL)
        return;
    set_state(g, DIALOG_SETS_TEXT);
    set_a_glyph(g);
}

static void open_the_window(GameState *g)
{
    if (dialog_window_at(g) == WINDOW_NONE)
        place_the_window(g);
    else
        grow_the_window(g);
}

/* The window shrinks, and once it is shut the dialog is over. */
static void close_the_window(GameState *g)
{
    uint8_t height = dialog_window_state(g);
    for (unsigned n = 0; n < WINDOW_LINES_A_FRAME; n++) {
        if (height == 0) {
            set_state(g, DIALOG_IDLE);
            set_dialog_window_position(g, WINDOW_NONE);
            forget_dialog_opened_by_the_field(g);
            return;
        }
        height--;
        set_dialog_window_state(g, height);
    }
}

/* ---- Reading it --------------------------------------------------------------------------------- */

/* Once read: the text goes on where it stopped -- on the next row, or on a
 * page cleared for it -- or, at its end, the window closes. */
static void read_on(GameState *g)
{
    MenuTextStop stopped = stopped_at(g);
    TextBreak b;
    if (text_break(stopped, &b))
        set_state(g, b.next_page || text_row(g) == LAST_TEXT_ROW ? DIALOG_CLEARS_THE_NEXT_PAGE : DIALOG_SETS_TEXT);
    else if (stopped == MENU_TEXT_WAITS && menu_text_wait(g) != 0)
        set_state(g, DIALOG_SETS_TEXT);
    else
        set_state(g, DIALOG_CLOSES);
}

/* The window stays while the player reads: until the confirm button, or for
 * its frames. */
static void hold(GameState *g)
{
    uint16_t left = field_dialog_hold(g);
    if (left == 0)
        read_on(g);
    else if (left != DIALOG_HOLD_UNTIL_CONFIRMED)
        set_field_dialog_hold(g, (uint16_t)(left - 1));
}

/* A question's choices come up once its text has all been set, the pointer
 * on the first; the answer picked, the text reads on. */
static void being_read(GameState *g)
{
    switch (dialog_choice_active(g)) {
    case DIALOG_NO_CHOICES:
        hold(g);
        break;
    case DIALOG_CHOICES_SHOWING:
        if (stopped_at(g) != MENU_TEXT_ENDED) {
            hold(g);
            break;
        }
        set_dialog_choice_pointer(g, dialog_choice_first(g));
        set_dialog_choice_active(g, DIALOG_CHOOSING);
        break;
    case DIALOG_CHOSEN:
        read_on(g);
        break;
    default:
        break;
    }
}

/* ---- The step ------------------------------------------------------------------------------------ */

/* Clearing the text takes a row a frame. */
static void clear_the_text(GameState *g, DialogState s, DialogState first)
{
    if (s == first) {
        clear_the_row(g);
        set_dialog_text_row(g, 0);
        send_the_text(g, SEND_EVERY_FRAME);
    } else {
        set_dialog_text_row(g, (uint8_t)(text_row(g) + 1));
    }
    set_state(g, (DialogState)(s + 1));
}

enum { CLEARING_ROWS = TEXT_ROWS };

void field_dialog_window_step(GameState *g)
{
    DialogState s = state(g);
    if (s >= DIALOG_CLEARS_THE_TEXT && s < DIALOG_CLEARS_THE_TEXT + CLEARING_ROWS) {
        clear_the_text(g, s, DIALOG_CLEARS_THE_TEXT);
        return;
    }
    if (s >= DIALOG_CLEARS_THE_NEXT_PAGE && s < DIALOG_CLEARS_THE_NEXT_PAGE + CLEARING_ROWS) {
        clear_the_text(g, s, DIALOG_CLEARS_THE_NEXT_PAGE);
        return;
    }
    switch (s) {
    case DIALOG_OPENS_THE_WINDOW:
        open_the_window(g);
        break;
    case DIALOG_SETS_THE_NEXT_PAGE:
        set_state(g, DIALOG_SETS_TEXT);
        /* fall through */
    case DIALOG_SETS_TEXT:
        set_a_glyph(g);
        break;
    case DIALOG_IS_READ:
        being_read(g);
        break;
    case DIALOG_CLOSES:
        close_the_window(g);
        break;
    default:
        break;
    }
}

// $C0:6ECB
/* The vblank's part: the staging tiles sent to the row being set. (A row
 * past 127, which the game never sets, carries the top bit of its doubling
 * into the address, as the original's addition takes the carry in.) */
void field_send_dialog_text(GameState *g, Platform *pf)
{
    uint8_t row = text_row(g);
    uint16_t vram = (uint16_t)(TEXT_TILES_VRAM + row * TEXT_ROW_WORDS + (row >> 7));
    FieldTransfer t = { .vram = vram, .mode = FIELD_TRANSFER_WORDS,
                        .source = RAM_FIELD_WINDOW_STAGING, .size = TEXT_ROW_BYTES };
    TIMED_AS(g, FIELD_VRAM_TRANSFER_ENTRY, field_send_transfer(g, pf, t));
}

/* ---- A question's choices ------------------------------------------------------------------------ */

static uint8_t pointer(const GameState *g) { return dialog_choice_pointer(g); }
static void point_at(GameState *g, uint8_t line) { set_dialog_choice_pointer(g, line); }

/* Down past the last choice comes back to the first, and up past the first
 * to the last. (Up from the window's top line goes to the last too.) */
static void point_down(GameState *g)
{
    uint8_t line = (uint8_t)(pointer(g) + 1);
    point_at(g, line > dialog_choice_last(g) ? dialog_choice_first(g) : line);
}

static void point_up(GameState *g)
{
    uint8_t line = pointer(g);
    point_at(g, line != 0 && (uint8_t)(line - 1) >= dialog_choice_first(g) ? (uint8_t)(line - 1) : dialog_choice_last(g));
}

static bool pointer_on_a_line(uint8_t p) { return !(p & DIALOG_NO_CHOICE_POINTER) && p != DIALOG_CHOICE_TAKEN; }

/* While the player picks, the pad moves the pointer and the confirm button,
 * held, picks the line it points at. Once picked, the line is the answer and
 * the pointer goes; and while a question's text is still showing, or once
 * it is answered, the confirm button pressed lets the window read on. */
void field_take_choice_buttons(GameState *g, uint8_t choosing)
{
    if (choosing == DIALOG_CHOOSING) {
        Buttons pressed = input_mapped(g, ASSIGNED_PRESSED);
        if (pressed & BUTTON_DOWN)
            point_down(g);
        else if (pressed & BUTTON_UP)
            point_up(g);
        else if (input_mapped(g, ASSIGNED_HELD) & ASSIGNED_CONFIRM)
            set_dialog_choice_active(g, DIALOG_CHOSEN);
        return;
    }
    if (choosing != DIALOG_CHOICES_SHOWING && pointer_on_a_line(pointer(g))) {
        set_dialog_choice(g, pointer(g));
        point_at(g, DIALOG_CHOICE_TAKEN);
    }
    if (input_mapped(g, ASSIGNED_PRESSED) & ASSIGNED_CONFIRM)
        set_field_dialog_hold(g, 0);
}

/* ---- The choice pointer ------------------------------------------------------------------------ *
 *
 * The pointer is drawn over the first two glyphs' place of the line it points
 * at, columns 2 and 3 of the window's text area, whose tile map is at VRAM
 * $1C00 (a row of 32 tiles, $40 words a line of text, the line's two tile
 * rows $20 words apart): there the line's own tiles are $2902 + $40 * line
 * above and $10 on below, and the pointer's are $28FC on. The field refreshes
 * them in the time after the picture (field/display.c's line interrupt). */
enum { CHOICE_LINES = 4 };
enum { LINE_TILES_VRAM = 0x1C02, LINE_OWN_TILE = 0x2902, POINTER_TILE = 0x28FC, LINE_WORDS = 0x40, LOWER_ROW = 0x20,
       OWN_LOWER_TILES = 0x10, POINTER_LOWER_TILES = 2 };

typedef struct {
    uint32_t address, first, second;   /* the origin tags of the three writes of a pair of tiles */
} PairSites;

static void write_pair(Platform *pf, uint16_t vram, uint16_t tile, PairSites at)
{
    vid_set_vram_address(pf, PF_AT(at.address), vram);
    vid_write_vram(pf, PF_AT(at.first), tile);
    vid_write_vram(pf, PF_AT(at.second), (uint16_t)(tile + 1));
}

/* A line's two places with its own tiles, or the pointer's. */
static void draw_line_start(Platform *pf, unsigned line, bool pointed_at, const PairSites at[2])
{
    uint16_t vram = (uint16_t)(LINE_TILES_VRAM + LINE_WORDS * line);
    uint16_t own = (uint16_t)(LINE_OWN_TILE + LINE_WORDS * line);
    write_pair(pf, vram, pointed_at ? POINTER_TILE : own, at[0]);
    write_pair(pf, (uint16_t)(vram + LOWER_ROW),
               pointed_at ? (uint16_t)(POINTER_TILE + POINTER_LOWER_TILES) : (uint16_t)(own + OWN_LOWER_TILES), at[1]);
}

/* Each line's own tiles as its own routine writes them ($C0:F159, $F142,
 * $F12B, $F110). */
static const PairSites own_tiles[CHOICE_LINES][2] = {
    { { 0xC0F15C, 0xC0F161, 0xC0F164 }, { 0xC0F169, 0xC0F16E, 0xC0F128 } },
    { { 0xC0F145, 0xC0F14A, 0xC0F14D }, { 0xC0F152, 0xC0F157, 0xC0F128 } },
    { { 0xC0F12E, 0xC0F133, 0xC0F136 }, { 0xC0F13B, 0xC0F140, 0xC0F128 } },
    { { 0xC0F113, 0xC0F118, 0xC0F11B }, { 0xC0F120, 0xC0F125, 0xC0F128 } },
};

/* And the pointer's, as the refresh writes them for the line pointed at. */
static const PairSites pointer_tiles[CHOICE_LINES][2] = {
    { { 0xC0F097, 0xC0F09C, 0xC0F09F }, { 0xC0F0A4, 0xC0F0C9, 0xC0F0CC } },
    { { 0xC0F0FC, 0xC0F101, 0xC0F104 }, { 0xC0F109, 0xC0F0C9, 0xC0F0CC } },
    { { 0xC0F0DC, 0xC0F0E1, 0xC0F0E4 }, { 0xC0F0E9, 0xC0F0C9, 0xC0F0CC } },
    { { 0xC0F0B7, 0xC0F0BC, 0xC0F0BF }, { 0xC0F0C4, 0xC0F0C9, 0xC0F0CC } },
};

bool field_choice_pointer_shown(const GameState *g) { return pointer(g) < DIALOG_NO_CHOICE_POINTER; }

/* The lines' first places refreshed: the pointer on the line it points at,
 * the others' own tiles; with no line pointed at (the choice taken) all
 * their own, and the pointer is gone. The lines written with their own
 * routines are written in the routines' order. */
void field_refresh_choice_pointer(GameState *g, Platform *pf)
{
    vid_start_vram_uploads(pf, PF_AT(0xC0F069), VID_VRAM_WORD_PORT);
    unsigned at = pointer(g);
    for (unsigned line = 0; line < CHOICE_LINES; line++)
        if (line != at)
            draw_line_start(pf, line, false, own_tiles[line]);
    if (at < CHOICE_LINES)
        draw_line_start(pf, at, true, pointer_tiles[at]);
    else
        point_at(g, DIALOG_NO_CHOICE_POINTER);
}

/* ---- The window's looks ---------------------------------------------------------------------- *
 *
 * The window is BG3 over its band of the screen (field/line_effects.c): its
 * frame a tile map at VRAM $1D00, 32 tiles by 10, drawn from the window
 * design the player chose (its tiles and colours), and inside it the text
 * area, a tile map at $1C00 whose eight rows of 32 name the text page's
 * tiles: each line of text two rows, its first sixteen columns' top and
 * bottom halves and then its last sixteen's. Both maps are built in the
 * staging buffer in work RAM and sent from there, one after the other, as a
 * map's screen is set up (field/screen_setup.c) or a frame at a time after a
 * battle (the vblank's screen transition, field/nmi.c). */
enum { FRAME_MAP_VRAM = 0x1D00, FRAME_COLUMNS = 32, FRAME_ROWS = 10, TEXT_AREA_VRAM = 0x1C00, TEXT_AREA_ROWS = 8 };
enum { ENTRY_PRIORITY = 0x2000, SECOND_PATTERN_TILES = 0x0300, TEXT_AREA_LOOK = 0x2900 };

/* The frame's tiles: its corners, its edges (the top's and the bottom's
 * alternating two tiles, the sides' a tile a row), and its interior, a
 * pattern of two tiles by two repeated in blocks that alternate with the
 * same pattern's second tiles. */
enum { FRAME_TOP_LEFT = 0xF0, FRAME_TOP = 0xF1, FRAME_TOP_RIGHT = 0xF3, FRAME_LEFT = 0xF4, FRAME_RIGHT = 0xF5,
       FRAME_BOTTOM_LEFT = 0xF8, FRAME_BOTTOM = 0xF9, FRAME_BOTTOM_RIGHT = 0xFB, FRAME_PATTERN = 0xFC };
enum { FRAME_SECOND_ROW_EDGE = 2 };

static uint16_t frame_entry(unsigned row, unsigned column)
{
    unsigned last_column = FRAME_COLUMNS - 1, last_row = FRAME_ROWS - 1;
    uint8_t tile;
    if (row == 0 || row == last_row) {
        bool top = row == 0;
        tile = column == 0             ? (top ? FRAME_TOP_LEFT : FRAME_BOTTOM_LEFT)
               : column == last_column ? (top ? FRAME_TOP_RIGHT : FRAME_BOTTOM_RIGHT)
                                       : (uint8_t)((top ? FRAME_TOP : FRAME_BOTTOM) + (column - 1) % 2);
        return (uint16_t)(ENTRY_PRIORITY | tile);
    }
    unsigned inner_row = row - 1;
    if (column == 0 || column == last_column) {
        tile = (uint8_t)((column == 0 ? FRAME_LEFT : FRAME_RIGHT) + (inner_row % 2) * FRAME_SECOND_ROW_EDGE);
        return (uint16_t)(ENTRY_PRIORITY | tile);
    }
    unsigned inner_column = column - 1;
    tile = (uint8_t)(FRAME_PATTERN + (inner_row % 2) * 2 + inner_column % 2);
    bool second = ((inner_column / 2) + (inner_row / 2)) % 2 != 0;
    return (uint16_t)(ENTRY_PRIORITY | (second ? SECOND_PATTERN_TILES : 0) | tile);
}

/* The text area's tile for a row and a column: the line's (two rows), its
 * column's half of the line, and the row's half of the glyphs. */
enum { TILES_A_LINE = 0x40, TILES_A_HALF_LINE = 0x20, TILES_A_GLYPH_ROW = 0x10, COLUMNS_A_HALF_LINE = 16 };

static uint16_t text_area_entry(unsigned row, unsigned column)
{
    unsigned tile = (row / 2) * TILES_A_LINE + (column / COLUMNS_A_HALF_LINE) * TILES_A_HALF_LINE +
                    (row % 2) * TILES_A_GLYPH_ROW + column % COLUMNS_A_HALF_LINE;
    return (uint16_t)(TEXT_AREA_LOOK | tile);
}

static void stage_entry(GameState *g, unsigned n, uint16_t entry)
{
    set_field_window_staging(g, 2 * n, (uint8_t)entry);
    set_field_window_staging(g, 2 * n + 1, (uint8_t)(entry >> 8));
}

/* For the original's code and shadow mode: it writes the maps through the
 * work RAM port, whose cursor it leaves past the last entry. */
static void leave_the_port_past(GameState *g, unsigned entries)
{
    enum { WORK_RAM_LOWER_BANK = 0 };
    hw_port_seek(g, (uint16_t)(RAM_ABS(RAM_FIELD_WINDOW_STAGING) + 2 * entries), WORK_RAM_LOWER_BANK);
}

// $C0:29F7
void field_build_the_window_frame(GameState *g)
{
    for (unsigned row = 0; row < FRAME_ROWS; row++)
        for (unsigned column = 0; column < FRAME_COLUMNS; column++)
            stage_entry(g, row * FRAME_COLUMNS + column, frame_entry(row, column));
    ORIGINAL_EFFECT(leave_the_port_past(g, FRAME_ROWS * FRAME_COLUMNS));
}

/* For the original's code and shadow mode: the end of the last run of tiles
 * it wrote, which it keeps in a scratch cell. */
enum { LAST_TEXT_AREA_TILE = 0xFF };

// $C0:2B78
void field_build_the_text_area(GameState *g)
{
    for (unsigned row = 0; row < TEXT_AREA_ROWS; row++)
        for (unsigned column = 0; column < FRAME_COLUMNS; column++)
            stage_entry(g, row * FRAME_COLUMNS + column, text_area_entry(row, column));
    ORIGINAL_EFFECT(leave_the_port_past(g, TEXT_AREA_ROWS * FRAME_COLUMNS));
    ORIGINAL_EFFECT(original_cell8(g, FIELD_SCRATCH_CELLS, LAST_TEXT_AREA_TILE));
}

static FieldTransfer staged_map(uint16_t vram, unsigned rows)
{
    return (FieldTransfer){ .vram = vram, .mode = FIELD_TRANSFER_WORDS, .source = RAM_FIELD_WINDOW_STAGING,
                            .size = (uint16_t)(rows * FRAME_COLUMNS * 2) };
}

// $C0:6EB0
void field_send_the_window_frame(GameState *g, Platform *pf)
{
    TIMED_AS(g, FIELD_VRAM_TRANSFER_ENTRY, field_send_transfer(g, pf, staged_map(FRAME_MAP_VRAM, FRAME_ROWS)));
}

// $C0:6EF1
void field_send_the_text_area(GameState *g, Platform *pf)
{
    TIMED_AS(g, FIELD_VRAM_TRANSFER_ENTRY, field_send_transfer(g, pf, staged_map(TEXT_AREA_VRAM, TEXT_AREA_ROWS)));
}

/* The design the player chose for windows. */
enum { WINDOW_DESIGN_BITS = 0x07 };
static uint8_t window_design(const GameState *g) { return (uint8_t)(game_settings(g, 1) & WINDOW_DESIGN_BITS); }

// $C0:6E5C
/* The design's frame tiles, among BG1's and BG2's tiles from $F0 (which no
 * map uses), and its interior's pattern, tiles $3FC-$3FF. */
enum { DESIGN_FRAME_VRAM = 0x1F00, DESIGN_FRAME_BYTES = 0x200, DESIGN_PATTERN_VRAM = 0x4FC0, DESIGN_PATTERN_BYTES = 0x80 };

void field_send_the_window_design(GameState *g, Platform *pf)
{
    uint32_t graphics = field_window_design_graphics(g, window_design(g));
    FieldTransfer frame = { DESIGN_FRAME_VRAM, FIELD_TRANSFER_WORDS, graphics, DESIGN_FRAME_BYTES };
    FieldTransfer pattern = { DESIGN_PATTERN_VRAM, FIELD_TRANSFER_WORDS, graphics + DESIGN_FRAME_BYTES,
                              DESIGN_PATTERN_BYTES };
    TIMED_AS(g, FIELD_VRAM_TRANSFER_ENTRY, field_send_transfer(g, pf, frame));
    TIMED_AS(g, FIELD_VRAM_TRANSFER_ENTRY, field_send_transfer(g, pf, pattern));
}

// $C0:70E9
/* The design's colours into the first palette's colours 1-15, but for three
 * greys of the window's own (9-11), and the palette as loaded copied whole to
 * the palette on the screen. */
enum { FIRST_WINDOW_COLOUR = 1, WINDOW_GREYS_FROM = 9, WINDOW_GREYS = 3, PALETTE_COLOURS_ALL = 256 };
static const uint16_t window_greys[WINDOW_GREYS] = { 0x0C63, 0x18C6, 0x739C };

void field_colour_the_window(GameState *g)
{
    uint8_t design = window_design(g);
    uint32_t mark = 0;
    ORIGINAL_EFFECT(mark = charge_uncounted_begin(g));
    for (unsigned n = 0; n < WINDOW_DESIGN_COLOURS; n++)
        set_palette_base(g, FIRST_WINDOW_COLOUR + n, field_window_design_colour(g, design, FIRST_WINDOW_COLOUR + n));
    for (unsigned n = 0; n < WINDOW_GREYS; n++)
        set_palette_base(g, WINDOW_GREYS_FROM + n, window_greys[n]);
    for (unsigned c = 0; c < PALETTE_COLOURS_ALL; c++)
        set_palette_working(g, c, palette_base(g, c));
    ORIGINAL_EFFECT(charge_uncounted_end(g, mark));
    ORIGINAL_EFFECT(charge_block_move(g, 2 * WINDOW_DESIGN_COLOURS + 2 * PALETTE_COLOURS_ALL));
    (void)mark;
}

// $C0:6F0C
/* The field's two sprites of its own, in the block of sprite tiles no object
 * is given: each sprite's top tiles and its bottom ones, a row of tiles
 * apart. */
enum { OWN_SPRITE_ROWS = 4, OWN_SPRITE_ROW_BYTES = 0x40 };
static const struct {
    uint16_t vram;
    uint16_t from;
} own_sprite_rows[OWN_SPRITE_ROWS] = { { 0x0000, 0x00 }, { 0x0100, 0x40 }, { 0x0030, 0x80 }, { 0x0130, 0xC0 } };

void field_send_the_fields_own_sprites(GameState *g, Platform *pf)
{
    for (unsigned k = 0; k < OWN_SPRITE_ROWS; k++) {
        FieldTransfer t = { own_sprite_rows[k].vram, FIELD_TRANSFER_WORDS,
                            (uint32_t)FIELD_OWN_SPRITES_TILES + own_sprite_rows[k].from, OWN_SPRITE_ROW_BYTES };
        TIMED_AS(g, FIELD_VRAM_TRANSFER_ENTRY, field_send_transfer(g, pf, t));
    }
}

// $C0:6F5E
/* The choice pointer's tiles, BG3's last four ($28FC-$28FF). */
enum { POINTER_TILES_VRAM = 0x57E0, POINTER_TILES_BYTES = 0x40 };

void field_send_the_choice_pointer(GameState *g, Platform *pf)
{
    FieldTransfer t = { POINTER_TILES_VRAM, FIELD_TRANSFER_WORDS, FIELD_CHOICE_POINTER_TILES, POINTER_TILES_BYTES };
    TIMED_AS(g, FIELD_VRAM_TRANSFER_ENTRY, field_send_transfer(g, pf, t));
}

/* ---- Where it meets the original ------------------------------------------------------------------ */

/* Whether the step sets a glyph this frame, and whether it starts the text:
 * for its guard, which takes only text the typesetter's port can set
 * (menu/typeset.h). */
static bool step_starts_the_text(const GameState *g)
{
    return state(g) == DIALOG_OPENS_THE_WINDOW && dialog_window_at(g) == WINDOW_NONE;
}

static bool step_sets_a_glyph(const GameState *g)
{
    DialogState s = state(g);
    bool sets = s == DIALOG_SETS_TEXT || s == DIALOG_SETS_THE_NEXT_PAGE ||
                (s == DIALOG_OPENS_THE_WINDOW && dialog_window_at(g) != WINDOW_NONE &&
                 dialog_window_state(g) <= WINDOW_FULL && dialog_window_state(g) + WINDOW_LINES_A_FRAME >= WINDOW_FULL);
    MenuTextStop stopped = stopped_at(g);
    return sets && stopped != MENU_TEXT_ENDED && !(stopped == MENU_TEXT_WAITS && menu_text_wait(g) == 0);
}

/* The field's page with the data bank $00; the speaker an object the tables
 * hold, as the window's placing reads it (with the byte after it, as a
 * word); and text the typesetter's port takes. */
bool field_dialog_window_can_step(Snes *s)
{
    if (!field_on_page_low_bank(s) || !m_bank_is_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = dialog_speaker_high(g) == 0 && field_names_object(dialog_speaker(g));
    if (ok && step_starts_the_text(g))
        ok = menu_text_can_be_typed(g, dialog_strings(g), dialog_string(g), RAM_FIELD_WINDOW_STAGING);
    if (ok && step_sets_a_glyph(g))
        ok = menu_text_can_carry_on(g);
    vars_peek(false);
    return ok;
}

/* None of these reads the registers but the choice's state in A; their
 * callers read only the overflow, which they leave as it was. */
void field_dialog_window_step_entry(Snes *s)
{
    field_dialog_window_step(state_of(s));
    m_rtl(s);
}

void field_take_choice_buttons_entry(Snes *s)
{
    field_take_choice_buttons(state_of(s), (uint8_t)s->cpu.a);
    m_rts(s);
}

void field_send_dialog_text_entry(Snes *s)
{
    field_send_dialog_text(state_of(s), m_platform(s));
    m_rts(s);
}

/* The pointer's writers are entered with the direct page on the video
 * registers, which they write through; on any other page they write memory,
 * which stays the original's. */
bool field_choice_lines_dp_is_ppu(Snes *s) { return s->cpu.d == M_DP_PPU; }

// $C0:F159, $C0:F142, $C0:F12B, $C0:F110
void field_draw_choice_line_0(Snes *s) { draw_line_start(m_platform(s), 0, false, own_tiles[0]); m_rts(s); }
void field_draw_choice_line_1(Snes *s) { draw_line_start(m_platform(s), 1, false, own_tiles[1]); m_rts(s); }
void field_draw_choice_line_2(Snes *s) { draw_line_start(m_platform(s), 2, false, own_tiles[2]); m_rts(s); }
void field_draw_choice_line_3(Snes *s) { draw_line_start(m_platform(s), 3, false, own_tiles[3]); m_rts(s); }

void field_refresh_choice_pointer_entry(Snes *s)
{
    field_refresh_choice_pointer(state_of(s), m_platform(s));
    m_rts(s);
}

/* The window's looks, on the field's page with the data bank reaching the
 * registers; nothing they leave in the registers is read but Y, which they
 * leave as it was. */
void field_build_the_window_frame_entry(Snes *s)
{
    field_build_the_window_frame(state_of(s));
    m_rtl(s);
}

void field_build_the_text_area_entry(Snes *s)
{
    field_build_the_text_area(state_of(s));
    m_rtl(s);
}

void field_send_the_window_frame_entry(Snes *s)
{
    field_send_the_window_frame(state_of(s), m_platform(s));
    m_rts(s);
}

void field_send_the_text_area_entry(Snes *s)
{
    field_send_the_text_area(state_of(s), m_platform(s));
    m_rts(s);
}

void field_send_the_window_design_entry(Snes *s)
{
    field_send_the_window_design(state_of(s), m_platform(s));
    m_rts(s);
}

void field_send_the_choice_pointer_entry(Snes *s)
{
    field_send_the_choice_pointer(state_of(s), m_platform(s));
    m_rts(s);
}
