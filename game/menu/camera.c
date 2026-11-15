/* The menus: a smaller view's camera (menu/camera.h). */
#include "menu/camera.h"

#include "charge.h"
#include "core/view_framing.h"
#include "menu/cursor.h"
#include "menu/load_screen.h"
#include "menu/main_menu.h"
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

/* ---- The windowed screens ------------------------------------------------------------------------ *
 *
 * The other screens are stacks of windows, and most of them run past 160
 * lines. Each is laid out again as runs of its lines: every window's top and
 * foot edges, a few lines of each, and what is inside them, the blank lines
 * between a window's edge and its first or last line of text left out, and
 * the blank lines both columns share where a screen has two. Where that still
 * leaves more lines than the view has, one window's insides (the run marked
 * `follows`) show as many of their lines as are over, round the cursor's
 * place when it is in them and from their top when it isn't, the other
 * windows whole above and below them. The runs were measured on the screens
 * as the original draws them; the hand stands at most two lines above its
 * place and reaches 14 below it. */
typedef struct {
    uint8_t top, end;
    bool follows;
} KeptLines;

typedef struct {
    const KeptLines *runs;
    uint8_t count;
} WindowedLayout;

#define LAYOUT(runs) { runs, (uint8_t)(sizeof runs / sizeof runs[0]) }

/* The items: the options, the item's description, and the list beside the
 * party, which follows the cursor. */
#define KEEP(top, end) { top, end, false }
#define FOLLOW(top, end) { top, end, true }

static const KeptLines ITEMS_RUNS[] = {
    KEEP(16, 20), KEEP(23, 42), KEEP(43, 52), KEEP(55, 71), KEEP(75, 84), FOLLOW(84, 204), KEEP(204, 208),
};
/* The techs: the members down the left beside the tabs and the list, which
 * follow the cursor (the list scrolls a line at a time, so no line inside
 * it is always blank), and the tech's description. */
static const KeptLines TECHS_RUNS[] = {
    KEEP(16, 20), FOLLOW(20, 156), KEEP(156, 166), KEEP(176, 192), KEEP(203, 208),
};
/* The settings: the icons down the left, the chosen setting's choices down
 * the right, and its name at the foot. */
static const KeptLines SETTINGS_RUNS[] = {
    KEEP(16, 19), KEEP(22, 48), KEEP(56, 72), KEEP(80, 108), KEEP(112, 144), KEEP(148, 170), KEEP(172, 180), KEEP(183, 201), KEEP(204, 208),
};
/* The saves to load: New Game and the three slots, each its edges and its
 * line with the hand beside it, and the slot's summary, the place and time
 * on the left and the party on the right. */
#define SLOT(t) KEEP(t, (t) + 3), KEEP((t) + 11, (t) + 27), KEEP((t) + 29, (t) + 32)
static const KeptLines LOAD_RUNS[] = {
    SLOT(8), SLOT(40), SLOT(72), SLOT(104), KEEP(136, 160), KEEP(168, 214),
};
/* While it asks whether to erase a slot: the question whole, over the
 * slots, and the summary below it from its top, as far as there is room. */
static const KeptLines LOAD_ERASING_RUNS[] = {
    SLOT(8), KEEP(40, 118), KEEP(118, 125), KEEP(133, 136), FOLLOW(136, 214),
};
/* Saving: the three slots whole as far as the third's line and hand, where
 * the question whether to save over a game stands beside them, and the
 * summary. */
static const KeptLines SAVE_RUNS[] = {
    KEEP(16, 106), KEEP(109, 112), KEEP(128, 131), KEEP(134, 152), KEEP(160, 201), KEEP(204, 208),
};
#undef SLOT
/* A shop: the description, the list beside what is bought or sold, which
 * follows the cursor, and the party with room for its jump. */
static const KeptLines SHOP_RUNS[] = {
    KEEP(8, 12), KEEP(16, 30), KEEP(35, 46), FOLLOW(46, 146), KEEP(146, 214),
};
#undef KEEP
#undef FOLLOW

static const WindowedLayout ITEMS_LAYOUT = LAYOUT(ITEMS_RUNS);
static const WindowedLayout TECHS_LAYOUT = LAYOUT(TECHS_RUNS);
static const WindowedLayout SETTINGS_LAYOUT = LAYOUT(SETTINGS_RUNS);
static const WindowedLayout LOAD_LAYOUT = LAYOUT(LOAD_RUNS);
static const WindowedLayout LOAD_ERASING_LAYOUT = LAYOUT(LOAD_ERASING_RUNS);
static const WindowedLayout SAVE_LAYOUT = LAYOUT(SAVE_RUNS);
static const WindowedLayout SHOP_LAYOUT = LAYOUT(SHOP_RUNS);

/* The main menu's window, its icons and their arrows, over the panel of the
 * option the cursor is on, which is that option's screen. */
enum { MAIN_MENU_WINDOW_TOP = 94, MAIN_MENU_WINDOW_END = 144 };
enum { HAND_ABOVE = 2, HAND_BELOW = 14 };

static const WindowedLayout *layout_of_the_option(uint8_t option)
{
    switch ((MainMenuOption)option) {
    case MAIN_MENU_ITEMS: return &ITEMS_LAYOUT;
    case MAIN_MENU_TECHS: return &TECHS_LAYOUT;
    case MAIN_MENU_SETTINGS: return &SETTINGS_LAYOUT;
    default: return NULL;
    }
}

/* The screen's layout, if it has one, and the lines that must stay in view:
 * the cursor's hand, or on the main menu its window. */
static const WindowedLayout *windowed_layout(const GameState *g, int *focus_top, int *focus_end)
{
    if (!menu_cursor_can_be_placed(g))
        return NULL;
    int y = menu_cursor_places_y(g, menu_cursor_index(g));
    *focus_top = y - HAND_ABOVE;
    *focus_end = y + HAND_BELOW;
    switch ((MenuScreen)menu_screen(g)) {
    case MENU_SCREEN_MAIN_MENU:
        *focus_top = MAIN_MENU_WINDOW_TOP;
        *focus_end = MAIN_MENU_WINDOW_END;
        return layout_of_the_option(menu_cursor_index(g));
    case MENU_SCREEN_ITEMS: return &ITEMS_LAYOUT;
    case MENU_SCREEN_TECHS: return &TECHS_LAYOUT;
    case MENU_SCREEN_SETTINGS: return &SETTINGS_LAYOUT;
    case MENU_SCREEN_SAVE:
    case MENU_SCREEN_SAVE_POINT: return &SAVE_LAYOUT;
    case MENU_SCREEN_LOAD: return menu_load_screen_asks_to_erase(g) ? &LOAD_ERASING_LAYOUT : &LOAD_LAYOUT;
    case MENU_SCREEN_SHOP: return &SHOP_LAYOUT;
    default: return NULL;
    }
}

/* The run that follows: `room` of its lines, round the focus where it lies
 * in them (the focus whole where it fits), else from its top. */
static int first_line_shown(KeptLines run, int room, int focus_top, int focus_end)
{
    int top = run.top;
    if (focus_end <= run.top || focus_top >= run.end)
        return top;
    int middle = (focus_top + focus_end) / 2;
    top = middle - room / 2;
    if (top > focus_top)
        top = focus_top;
    if (top + room < focus_end)
        top = focus_end - room;
    if (top + room > run.end)
        top = run.end - room;
    if (top < run.top)
        top = run.top;
    return top;
}

/* The layout's bands for a view `height` lines tall, false where they
 * don't fit (the camera then follows the cursor over the screen). */
enum { MOST_RUNS = VID_VIEW_PANELS - 2 };

static void add_run(KeptLines *runs, unsigned *count, int top, int end, bool follows)
{
    if (end > top && *count < MOST_RUNS)
        runs[(*count)++] = (KeptLines){ (uint8_t)top, (uint8_t)end, follows };
}

/* The layout's runs, and on the main menu its window's lines among them
 * whole, unless a run that follows holds them (which keeps them in view). */
static unsigned runs_with_the_window(const WindowedLayout *layout, int focus_top, int focus_end, bool window,
                                     KeptLines *runs)
{
    for (unsigned k = 0; k < layout->count; k++) {
        KeptLines r = layout->runs[k];
        if (r.follows && r.top <= focus_top && focus_end <= r.end)
            window = false;
    }
    unsigned count = 0;
    bool window_added = false;
    for (unsigned k = 0; k < layout->count; k++) {
        KeptLines r = layout->runs[k];
        if (!window || r.follows) {
            if (window && !window_added && r.top >= focus_end) {
                add_run(runs, &count, focus_top, focus_end, false);
                window_added = true;
            }
            add_run(runs, &count, r.top, r.end, r.follows);
            continue;
        }
        add_run(runs, &count, r.top, r.end < focus_top ? r.end : focus_top, false);
        if (!window_added && r.end > focus_top) {
            add_run(runs, &count, focus_top, focus_end, false);
            window_added = true;
        }
        add_run(runs, &count, r.top > focus_end ? r.top : focus_end, r.end, false);
    }
    if (window && !window_added)
        add_run(runs, &count, focus_top, focus_end, false);
    return count;
}

static bool bands_of_the_windows(const WindowedLayout *layout, int height, int focus_top, int focus_end,
                                 bool window, Bands *b)
{
    KeptLines runs[MOST_RUNS];
    unsigned count = runs_with_the_window(layout, focus_top, focus_end, window, runs);
    int fixed = 0;
    for (unsigned k = 0; k < count; k++)
        if (!runs[k].follows)
            fixed += runs[k].end - runs[k].top;
    int room = height - fixed;
    for (unsigned k = 0; k < count; k++) {
        if (!runs[k].follows) {
            band(b, runs[k].top, runs[k].end);
            continue;
        }
        int lines = runs[k].end - runs[k].top;
        if (room <= 0)
            return false;
        if (lines <= room) {
            band(b, runs[k].top, runs[k].end);
        } else {
            int top = first_line_shown(runs[k], room, focus_top, focus_end);
            band(b, top, top + room);
        }
    }
    return bands_lines(b) <= height;
}

static bool windowed_bands(const GameState *g, int height, Bands *b)
{
    int focus_top = 0, focus_end = 0;
    const WindowedLayout *layout = windowed_layout(g, &focus_top, &focus_end);
    if (!layout)
        return false;
    bool on_the_main_menu = (MenuScreen)menu_screen(g) == MENU_SCREEN_MAIN_MENU;
    return bands_of_the_windows(layout, height, focus_top, focus_end, on_the_main_menu, b);
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
    if (naming_bands(g, &bands) || windowed_bands(g, view.height, &bands))
        keep_the_bands_in_view(pf, view, &bands);
    else
        view_follow(pf, what_the_player_follows(g), view_all_lines(view));
    charge_uncounted_end(g, mark);
}
