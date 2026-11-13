/* Battle: the battle window laid out across a wider view.
 *
 * The window is one tilemap a screen across: the text on its rows 1 to 6 and
 * the frames around it on rows 16 to 21. On a wider view (vid_view) it is laid
 * out across the view each frame (core/panel_layout.h). The windows at its
 * left -- the members' command windows, or the list of the enemies -- keep to
 * the view's left side; the party's window stretches to its right side, and
 * the parts of the members' lines (names, numbers, gauges) spread across it
 * from its left to its right. The tech page's two windows, the list and the
 * party's MP beside it, each stretch to their side of the view, the list
 * keeping to its left and the MP in the middle of theirs. The item list fills
 * the window, which stretches across the view, its names at the left and its
 * arrows at the right. The message window stretches across the view too, its
 * message kept to the left.
 *
 * What the rows show is noted as the vblank sends them, and the border between
 * the windows as it sends the frames, so the layout always fits what the
 * window holds in VRAM. All of it is the program's own state, beside the
 * game's memory, read outside the game's time. */
#include "battle/wide_window.h"

#include "battle/window.h"
#include "charge.h"
#include "core/panel_layout.h"
#include "platform/video.h"
#include "vars.h"

/* The window's map, its text rows and its frames' rows; and the message
 * window's (a battle message, a tech's description), its text a picture drawn
 * across its rows' tiles. */
enum { WINDOW_MAP = 0x7800 };
static const PanelRows TEXT_ROWS = { 1, 6 }, FRAME_ROWS = { 16, 6 };
static const PanelRows MESSAGE_ROWS = { 24, 4 }, MESSAGE_FRAME_ROWS = { 28, 3 };

/* ---- Noting what is sent -------------------------------------------------------------- */

void battle_note_window_rows_sent(GameState *g, WindowShows shows)
{
    BattleWideWindow *w = battle_wide_window(g);
    uint32_t mark = charge_uncounted_begin(g);
    w->shows = (uint8_t)shows;
    w->layout = (uint8_t)battle_window_layout_now(g);
    charge_uncounted_end(g, mark);
}

/* The frames' top row: a window's top left corner, or the top right one of a
 * window that another draws beside, marks where one window meets the next. */
enum { FRAME_TOP_LEFT = 0xF0, FRAME_TOP_RIGHT = 0xF3 };

void battle_note_frames_sent(GameState *g)
{
    uint8_t divider = 0;
    uint32_t mark = charge_uncounted_begin(g);
    for (unsigned column = 1; column < PANEL_COLUMNS - 1; column++) {
        uint8_t tile = battle_window_frames_tilemap_tile(g, column);
        if (tile == FRAME_TOP_LEFT || tile == FRAME_TOP_RIGHT)
            divider = (uint8_t)column;
    }
    charge_uncounted_end(g, mark);
    battle_wide_window(g)->divider = divider;
}

/* ---- The layout ---------------------------------------------------------------------- */

/* The frames: the windows left of the divider at the view's left side, and
 * the window right of it (the whole window, without one) stretched to its
 * right side; or, on the tech page, the two windows either side of the divider
 * each stretched to their side. */
static void lay_out_frames(PanelLayout *l, const BattleWideWindow *w)
{
    int8_t m = l->margin;
    uint8_t divider = w->divider;
    int8_t right = (int8_t)(PANEL_COLUMNS - 1 + m);
    if (w->shows == WINDOW_SHOWS_TECHS && divider) {
        panel_stretch_window(l, FRAME_ROWS, 0, divider, (int8_t)-m, (int8_t)divider);
        panel_stretch_window(l, FRAME_ROWS, divider, PANEL_COLUMNS - 1, (int8_t)divider, right);
        return;
    }
    if (divider)
        panel_show_columns(l, FRAME_ROWS, 0, divider, (int8_t)-m);
    panel_stretch_window(l, FRAME_ROWS, divider, PANEL_COLUMNS - 1, (int8_t)(divider - m), right);
}

/* A part of the members' lines: its first column and how many it takes. */
typedef struct {
    uint8_t column, columns;
} LinePart;

enum { NAME_COLUMNS = 5, HP_COLUMNS = 4, MAX_HP_COLUMNS = 7, MP_COLUMNS = 2, GAUGE_COLUMNS = 6 };

/* The parts of the members' lines in the layout, left to right: the name, the
 * HP and the MP (one part where they stand together), and the gauge. */
static unsigned line_parts(WindowLayout layout, LinePart part[4])
{
    MemberLine line = battle_member_line(layout, 0);
    uint8_t name = (uint8_t)(line.name % WINDOW_COLUMNS), hp = (uint8_t)(line.hp % WINDOW_COLUMNS);
    uint8_t mp = (uint8_t)(line.mp % WINDOW_COLUMNS), gauge = (uint8_t)(line.gauge % WINDOW_COLUMNS);
    unsigned n = 0;
    if (layout == LAYOUT_MAX_HP) {
        part[n++] = (LinePart){ name, NAME_COLUMNS };
        part[n++] = (LinePart){ hp, MAX_HP_COLUMNS };
        part[n++] = (LinePart){ mp, MP_COLUMNS };
        return n;
    }
    LinePart numbers = { hp, (uint8_t)(mp + MP_COLUMNS - hp) }, named = { name, NAME_COLUMNS };
    part[n++] = name < hp ? named : numbers;
    part[n++] = name < hp ? numbers : named;
    part[n++] = (LinePart){ gauge, GAUGE_COLUMNS };
    return n;
}

/* The party's lines: the windows left of the divider at the view's left side,
 * and the parts right of it spread from the party's window's left side (as
 * far from it as they were) to its right side. A part the windows at the left
 * cover stays covered. */
static void lay_out_party(PanelLayout *l, const BattleWideWindow *w)
{
    int8_t m = l->margin;
    panel_show_nothing(l, TEXT_ROWS, 0, PANEL_COLUMNS);
    if (w->divider)
        panel_show_columns(l, TEXT_ROWS, 0, (uint8_t)(w->divider + 1), (int8_t)-m);
    LinePart part[4];
    unsigned parts = line_parts((WindowLayout)w->layout, part);
    for (unsigned i = 0; i < parts; i++) {
        if (part[i].column <= w->divider)
            continue;
        int shift = -m + (int)(2 * m * (int)i) / (int)(parts - 1);
        panel_show_columns(l, TEXT_ROWS, part[i].column, part[i].columns, (int8_t)(part[i].column + shift));
    }
}

/* A list in the window: the columns left of `split` at the view's left side,
 * and the rest moved `right_moves` columns. */
static void lay_out_list(PanelLayout *l, uint8_t split, int8_t right_moves)
{
    panel_show_nothing(l, TEXT_ROWS, 0, PANEL_COLUMNS);
    panel_show_columns(l, TEXT_ROWS, 0, split, (int8_t)-l->margin);
    panel_show_columns(l, TEXT_ROWS, split, (uint8_t)(PANEL_COLUMNS - split), (int8_t)(split + right_moves));
}

/* The item list's halves: the names and counts, and the column after them and
 * the scroll arrows. */
enum { ITEM_LIST_HALF = 20 };

void battle_lay_out_the_window_across(GameState *g, Platform *pf)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_wider(view))
        return;
    const BattleWideWindow *w = battle_wide_window(g);
    PanelLayout *l = panel_layout_begin(g, view);
    lay_out_frames(l, w);
    panel_stretch_window(l, MESSAGE_FRAME_ROWS, 0, PANEL_COLUMNS - 1, (int8_t)-l->margin,
                         (int8_t)(PANEL_COLUMNS - 1 + l->margin));
    panel_show_nothing(l, MESSAGE_ROWS, 0, PANEL_COLUMNS);
    panel_show_columns(l, MESSAGE_ROWS, 0, PANEL_COLUMNS, (int8_t)-l->margin);
    switch ((WindowShows)w->shows) {
    case WINDOW_SHOWS_PARTY: lay_out_party(l, w); break;
    case WINDOW_SHOWS_TECHS: lay_out_list(l, w->divider, (int8_t)(l->margin / 2)); break;
    case WINDOW_SHOWS_ITEMS: lay_out_list(l, ITEM_LIST_HALF, l->margin); break;
    default: break;
    }
    panel_layout_send(l, pf, WINDOW_MAP);
}
