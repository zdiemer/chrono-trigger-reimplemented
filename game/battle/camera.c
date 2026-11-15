/* Battle: a smaller view's camera (core/view_framing.h).
 *
 * A battle is fought on the field's screen, the combatants wherever they
 * stand on it, and the battle window across its foot. A smaller view keeps
 * the window whole at its own foot, narrower (below), and frames the
 * combatants in the lines above it: the middle of the box round every combatant that is in the
 * fight and drawn, or the screen's middle while none is. */
#include "battle/camera.h"

#include "battle/animation.h"
#include "battle/battle.h"
#include "battle/combatant.h"
#include "charge.h"
#include "core/view_framing.h"
#include "vars.h"

/* The window's lines on the screen, as its line tables lay it out: 48 lines
 * from line 160, above the screen's last 16. */
enum { WINDOW_TOP = 160, WINDOW_LINES = 48 };
/* A combatant's point is at its feet; the box takes its middle. */
enum { MIDDLE_ABOVE_THE_FEET = 16 };

static ViewFocus the_combatants_middle(const GameState *g)
{
    int left = VID_ORIGINAL_VIEW_WIDTH, right = -1, top = VID_ORIGINAL_VIEW_HEIGHT, bottom = -1;
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        if (!combatant_in_fight(g, slot) || battle_object_hidden(g, slot))
            continue;
        unsigned obj = battle_combatant_object(slot);
        int x = battle_object_x(g, obj), y = battle_object_y(g, obj) - MIDDLE_ABOVE_THE_FEET;
        left = x < left ? x : left;
        right = x > right ? x : right;
        top = y < top ? y : top;
        bottom = y > bottom ? y : bottom;
    }
    if (right < 0)
        return view_screen_middle();
    return (ViewFocus){ (int16_t)((left + right) / 2), (int16_t)((top + bottom) / 2) };
}

/* ---- The window, narrower ------------------------------------------------------------------- *
 *
 * The window is a tilemap the screen's width (battle/wide_window.c): its
 * text on rows 1 to 6, BG3's, and the frames round it on rows 16 to 21,
 * BG1's, its windows' borders at the screen's sides. A smaller view 16 dots
 * narrower shows it whole by cutting two of its columns from inside a
 * window: two that hold no text on any row, between columns of the frames'
 * repeating pattern, nowhere near a border. Of the columns that can go, the
 * middle one goes, so the cut stays put while the window's text changes
 * little. Where none can, the window's middle is shown, its sides cut. */
enum {
    WINDOW_MAP = 0x7800, MAP_COLUMNS = 32, TEXT_FIRST_ROW = 1, TEXT_ROWS = 6, FRAME_FIRST_ROW = 16, FRAME_ROWS = 6,
    PATTERN_PERIOD = 4, CUT_COLUMNS = 2, COLUMN_DOTS = 8, TILE_NUMBER = 0x3FF, TILE_WORDS_2BPP = 8,
};

static uint16_t window_entry(const Platform *pf, unsigned row, int column)
{
    uint16_t w = 0;
    vid_read_vram(pf, (uint16_t)(WINDOW_MAP + row * MAP_COLUMNS + (unsigned)column), &w, 1);
    return w;
}

/* A column of the frames inside a window: each row's tile the same as a
 * column's two or four away (the pattern repeats; a border doesn't). */
static bool inside_a_window(const Platform *pf, int column)
{
    static const int8_t away[] = { -PATTERN_PERIOD, -PATTERN_PERIOD / 2, PATTERN_PERIOD / 2, PATTERN_PERIOD };
    for (unsigned row = FRAME_FIRST_ROW; row < FRAME_FIRST_ROW + FRAME_ROWS; row++) {
        uint16_t e = window_entry(pf, row, column);
        bool repeats = false;
        for (unsigned k = 0; k < sizeof away && !repeats; k++) {
            int c = column + away[k];
            repeats = c >= 0 && c < MAP_COLUMNS && window_entry(pf, row, c) == e;
        }
        if (!repeats)
            return false;
    }
    return true;
}

/* A column of the text with nothing drawn on any row. */
static bool no_text(const Platform *pf, uint16_t chars, int column)
{
    for (unsigned row = TEXT_FIRST_ROW; row < TEXT_FIRST_ROW + TEXT_ROWS; row++) {
        uint16_t words[TILE_WORDS_2BPP];
        uint16_t e = window_entry(pf, row, column);
        vid_read_vram(pf, (uint16_t)(chars + (e & TILE_NUMBER) * TILE_WORDS_2BPP), words, TILE_WORDS_2BPP);
        for (unsigned i = 0; i < TILE_WORDS_2BPP; i++)
            if (words[i])
                return false;
    }
    return true;
}

/* The first of the two columns to cut, or -1. */
static int column_to_cut(const Platform *pf, uint16_t text_chars)
{
    int found[MAP_COLUMNS], count = 0;
    for (int column = 1; column + CUT_COLUMNS < MAP_COLUMNS - 1; column++) {
        bool can = true;
        for (int c = column - 1; c <= column + CUT_COLUMNS && can; c++)
            can = inside_a_window(pf, c);
        for (int c = column; c < column + CUT_COLUMNS && can; c++)
            can = no_text(pf, text_chars, c);
        if (can)
            found[count++] = column;
    }
    return count ? found[count / 2] : -1;
}

void battle_frame_the_view(GameState *g, Platform *pf)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_smaller(view))
        return;
    uint32_t mark = charge_uncounted_begin(g);
    int16_t window_at = (int16_t)(view.height - WINDOW_LINES);
    VidBg text;
    int cut = -1, cut_dots = VID_ORIGINAL_VIEW_WIDTH - view.width;
    if (cut_dots == CUT_COLUMNS * COLUMN_DOTS && vid_layer_last_shown(pf, VID_BG3, WINDOW_TOP + 1, &text))
        cut = column_to_cut(pf, text.char_base);
    if (cut >= 0)
        view_keep_lines_cut(pf, WINDOW_TOP, WINDOW_LINES, window_at, (int16_t)(cut * COLUMN_DOTS), (uint16_t)cut_dots);
    else
        view_keep_lines(pf, WINDOW_TOP, WINDOW_LINES, window_at);
    ViewLines open = { 0, (uint16_t)window_at, WINDOW_TOP, WINDOW_TOP + WINDOW_LINES };
    view_centre_on(pf, the_combatants_middle(g), open);
    charge_uncounted_end(g, mark);
}
