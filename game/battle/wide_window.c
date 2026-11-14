/* Battle: the battle window laid out across a wider view.
 *
 * The window is one tilemap a screen across: the text on its rows 1 to 6 and
 * the frames around it on rows 16 to 21. On a wider view (vid_view) it is laid
 * out across the view each frame (core/panel_layout.h), its windows the size
 * the original draws them. Where the frames have a border between windows,
 * the windows left of it -- the list of the enemies, or a member's command
 * window drawn over it, or the tech page's list -- keep to the view's left
 * side, and the window right of it -- the party's, or the tech page's MP --
 * to its right side, their text going with them; the battlefield shows in the
 * gap between. A command window drawn over the enemies' list covers the
 * names; on a wider view the rest of that list goes too, so the command
 * window stands alone rather than beside an empty piece of a window. A window
 * across the whole screen (the item list) and the message window stay in the
 * middle of the view, as the original draws them.
 *
 * What the rows show is noted as the vblank sends them, and the borders
 * between the windows as it sends the frames, so the layout always fits what
 * the window holds in VRAM. All of it is the program's own state, beside the
 * game's memory, read outside the game's time. */
#include "battle/wide_window.h"

#include "charge.h"
#include "core/panel_layout.h"
#include "platform/video.h"
#include "vars.h"

/* The window's map, its text rows and its frames' rows. The message window's
 * rows (24 to 30) stay as the original lays them out. */
enum { WINDOW_MAP = 0x7800 };
static const PanelRows TEXT_ROWS = { 1, 6 };
/* All the window's rows above the message window's, the frames' and the
 * blank rows round them among them: parted alike, so the gap between the
 * windows holds nothing of the window on any line it shows. */
static const PanelRows WINDOW_ROWS = { 0, 24 };

/* ---- Noting what is sent -------------------------------------------------------------- */

void battle_note_window_rows_sent(GameState *g, WindowShows shows) { battle_wide_window(g)->shows = (uint8_t)shows; }

/* The frames' top row: a window's top left corner, or the top right one of a
 * window that another draws beside, marks where one window meets the next. */
enum { FRAME_TOP_LEFT = 0xF0, FRAME_TOP_RIGHT = 0xF3 };

void battle_note_frames_sent(GameState *g)
{
    uint8_t divider = 0, first_right = 0;
    bool shared = false;
    uint32_t mark = charge_uncounted_begin(g);
    for (unsigned column = 1; column < PANEL_COLUMNS - 1; column++) {
        uint8_t tile = battle_window_frames_tilemap_tile(g, column);
        if (tile == FRAME_TOP_LEFT || tile == FRAME_TOP_RIGHT)
            divider = (uint8_t)column, shared = tile == FRAME_TOP_RIGHT;
        if (tile == FRAME_TOP_RIGHT && !first_right)
            first_right = (uint8_t)column;
    }
    charge_uncounted_end(g, mark);
    BattleWideWindow *w = battle_wide_window(g);
    w->divider = divider;
    w->shared = shared;
    /* a window's right corner short of the border: a command window drawn
     * over the enemies' list */
    w->command = first_right && first_right < divider ? first_right : 0;
}

/* ---- The layout ---------------------------------------------------------------------- */

/* The rows split at the border: the columns left of it (up to `left_columns`
 * of them) at the view's left side, and the columns from it on at its right
 * side; or, with no border, all of them in the middle, as the original draws
 * them. A border the two windows share (the tech page's) goes with both. */
static void split_at_the_border(PanelLayout *l, PanelRows rows, uint8_t divider, uint8_t left_columns)
{
    if (!divider)
        return;
    int8_t m = l->margin;
    panel_show_nothing(l, rows, 0, PANEL_COLUMNS);
    panel_show_columns(l, rows, 0, left_columns, (int8_t)-m);
    panel_show_columns(l, rows, divider, (uint8_t)(PANEL_COLUMNS - divider), (int8_t)(divider + m));
}

void battle_lay_out_the_window_across(GameState *g, Platform *pf)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_wider(view))
        return;
    const BattleWideWindow *w = battle_wide_window(g);
    PanelLayout *l = panel_layout_begin(g, view);
    uint8_t left = w->command ? (uint8_t)(w->command + 1) : (uint8_t)(w->divider + (w->shared ? 1 : 0));
    split_at_the_border(l, WINDOW_ROWS, w->divider, left);
    bool parted_text = w->shows == WINDOW_SHOWS_PARTY || w->shows == WINDOW_SHOWS_TECHS;
    if (w->divider && !parted_text) {   /* a list across the window: its text as the original lays it out */
        panel_show_nothing(l, TEXT_ROWS, (int8_t)-l->margin, (uint8_t)(PANEL_COLUMNS + 2 * l->margin));
        panel_show_columns(l, TEXT_ROWS, 0, PANEL_COLUMNS, 0);
    }
    panel_layout_send(l, pf, WINDOW_MAP);
}
