/* The menus: a smaller view's camera (menu/camera.h). */
#include "menu/camera.h"

#include "charge.h"
#include "core/view_framing.h"
#include "menu/cursor.h"
#include "menu/menu.h"
#include "menu/naming.h"
#include "overworld/setup.h"
#include "vars.h"

/* The cursor's place is its hand's top left; the camera keeps the line it
 * points at, a little below it, in frame. Across, the screens keep within
 * 224 dots of the middle, which a view 240 across shows whole, so the camera
 * stays in the middle. */
enum { POINTED_AT_BELOW = 4 };

static ViewFocus what_the_player_follows(const GameState *g)
{
    ViewFocus middle = view_screen_middle();
    if (overworld_on_a_world_map(g) || !menu_cursor_can_be_placed(g))
        return middle;
    uint8_t at = menu_cursor_index(g);
    return (ViewFocus){ middle.x, (int16_t)(menu_cursor_places_y(g, at) + POINTED_AT_BELOW) };
}

/* ---- The naming ------------------------------------------------------------------------------ *
 *
 * The naming's screen fills 208 of the original's lines: the name's window
 * (the portrait and the name) above the letters' window, eight rows of
 * letters 16 lines apart. In a smaller view it is laid out again down the
 * view as bands of its lines, every one whole across: the name's window,
 * the letters' window's top, each row of letters with most of the blank
 * lines between them left out (the row the cursor is on with room for its
 * hand), and the window's foot, a line or two of each window's edges left
 * out to fit. While it waits for the first button, the window of
 * instructions the original draws over three of the rows is kept whole in
 * their place. */
enum {
    NAME_WINDOW_TOP = 8, NAME_WINDOW_LINES = 62,
    NAME_WINDOW_EDGE_LEFT_OUT = 2,            /* lines of the name's window's edges left out on the grid */
    LETTERS_WINDOW_TOP = 72, LETTERS_WINDOW_TOP_END = 82,
    GRID_ROWS = 8, GRID_STEP = 16,
    FIRST_ROW = MENU_NAME_GRID_TOP + 1,      /* the first row's letters' top line */
    ROW_LINES = 10, CURSOR_ROW_LINES = 11,   /* a row's letters, and with the hand beside them */
    LETTERS_WINDOW_FOOT = 206, LETTERS_WINDOW_END = 214,
    INSTRUCTIONS_TOP = 96, INSTRUCTIONS_END = 144,
    INSTRUCTIONS_COVER_ROWS_UP_TO = 4,        /* the rows below the instructions start with the fifth */
    ROW_LINES_BY_THE_INSTRUCTIONS = 8,
};

typedef struct {
    int16_t at;
    uint8_t count;
    int16_t top[VID_VIEW_PANELS - 2];   /* two left for the black above and below */
    uint8_t lines[VID_VIEW_PANELS - 2];
} Bands;

static void band(Bands *b, int top, int end)
{
    if (b->count < VID_VIEW_PANELS - 2 && end > top) {
        b->top[b->count] = (int16_t)top;
        b->lines[b->count++] = (uint8_t)(end - top);
    }
}

static int bands_lines(const Bands *b)
{
    int n = 0;
    for (unsigned k = 0; k < b->count; k++)
        n += b->lines[k];
    return n;
}

static int row_top(unsigned row) { return FIRST_ROW + (int)row * GRID_STEP; }

/* The bands while the player names: every row, the cursor's with its hand. */
static void bands_of_the_grid(const GameState *g, Bands *b)
{
    unsigned cursor = (unsigned)(menu_naming_cursor_place(g) >> 8) - MENU_NAME_GRID_TOP;
    cursor /= GRID_STEP;
    band(b, NAME_WINDOW_TOP + NAME_WINDOW_EDGE_LEFT_OUT,
         NAME_WINDOW_TOP + NAME_WINDOW_LINES - NAME_WINDOW_EDGE_LEFT_OUT);
    band(b, LETTERS_WINDOW_TOP, LETTERS_WINDOW_TOP_END);
    for (unsigned row = 0; row < GRID_ROWS; row++)
        band(b, row_top(row), row_top(row) + (row == cursor ? CURSOR_ROW_LINES : ROW_LINES));
    band(b, LETTERS_WINDOW_FOOT, LETTERS_WINDOW_END);
}

/* The bands while the instructions are up: the first row, the instructions
 * whole, the rows below them, a line or two of each window's edge left out
 * to fit. */
static void bands_with_the_instructions(Bands *b)
{
    enum { NAME_EDGE_LEFT_OUT = 4, TOP_LEFT_OUT = 2, FOOT_LINES = 4 };
    band(b, NAME_WINDOW_TOP + NAME_EDGE_LEFT_OUT, NAME_WINDOW_TOP + NAME_WINDOW_LINES - NAME_EDGE_LEFT_OUT);
    band(b, LETTERS_WINDOW_TOP + TOP_LEFT_OUT, INSTRUCTIONS_TOP);
    band(b, INSTRUCTIONS_TOP, INSTRUCTIONS_END);
    for (unsigned row = INSTRUCTIONS_COVER_ROWS_UP_TO; row < GRID_ROWS; row++)
        band(b, row_top(row), row_top(row) + ROW_LINES_BY_THE_INSTRUCTIONS);
    band(b, LETTERS_WINDOW_FOOT, LETTERS_WINDOW_FOOT + FOOT_LINES);
}

/* Whether the naming's grid is up, and its bands: the line effects the
 * naming runs say which, the first naming's (with its instructions) until
 * the first button, then the grid's (menu/naming.c). */
static bool naming_bands(const GameState *g, Bands *b)
{
    if ((MenuScreen)menu_screen(g) != MENU_SCREEN_NAMING)
        return false;
    uint8_t effects = menu_line_effects(g);
    if (effects == MENU_NAMING_FIRST_LINE_EFFECTS)
        bands_with_the_instructions(b);
    else if (effects == MENU_NAMING_LINE_EFFECTS)
        bands_of_the_grid(g, b);
    else
        return false;
    return true;
}

/* The bands down the view, in the middle of it where they leave lines over,
 * which show black. */
static void keep_the_bands_in_view(Platform *pf, VidView view, const Bands *b)
{
    int at = ((int)view.height - bands_lines(b)) / 2;
    if (at < 0)
        at = 0;
    view_keep_nothing(pf, 0, (uint16_t)at);
    for (unsigned k = 0; k < b->count; k++) {
        view_keep_lines(pf, b->top[k], b->lines[k], (int16_t)at);
        at += b->lines[k];
    }
    if (at < (int)view.height)
        view_keep_nothing(pf, (int16_t)at, (uint16_t)(view.height - at));
}

void menu_frame_the_view(GameState *g, Platform *pf)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_smaller(view))
        return;
    uint32_t mark = charge_uncounted_begin(g);
    Bands bands = { 0 };
    if (naming_bands(g, &bands))
        keep_the_bands_in_view(pf, view, &bands);
    else
        view_follow(pf, what_the_player_follows(g), view_all_lines(view));
    charge_uncounted_end(g, mark);
}
