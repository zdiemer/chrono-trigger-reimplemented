/* The menus' scrolling lists of items as their rows sit in work RAM
 * (game/menu/lists.h). */
#include "menu/lists.h"

#include "charge.h"
#include "core/input.h"
#include "cpu_io.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* The step's low byte's sign is the way the list slid. */
enum { STEP_UP = 0x80 };

bool menu_list_scrolls(const GameState *g) { return menu_list_scroll_most(g) != 0; }

bool menu_list_slid_up(const GameState *g) { return ((uint8_t)menu_scroll_step(g) & STEP_UP) != 0; }

enum { WORK_RAM = 0x7E0000 };

void menu_move_list_rows(GameState *g, uint16_t rows_at, unsigned shown_bytes, unsigned row_bytes, bool up)
{
    uint32_t first = WORK_RAM | rows_at, past = WORK_RAM | (uint16_t)(rows_at + shown_bytes);
    if (!up) {
        wram_copy(g, first + row_bytes, first, shown_bytes);
        charge_block_move(g, shown_bytes);
        return;
    }
    for (unsigned row = shown_bytes / row_bytes - 1; row-- > 0;)
        wram_copy(g, first + row * row_bytes, first + (row + 1) * row_bytes, row_bytes);
    wram_copy(g, past, first, row_bytes);
    charge_block_move(g, shown_bytes);
}

/* ---- The frame ----------------------------------------------------------------------------------- */

// $C2:EA81
/* A scrolling list laid out: `places` places from the list's first place
 * (menu_list_first_place), `rows` of them in view at once, or all of them
 * where it has fewer. It scrolls by the places it has past its rows, and the
 * paging pages a view's rows at a time as far as that; the cursor's last
 * place is the view's last row; the scroll bar's span starts as the places
 * past the rows, and its thumb moves the bar's `track` over them, a share of
 * it a place. The share is what the CPU's divider makes of it: the track
 * divided by the places past the rows counted in a byte, wrapped round for a
 * list shorter than its rows, and all ones where there are none (a bar no
 * screen draws then). */

void menu_frame_the_list(GameState *g, uint8_t places, uint8_t rows, uint16_t track)
{
    uint8_t divisor = (uint8_t)(places - rows);
    bool fewer = places < rows;
    uint8_t shown = fewer ? places : rows, past = fewer ? 0 : divisor;
    set_menu_paging_rows(g, rows);
    set_menu_list_last_place(g, (uint8_t)((shown ? shown - 1 : 0) + menu_list_first_place(g)));
    set_menu_list_scroll_most(g, past);
    set_menu_scroll_bar_span(g, past);
    set_menu_paging_last_top(g, past);
    set_menu_scroll_bar_step(g, divisor ? (uint16_t)(track / divisor) : 0xFFFF);
}

/* ---- The cursor along a list ------------------------------------------------------------------- */

/* Whether the push took the cursor past the rows of a list that scrolls,
 * which the list's screen then scrolls a row. */
bool menu_list_scrolls_a_row(const GameState *g)
{
    return menu_list_scrolls(g) && menu_pushed(g, MENU_PUSHED_PAST_AN_EDGE);
}

// $C2:EA53
/* The cursor pushed along a list. A list that scrolls leaves the cursor to
 * its screen, which scrolls a row where the push took it past the rows, and
 * while Start is held asks for a fast repeat, so that the list runs on. One
 * that doesn't scroll wraps the cursor round: pushed past its bottom to its
 * first place, past its top to its last; and a cursor past its last place
 * goes to its first. */
void menu_keep_the_cursor_on_the_list(GameState *g)
{
    if (menu_list_scrolls(g)) {
        if (input_held(g) & BUTTON_START)
            menu_ask_for_a_fast_repeat(g);
        return;
    }
    if (menu_pushed(g, MENU_PUSHED_PAST_AN_EDGE))
        set_menu_cursor_index(g, menu_pushed(g, MENU_PUSHED_DOWN) ? menu_list_first_place(g) : menu_list_last_place(g));
    if (menu_list_last_place(g) < menu_cursor_index(g))
        set_menu_cursor_index(g, menu_list_first_place(g));
}

/* ---- The slide ---------------------------------------------------------------------------------- */

/* A row of a list is twelve lines on the screen and two tiles, sixteen
 * lines, in its buffer, so the next row down the buffer shows four lines
 * further down the layer than the one before. */
enum { ROW_LINES = 12, BUFFER_ROW_LINES = 16, NEXT_ROW_SCROLL = BUFFER_ROW_LINES - ROW_LINES };

/* A slide takes four frames, three lines a frame, and keeps the push's
 * repeat from coming before it is over. */
enum { SLIDE_FRAMES = 4, SLIDE_STEP = 3, REPEAT_SOON = 5, REPEAT_AFTER_THE_SLIDE = 4 };

/* A per-line table is bands of three bytes, a band's lines and then its
 * vertical scroll. */
enum { BAND_BYTES = 3 };

/* The band the slide coming in above the list shows it in, before the
 * list's first row. */
enum { ROW_ABOVE = 0, FIRST_ROW = 1 };

static uint8_t first_band_in_view(const GameState *g) { return (uint8_t)(menu_slide_first_band(g) / BAND_BYTES); }
static void set_first_band_in_view(GameState *g, uint8_t band) { set_menu_slide_first_band(g, (uint8_t)(band * BAND_BYTES)); }

static void set_band(GameState *g, unsigned band, uint8_t lines, uint16_t scroll)
{
    set_menu_slide_bands_lines(g, band, lines);
    set_menu_slide_bands_scroll(g, band, scroll);
}

/* The bands staged for the vblank's block move. */
static void stage_band(GameState *g, unsigned band, uint8_t lines, uint16_t scroll)
{
    set_menu_slide_staged_table_byte(g, band * BAND_BYTES, lines);
    set_menu_slide_staged_table_byte(g, band * BAND_BYTES + 1, (uint8_t)scroll);
    set_menu_slide_staged_table_byte(g, band * BAND_BYTES + 2, (uint8_t)(scroll >> 8));
}

/* The list's own table, where the screen keeps it (bank $7E). */
static uint8_t list_table_byte(const GameState *g, unsigned at)
{
    return wram_read8(g, WORK_RAM | (uint16_t)(menu_block_move_dest(g) + at));
}

static uint16_t list_band_scroll(const GameState *g, unsigned band)
{
    return (uint16_t)(list_table_byte(g, band * BAND_BYTES + 1) | list_table_byte(g, band * BAND_BYTES + 2) << 8);
}

static void charge_slide_started(GameState *g, unsigned rows, unsigned table_bytes)
{
    (void)rows;
    charge_block_move(g, 2 * table_bytes);
}

/* The table as it rests is kept, to go back once the slide is over; the
 * staged table takes the bands past the rows from it, a band on (the slide
 * shows a row more than the list), and the slide's bands are the rows with
 * the row coming in above them -- the buffer's last row, which the screen
 * has drawn the new row in -- and below them the buffer's next row after
 * the last in view. The slide's first band in view is the list's first row
 * for a slide down, the row above for a slide up. */
void menu_start_list_slide(GameState *g)
{
    uint8_t rows = menu_slide_rows(g);
    unsigned table_bytes = menu_block_move_size(g) + 1u;
    set_menu_block_move_source(g, (uint16_t)RAM_MENU_SLIDE_STAGED_TABLE);
    for (unsigned band = 0; band < rows; band++)
        set_band(g, FIRST_ROW + band, list_table_byte(g, band * BAND_BYTES), list_band_scroll(g, band));
    for (unsigned at = 0; at < table_bytes; at++)
        set_menu_slide_table_at_rest_byte(g, at, list_table_byte(g, at));
    for (unsigned at = rows * BAND_BYTES; at < table_bytes; at++)
        set_menu_slide_staged_table_byte(g, at, list_table_byte(g, at - BAND_BYTES));
    set_menu_slide_bands_scroll(g, rows, (uint16_t)(menu_slide_bands_scroll(g, rows - 1) + NEXT_ROW_SCROLL));
    set_band(g, ROW_ABOVE, ROW_LINES, (uint16_t)(menu_slide_bands_scroll(g, FIRST_ROW) + rows * BUFFER_ROW_LINES - NEXT_ROW_SCROLL));
    set_menu_slide_bands_lines(g, rows, ROW_LINES);
    bool down = menu_pushed(g, MENU_PUSHED_DOWN);
    set_first_band_in_view(g, down ? FIRST_ROW : ROW_ABOVE);
    set_menu_scroll_step(g, down ? SLIDE_STEP : (uint16_t)-SLIDE_STEP);
    set_menu_scroll_frames(g, SLIDE_FRAMES);
    if (input_repeat_countdown(g) == REPEAT_SOON)
        set_input_repeat_countdown(g, REPEAT_AFTER_THE_SLIDE);
    charge_slide_started(g, rows, table_bytes);
}

/* The rows in view, as far along as the slide has moved: each band's
 * scroll moved that far; the first row in view shows what is left of it
 * (down) or the row coming in above what it has gained (up), and the last
 * row in view the rest of its lines. The vblank copies them in. */
void menu_slide_list_a_frame(GameState *g)
{
    uint8_t rows = menu_slide_rows(g), first = first_band_in_view(g);
    uint16_t moved = menu_scroll_moved(g);
    uint8_t first_lines = (int16_t)moved < 0 ? (uint8_t)-moved : (uint8_t)(menu_slide_bands_lines(g, first) - moved);
    for (unsigned band = 0; band < rows; band++)
        stage_band(g, band, band == 0 ? first_lines : menu_slide_bands_lines(g, first + band),
                   (uint16_t)(menu_slide_bands_scroll(g, first + band) + moved));
    uint8_t last = (uint8_t)(first + rows - 1);
    set_menu_slide_staged_table_byte(g, (rows - 1u) * BAND_BYTES, (uint8_t)(menu_slide_bands_lines(g, last) - first_lines));
    menu_ask_for_the_block_move(g);
}

/* The table as it rested goes back in. */
void menu_put_the_list_bands_back(GameState *g)
{
    set_menu_block_move_source(g, (uint16_t)RAM_MENU_SLIDE_TABLE_AT_REST);
    menu_ask_for_the_block_move(g);
}

/* ---- What the slide takes ------------------------------------------------------------------------ */

// $C2:EB1F

/* The screens keep their lists' tables among the menus' per-line tables,
 * below the slide's own, and slide up to twelve rows, a table of no more
 * than the slide's staging holds. */
enum {
    LINE_TABLES = 0x9000,
    MOST_ROWS = MENU_SLIDE_BANDS_COUNT - 1,
    MOST_TABLE_BYTES = MENU_SLIDE_TABLE_AT_REST_BYTES,
};

/* ---- Paging ------------------------------------------------------------------------------------- */

/* The shoulder assignments page a list: the first back a page, the second
 * on a page. */
static const MenuButton PAGE_BACK = MENU_PAGE_ONE_WAY, PAGE_ON = MENU_PAGE_THE_OTHER;

/* Where the frame's input pages the list from `top`: a page past either end
 * goes to the end instead (`to_an_end`). The sums are a byte's, as the
 * original's: a page back from near the start wraps past the last first row,
 * which is how it finds the start. */
static uint8_t paged_to(const GameState *g, uint8_t top, bool *to_an_end)
{
    uint8_t rows = menu_paging_rows(g), last_top = menu_paging_last_top(g), to = top;
    *to_an_end = false;
    if (menu_asked(g, PAGE_BACK)) {
        to = (uint8_t)(top - rows);
        if (to >= last_top) {
            *to_an_end = true;
            to = 0;
        }
    } else if (menu_asked(g, PAGE_ON)) {
        unsigned on = (unsigned)top + rows;
        to = (uint8_t)on;
        if (on > 0xFF || to >= last_top) {
            *to_an_end = true;
            to = last_top;
        }
    }
    return to;
}

/* Paging to an end puts the cursor on the end's place. */
uint8_t menu_turn_page(GameState *g, uint8_t top)
{
    bool to_an_end;
    uint8_t to = paged_to(g, top, &to_an_end);
    if (to_an_end)
        set_menu_cursor_index(g, menu_asked(g, PAGE_BACK) ? menu_list_first_place(g) : menu_list_last_place(g));
    if (to != top) {
        set_menu_scroll_bar_thumb(g, to);
        set_menu_cursor_frame_start(g, MENU_CURSOR_MOVED_ANYWAY);
    }
    return to;
}

bool menu_page_turns(const GameState *g, uint8_t top)
{
    bool to_an_end;
    return paged_to(g, top, &to_an_end) != top;
}

uint8_t menu_page_destination(const GameState *g, uint8_t top)
{
    bool to_an_end;
    return paged_to(g, top, &to_an_end);
}
