/* The menus: a smaller view's camera (menu/camera.h). */
#include "menu/camera.h"

#include "charge.h"
#include "core/view_framing.h"
#include "menu/cursor.h"
#include "menu/equipment.h"
#include "menu/item_screen.h"
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
    int16_t column_top[VID_VIEW_PANELS - 2];   /* with a column moved by its own lines, its line beside `top` */
    int16_t column_end;                         /* the column's screen X past its last dot, or 0 */
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

/* ---- The items ------------------------------------------------------------------------------- *
 *
 * The items' screen fills 192 of the original's lines: the options and the
 * item's description above the list's ten rows, 12 lines apart, each row's
 * text 8 lines and the hand beside the cursor's 11. In a smaller view the
 * options and the description keep their text and a few lines of each edge,
 * and the list two of the four blank lines below each row but the cursor's.
 * With a window beside the list (the members an item is used on, three
 * windows of four rows 8 lines apart, or who can equip it), whose rows don't
 * fall between the list's, the list keeps its lines but those blank in both
 * columns: most of the gap between the first two members' windows, and a
 * line under the last row. A row the list slides past its ends shows cut a
 * frame or two as it moves. */
enum {
    ITEM_ROWS = 10, ITEM_FIRST_ROW = 88, ITEM_ROW_STEP = 12,
    ITEM_ROW_KEPT = 8, ITEM_ROW_LEFT_OUT = 2,   /* from a row's top, its text, then the lines left out */
    ITEM_LIST_TOP = 76, ITEM_LIST_END = 208,      /* the description's foot and the list's edges */
};

typedef struct {
    uint8_t top, end;
} LineSpan;

/* the options' window and the description's (its foot goes with the list's top) */
static const LineSpan ITEMS_ABOVE_THE_LIST[] = { { 16, 18 }, { 24, 40 }, { 44, 52 }, { 59, 68 } };
static const LineSpan ITEMS_LIST_BESIDE_A_WINDOW[] = { { ITEM_LIST_TOP, 120 }, { 123, 167 }, { 168, 205 } };

static int item_row_top(unsigned row) { return ITEM_FIRST_ROW + (int)row * ITEM_ROW_STEP; }

/* The list's row the cursor points at, or ITEM_ROWS where it isn't on the list. */
static unsigned items_cursor_row(const GameState *g)
{
    if (!menu_cursor_can_be_placed(g))
        return ITEM_ROWS;
    int pointed_at = menu_cursor_places_y(g, menu_cursor_index(g)) + POINTED_AT_BELOW;
    int row = (pointed_at - ITEM_FIRST_ROW + ITEM_ROW_STEP / 2) / ITEM_ROW_STEP;
    return pointed_at < ITEM_FIRST_ROW - ITEM_ROW_STEP / 2 || row >= ITEM_ROWS ? ITEM_ROWS : (unsigned)row;
}

static void spans(Bands *b, const LineSpan *s, unsigned count)
{
    for (unsigned k = 0; k < count; k++)
        band(b, s[k].top, s[k].end);
}

static bool items_bands(const GameState *g, Bands *b)
{
    if ((MenuScreen)menu_screen(g) != MENU_SCREEN_ITEMS)
        return false;
    spans(b, ITEMS_ABOVE_THE_LIST, sizeof ITEMS_ABOVE_THE_LIST / sizeof ITEMS_ABOVE_THE_LIST[0]);
    if (menu_items_window_beside_the_list(g)) {
        spans(b, ITEMS_LIST_BESIDE_A_WINDOW, sizeof ITEMS_LIST_BESIDE_A_WINDOW / sizeof ITEMS_LIST_BESIDE_A_WINDOW[0]);
        return true;
    }
    unsigned cursor = items_cursor_row(g);
    int from = ITEM_LIST_TOP;
    for (unsigned row = 0; row + 1 < ITEM_ROWS; row++) {
        if (row == cursor)
            continue;
        band(b, from, item_row_top(row) + ITEM_ROW_KEPT);
        from = item_row_top(row) + ITEM_ROW_KEPT + ITEM_ROW_LEFT_OUT;
    }
    band(b, from, ITEM_LIST_END);
    return true;
}

/* ---- A shop ---------------------------------------------------------------------------------- *
 *
 * A shop's screen fills 200 of the original's lines: the description at the
 * top; the list of wares, eight rows 12 lines apart, beside what is bought
 * or sold, the party's money and how many it has of the ware; and the party
 * at the foot, the member the ware suits jumping. In a smaller view the
 * description keeps its text and a few lines of its foot, and the party its
 * window with room for the jump. The list keeps its rows and the lines
 * beside them, but for the blank lines under its first and third rows,
 * where the column beside it is blank too whatever it shows (the price and
 * power of a ware being bought stand under the fourth and sixth); the hand
 * beside either of those rows shows its top nine lines of eleven. */
enum {
    SHOP_FIRST_ROW = 52, SHOP_ROW_STEP = 12,
    SHOP_ROW_KEPT = 8, SHOP_ROW_LEFT_OUT = 4,   /* from a row's top: its text, then the lines left out */
    SHOP_LIST_TOP = 48, SHOP_LIST_END = 146,
};
static const LineSpan SHOP_DESCRIPTION[] = { { 16, 28 }, { 42, 44 } };
static const LineSpan SHOP_PARTY = { 152, 208 };
static const uint8_t SHOP_ROWS_WITH_A_GAP[] = { 0, 2 };

static bool shop_bands(const GameState *g, Bands *b)
{
    if ((MenuScreen)menu_screen(g) != MENU_SCREEN_SHOP)
        return false;
    spans(b, SHOP_DESCRIPTION, sizeof SHOP_DESCRIPTION / sizeof SHOP_DESCRIPTION[0]);
    int from = SHOP_LIST_TOP;
    for (unsigned k = 0; k < sizeof SHOP_ROWS_WITH_A_GAP; k++) {
        int text_end = SHOP_FIRST_ROW + SHOP_ROWS_WITH_A_GAP[k] * SHOP_ROW_STEP + SHOP_ROW_KEPT;
        band(b, from, text_end);
        from = text_end + SHOP_ROW_LEFT_OUT;
    }
    band(b, from, SHOP_LIST_END);
    band(b, SHOP_PARTY.top, SHOP_PARTY.end);
    return true;
}

/* ---- The equipment --------------------------------------------------------------------------- *
 *
 * The equipment's screen is two columns of windows from line 16 to 208. On
 * the right, the member's portrait, the four slots (rows 12 lines apart, the
 * hand beside one), the figures and the experience; on the left, the
 * members' windows (rows 8 apart) and the clock, or, once a member is
 * chosen, that member's window, the slot's name and what fits it (rows 12
 * apart). Choosing from what fits, the left's list is longer, and a window
 * for the item's description takes the foot of both columns. The columns'
 * rows don't line up, so they have almost no blank lines in common. In a
 * smaller view each column leaves out its own: every window and every row
 * stays, each column a few lines shorter at its windows' edges and between
 * groups of rows, the figures' rows 8 or 9 lines apart, the slots' and the
 * list's whole for the hand. The left column is moved by its own lines
 * beside the right (the original moves it by columns already, by mode 2's
 * offsets). Both columns keep 160 lines, the description's the same lines
 * in both, and the view starts a band wherever either column's next run
 * starts: 12 bands at most, of the 14 there are. */
enum { EQUIPMENT_COLUMN_END = 112 };   /* the left column's windows end short of the screen's X 112 */

static const LineSpan EQUIPMENT_RIGHT[] = {
    { 16, 19 }, { 23, 72 }, { 79, 128 }, { 131, 140 }, { 144, 152 }, { 156, 164 }, { 168, 176 }, { 182, 208 },
};
static const LineSpan EQUIPMENT_MEMBERS[] = {
    { 16, 19 }, { 23, 56 }, { 60, 104 }, { 108, 152 }, { 156, 165 }, { 175, 184 }, { 190, 208 },
};
static const LineSpan EQUIPMENT_SLOT[] = {
    { 16, 19 }, { 23, 56 }, { 60, 68 }, { 79, 89 }, { 92, 94 }, { 98, 100 }, { 106, 208 },
};
static const LineSpan EQUIPMENT_RIGHT_OVER_THE_DESCRIPTION[] = {
    { 16, 19 }, { 23, 72 }, { 79, 140 }, { 143, 152 }, { 155, 164 }, { 167, 178 }, { 184, 200 }, { 204, 206 },
};
static const LineSpan EQUIPMENT_WHAT_FITS[] = {
    { 16, 19 }, { 23, 56 }, { 60, 61 }, { 66, 68 }, { 75, 178 }, { 184, 200 }, { 204, 206 },
};

typedef struct {
    const LineSpan *spans;
    uint8_t count;
} Column;
#define COLUMN(spans) { spans, (uint8_t)(sizeof spans / sizeof spans[0]) }

/* The bands of two columns side by side: down the view, a band wherever
 * either column's next run of lines starts; false where they need more than
 * the bands there are. */
static bool bands_of_two_columns(Column left, Column right, int16_t column_end, Bands *b)
{
    unsigned l = 0, r = 0;
    int left_at = left.spans[0].top, right_at = right.spans[0].top;
    b->column_end = column_end;
    while (l < left.count && r < right.count) {
        if (b->count == VID_VIEW_PANELS - 2)
            return false;
        int lines = left.spans[l].end - left_at;
        if (right.spans[r].end - right_at < lines)
            lines = right.spans[r].end - right_at;
        b->column_top[b->count] = (int16_t)left_at;
        band(b, right_at, right_at + lines);
        left_at += lines, right_at += lines;
        if (left_at == left.spans[l].end && ++l < left.count)
            left_at = left.spans[l].top;
        if (right_at == right.spans[r].end && ++r < right.count)
            right_at = right.spans[r].top;
    }
    return true;
}

static bool equipment_bands(const GameState *g, Bands *b)
{
    if ((MenuScreen)menu_screen(g) != MENU_SCREEN_EQUIPMENT)
        return false;
    static const Column RIGHT = COLUMN(EQUIPMENT_RIGHT), MEMBERS = COLUMN(EQUIPMENT_MEMBERS),
                        SLOT = COLUMN(EQUIPMENT_SLOT),
                        RIGHT_OVER_THE_DESCRIPTION = COLUMN(EQUIPMENT_RIGHT_OVER_THE_DESCRIPTION),
                        WHAT_FITS = COLUMN(EQUIPMENT_WHAT_FITS);
    if (menu_equip_choosing_an_item(g))
        return bands_of_two_columns(WHAT_FITS, RIGHT_OVER_THE_DESCRIPTION, EQUIPMENT_COLUMN_END, b);
    return bands_of_two_columns(menu_equip_choosing_a_member(g) ? MEMBERS : SLOT, RIGHT, EQUIPMENT_COLUMN_END, b);
}
#undef COLUMN

/* ---- The windowed screens ------------------------------------------------------------------------ *
 *
 * The other screens are stacks of windows, and most of them run past 160
 * lines. Each is laid out again as runs of its lines: every window's top and
 * foot edges, a few lines of each, and what is inside them, the blank lines
 * between a window's edge and its first or last line of text left out, and
 * the blank lines both columns share where a screen has two. Where that still
 * leaves more lines than the view has, one window's insides (the run marked
 * `follows`) show the most of their lines the view has room for, round the
 * cursor's place when it is in them and from their top when it isn't, the
 * other windows whole above and below them. Those lines start and end only
 * where both columns are between rows of text (the layout's `cuts`), so no
 * row is shown sliced; what room is over shows black. The runs were
 * measured on the screens as the original draws them; the hand stands at
 * most two lines above its place and reaches 14 below it. */
typedef struct {
    uint8_t top, end;
    bool follows;
} KeptLines;

typedef struct {
    const KeptLines *runs;
    uint8_t count;
    const uint8_t *cuts;   /* the lines its run that follows may start or end at, in order */
    uint8_t cut_count;
} WindowedLayout;

#define LAYOUT(runs) { runs, (uint8_t)(sizeof runs / sizeof runs[0]), NULL, 0 }
#define FOLLOWING(runs, cuts) { runs, (uint8_t)(sizeof runs / sizeof runs[0]), cuts, (uint8_t)sizeof cuts }

#define KEEP(top, end) { top, end, false }
#define FOLLOW(top, end) { top, end, true }

/* Under the main menu's window: the options and the list beside the party
 * whole, the description (which the panel leaves blank) left out. */
static const KeptLines MAIN_MENU_ITEMS_RUNS[] = {
    KEEP(16, 20), KEEP(23, 42), KEEP(43, 48), KEEP(80, 208),
};
/* The techs: the members down the left beside the tabs and the list, the
 * blank lines at the top of their windows left out, and the tech's
 * description. Nothing inside the list is left out: it scrolls a line at a
 * time, so no line inside it is always blank. */
static const KeptLines TECHS_RUNS[] = {
    KEEP(16, 19), KEEP(22, 156), KEEP(156, 163), KEEP(179, 190), KEEP(204, 208),
};
/* The settings: the icons down the left, the chosen setting's choices down
 * the right, and its name at the foot. */
static const KeptLines SETTINGS_RUNS[] = {
    KEEP(16, 18), KEEP(22, 48), KEEP(56, 72), KEEP(80, 108), KEEP(112, 144), KEEP(148, 170), KEEP(172, 180), KEEP(183, 201), KEEP(204, 208),
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
/* the summary's first row of each column, or all of it */
static const uint8_t LOAD_ERASING_CUTS[] = { 136, 160, 214 };
/* Saving: the three slots whole as far as the third's line and hand, where
 * the question whether to save over a game stands beside them, and the
 * summary. */
static const KeptLines SAVE_RUNS[] = {
    KEEP(16, 106), KEEP(109, 112), KEEP(128, 131), KEEP(134, 152), KEEP(160, 201), KEEP(204, 208),
};
#undef SLOT
/* The equipment's panel under the main menu: the members and the clock down
 * the left beside the member's portrait, equipment and figures, from the
 * top. The two columns' rows are 8 and 12 lines apart, so they are both
 * between rows only here and there: the panel shows down to the figures'
 * last row, the clock and the experience below it out of view. */
static const KeptLines EQUIPMENT_RUNS[] = { FOLLOW(16, 208) };
static const uint8_t EQUIPMENT_CUTS[] = { 16, 72, 104, 112, 116, 152, 164, 176, 184, 200, 208 };
/* Exchanging the party: three members' windows and the window to exchange
 * them, each its edges and its rows. At the gate, the members both sides
 * and the question in the fourth window, a line less of each edge. */
#define MEMBER_WINDOW(t) KEEP(t, (t) + 2), KEEP((t) + 7, (t) + 41), KEEP((t) + 46, (t) + 48)
static const KeptLines EXCHANGE_RUNS[] = {
    MEMBER_WINDOW(16), MEMBER_WINDOW(64), MEMBER_WINDOW(112), KEEP(160, 163), KEEP(172, 190), KEEP(204, 208),
};
static const KeptLines EXCHANGE_AT_THE_GATE_RUNS[] = {
    MEMBER_WINDOW(16), MEMBER_WINDOW(64), MEMBER_WINDOW(112), MEMBER_WINDOW(160),
};
#undef MEMBER_WINDOW
#undef KEEP
#undef FOLLOW

static const WindowedLayout MAIN_MENU_ITEMS_LAYOUT = LAYOUT(MAIN_MENU_ITEMS_RUNS);
static const WindowedLayout TECHS_LAYOUT = LAYOUT(TECHS_RUNS);
static const WindowedLayout SETTINGS_LAYOUT = LAYOUT(SETTINGS_RUNS);
static const WindowedLayout LOAD_LAYOUT = LAYOUT(LOAD_RUNS);
static const WindowedLayout LOAD_ERASING_LAYOUT = FOLLOWING(LOAD_ERASING_RUNS, LOAD_ERASING_CUTS);
static const WindowedLayout SAVE_LAYOUT = LAYOUT(SAVE_RUNS);
static const WindowedLayout EQUIPMENT_LAYOUT = FOLLOWING(EQUIPMENT_RUNS, EQUIPMENT_CUTS);
static const WindowedLayout EXCHANGE_LAYOUT = LAYOUT(EXCHANGE_RUNS);
static const WindowedLayout EXCHANGE_AT_THE_GATE_LAYOUT = LAYOUT(EXCHANGE_AT_THE_GATE_RUNS);

/* The main menu's window, its icons and their arrows, over the panel of the
 * option the cursor is on, which is that option's screen. */
enum { MAIN_MENU_WINDOW_TOP = 94, MAIN_MENU_WINDOW_END = 144 };
enum { HAND_ABOVE = 2, HAND_BELOW = 14 };

static const WindowedLayout *layout_of_the_option(uint8_t option)
{
    switch ((MainMenuOption)option) {
    case MAIN_MENU_EQUIPMENT: return &EQUIPMENT_LAYOUT;
    case MAIN_MENU_ITEMS: return &MAIN_MENU_ITEMS_LAYOUT;
    case MAIN_MENU_TECHS: return &TECHS_LAYOUT;
    case MAIN_MENU_SETTINGS: return &SETTINGS_LAYOUT;
    case MAIN_MENU_EXCHANGE: return &EXCHANGE_LAYOUT;
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
    case MENU_SCREEN_TECHS: return &TECHS_LAYOUT;
    case MENU_SCREEN_SETTINGS: return &SETTINGS_LAYOUT;
    case MENU_SCREEN_SAVE:
    case MENU_SCREEN_SAVE_POINT: return &SAVE_LAYOUT;
    case MENU_SCREEN_LOAD: return menu_load_screen_asks_to_erase(g) ? &LOAD_ERASING_LAYOUT : &LOAD_LAYOUT;
    case MENU_SCREEN_EXCHANGE: return &EXCHANGE_LAYOUT;
    case MENU_SCREEN_EXCHANGE_AT_THE_GATE: return &EXCHANGE_AT_THE_GATE_LAYOUT;
    default: return NULL;
    }
}

/* The run that follows: the most of its lines between two of the layout's
 * cuts that `room` holds, holding the focus where it lies in the run (the
 * whole of it, or failing that the line the cursor points at), else from
 * the run's top; false where no two cuts are near enough. */
static bool lines_between_cuts(const WindowedLayout *layout, int room, int from_at_least, int to_at_least,
                               int *top, int *end)
{
    int most = 0;
    for (unsigned i = 0; i < layout->cut_count; i++)
        for (unsigned j = i + 1; j < layout->cut_count; j++) {
            int from = layout->cuts[i], to = layout->cuts[j];
            if (from <= from_at_least && to_at_least <= to && to - from <= room && to - from > most)
                most = to - from, *top = from, *end = to;
        }
    return most > 0;
}

static bool lines_shown(const WindowedLayout *layout, KeptLines run, int room, int focus_top, int focus_end,
                        int *top, int *end)
{
    if (focus_end <= run.top || focus_top >= run.end)
        return lines_between_cuts(layout, room, run.top, run.top, top, end);
    int pointed_at = focus_top + HAND_ABOVE + POINTED_AT_BELOW;
    return lines_between_cuts(layout, room, focus_top, focus_end, top, end) ||
           lines_between_cuts(layout, room, pointed_at, pointed_at + 1, top, end);
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
            int top, end;
            if (!lines_shown(layout, runs[k], room, focus_top, focus_end, &top, &end))
                return false;
            band(b, top, end);
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
        if (b->column_end)
            view_keep_lines_beside(pf, b->top[k], b->lines[k], (int16_t)at, b->column_top[k], b->column_end);
        else
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
    if (naming_bands(g, &bands) || items_bands(g, &bands) || shop_bands(g, &bands) || equipment_bands(g, &bands) ||
        windowed_bands(g, view.height, &bands))
        keep_the_bands_in_view(pf, view, &bands);
    else
        view_follow(pf, what_the_player_follows(g), view_all_lines(view));
    charge_uncounted_end(g, mark);
}
