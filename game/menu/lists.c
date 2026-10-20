/* The menus' scrolling lists of items as their rows sit in work RAM
 * (game/menu/lists.h). */
#include "menu/lists.h"

#include "charge.h"
#include "core/input.h"
#include "menu/cursor.h"
#include "menu/bridge.h"
#include "menu/display.h"
#include "original_effects.h"
#include "port/machine.h"
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
        ORIGINAL_EFFECT(charge_block_move(g, shown_bytes));
        return;
    }
    for (unsigned row = shown_bytes / row_bytes - 1; row-- > 0;)
        wram_copy(g, first + row * row_bytes, first + (row + 1) * row_bytes, row_bytes);
    wram_copy(g, past, first, row_bytes);
    ORIGINAL_EFFECT(charge_block_move(g, shown_bytes));
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

/* For the original's code and shadow mode: the start counts the rows'
 * bands' bytes, and a frame its bands down to none, in the menus' first
 * cell; the original copies the tables with block moves. */
static void leave_slide_started(GameState *g, unsigned rows, unsigned table_bytes)
{
    set_menu_scratch_slide_band_bytes(g, (uint16_t)(rows * BAND_BYTES));
    charge_block_move(g, rows * BAND_BYTES + 2 * table_bytes - rows * BAND_BYTES);
}

// $C2:EB9B
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
    ORIGINAL_EFFECT(leave_slide_started(g, rows, table_bytes));
}

// $C2:EB1F
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
    ORIGINAL_EFFECT(set_menu_scratch_slide_bands_left(g, 0));
}

/* The table as it rested goes back in. */
void menu_put_the_list_bands_back(GameState *g)
{
    set_menu_block_move_source(g, (uint16_t)RAM_MENU_SLIDE_TABLE_AT_REST);
    menu_ask_for_the_block_move(g);
}

/* ---- The slide's entries ------------------------------------------------------------------------- */

/* The task switch a frame of the slide goes on to, which waits for the
 * next frame and returns to the frame's caller; it keeps X, Y and the flags
 * for the caller across the wait. */
enum { YIELD = 0xC2821E, KEEPS_X_Y_AND_FLAGS = DC_A };

/* For the original's code and shadow mode: X and Y past the last band, as
 * the staging leaves them; the flags are the caller's. */
static void leave_bands_staged(Snes *s)
{
    const GameState *g = state_of(s);
    unsigned rows = menu_slide_rows(g);
    s->cpu.x = (uint16_t)(menu_slide_first_band(g) + rows * BAND_BYTES);
    s->cpu.y = (uint16_t)(rows * BAND_BYTES);
}

// $C2:EB9B
void menu_start_list_slide_entry(Snes *s)
{
    menu_start_list_slide(state_of(s));
    m_rts(s);
}

// $C2:EB1F
void menu_slide_list_a_frame_entry(Snes *s)
{
    menu_slide_list_a_frame(state_of(s));
    ORIGINAL_EFFECT(leave_bands_staged(s));
    m_stop_at_own(s, YIELD, KEEPS_X_Y_AND_FLAGS);
}

/* The screens keep their lists' tables among the menus' per-line tables,
 * below the slide's own, and slide up to twelve rows, a table of no more
 * than the slide's staging holds. */
enum { LINE_TABLES = 0x9000, MOST_ROWS = MENU_SLIDE_BANDS_COUNT - 1, MOST_TABLE_BYTES = MENU_SLIDE_TABLE_AT_REST_BYTES };

bool menu_list_slide_is_startable(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    unsigned rows = menu_slide_rows(g), table_bytes = menu_block_move_size(g) + 1u, at = menu_block_move_dest(g);
    bool ok = rows >= 1 && rows <= MOST_ROWS && table_bytes <= MOST_TABLE_BYTES && rows * BAND_BYTES <= table_bytes
              && at >= LINE_TABLES && at + table_bytes <= RAM_MENU_BLOCK_MOVE_SOURCE - 0x7E0000u;
    vars_peek(false);
    return ok;
}

/* A frame shows the bands from the first in view, the list's first row or
 * the row above it, while the slide is still within that first band: a
 * slide moves less than a row. */
static bool still_within_the_first_band(const GameState *g, uint8_t first)
{
    uint8_t moved = (uint8_t)menu_scroll_moved(g), lines = menu_slide_bands_lines(g, first);
    if ((int16_t)menu_scroll_moved(g) < 0)
        return (uint8_t)~moved < lines;
    return moved < lines;
}

bool menu_list_slide_frame_is_showable(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    unsigned rows = menu_slide_rows(g), offset = menu_slide_first_band(g), first = offset / BAND_BYTES;
    bool ok = rows >= 1 && offset % BAND_BYTES == 0 && first <= FIRST_ROW && first + rows <= MENU_SLIDE_BANDS_COUNT
              && still_within_the_first_band(g, (uint8_t)first);
    vars_peek(false);
    return ok;
}

/* What the original leaves its callers, who branch on it: where the list
 * scrolls a row, the push's ways in A's low byte and N set; otherwise the
 * direct page in A, as its last load takes it. */
void menu_leave_cursor_kept_on_the_list(Snes *s)
{
    const GameState *g = state_of(s);
    if (menu_list_scrolls_a_row(g)) {
        uint8_t moves = menu_cursor_moves(g);
        m_set_a8(s, moves);
        m_set_nz8(s, moves);
    } else {
        s->cpu.a = s->cpu.d;
        m_set_nz16(s, s->cpu.d);
    }
}

void menu_step_keeps_the_cursor_on_the_list(Snes *s)
{
    GameState *g = state_of(s);
    TIMED_AS(g, 0xC2EA53, menu_keep_the_cursor_on_the_list(g));
    ORIGINAL_EFFECT(menu_leave_cursor_kept_on_the_list(s));
}

void menu_keep_the_cursor_on_the_list_entry(Snes *s)
{
    menu_keep_the_cursor_on_the_list(state_of(s));
    menu_leave_cursor_kept_on_the_list(s);
    m_rts(s);
}

/* ---- Paging ------------------------------------------------------------------------------------- */

/* The shoulder assignments page a list: the first back a page, the second
 * on a page. */
static const MenuButton PAGE_BACK = MENU_PAGE_ONE_WAY, PAGE_ON = MENU_PAGE_THE_OTHER;

/* For the original's code and shadow mode: the paging takes the list's
 * first row in its cell and leaves the new one there. */
static void leave_paged_top(GameState *g, uint8_t top) { set_menu_paging_top(g, top); }

// $C2:93BF
/* A page past either end goes to the end instead, its cursor to the end's
 * place. The sums are a byte's, as the original's: a page back from near
 * the start wraps past the last first row, which is how it finds the
 * start. */
uint8_t menu_turn_page(GameState *g, uint8_t top)
{
    uint8_t rows = menu_paging_rows(g), last_top = menu_paging_last_top(g), to = top;
    if (menu_asked(g, PAGE_BACK)) {
        to = (uint8_t)(top - rows);
        if (to >= last_top) {
            set_menu_cursor_index(g, menu_list_first_place(g));
            to = 0;
        }
    } else if (menu_asked(g, PAGE_ON)) {
        unsigned on = (unsigned)top + rows;
        to = (uint8_t)on;
        if (on > 0xFF || to >= last_top) {
            set_menu_cursor_index(g, menu_list_last_place(g));
            to = last_top;
        }
    }
    if (to != top) {
        set_menu_scroll_bar_thumb(g, to);
        set_menu_cursor_frame_start(g, MENU_CURSOR_MOVED_ANYWAY);
    }
    ORIGINAL_EFFECT(leave_paged_top(g, to));
    return to;
}

/* The paging's answer in A: whether a page turned. */
enum { TURNED = 0xFF, NOT_TURNED = 0x00 };

// $C2:93BF
void menu_turn_page_entry(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t top = menu_paging_top(g);
    m_set_a8(s, menu_turn_page(g, top) != top ? TURNED : NOT_TURNED);
    m_rts(s);
}
