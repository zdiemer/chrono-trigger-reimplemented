/* A wider view's arrangement of the panels (core/panel_layout.h).
 *
 * The layout is the video service's (VidMapLayout): for each of the map's
 * rows, a ring of columns round from the layer's column 0, each naming the
 * map's column it shows. A screen column is its place in the ring, the left
 * margin's counted down from the ring's end. The ring holds the original's
 * screen and sixteen columns either side, more than the widest view shows. */
#include "core/panel_layout.h"

enum { RING = VID_LAYOUT_COLUMNS, RING_MARGIN = (VID_LAYOUT_COLUMNS - PANEL_COLUMNS) / 2 };

/* A window's inside repeats a pattern four columns across (menu/windows.c,
 * the battle's frames), from its first column inside the border. */
enum { PATTERN_COLUMNS = 4 };

static bool in_the_ring(int s) { return s >= -RING_MARGIN && s < PANEL_COLUMNS + RING_MARGIN; }
static uint8_t *cell(PanelLayout *l, unsigned row, int s) { return &l->map.column[row % VID_LAYOUT_ROWS][(unsigned)s & (RING - 1)]; }

PanelLayout *panel_layout_begin(GameState *g, VidView view)
{
    PanelLayout *l = panel_layout(g);
    int margin = (-vid_view_left(view) + PANEL_COLUMN_DOTS - 1) / PANEL_COLUMN_DOTS;
    l->margin = (int8_t)(margin < RING_MARGIN ? margin : RING_MARGIN);
    for (unsigned row = 0; row < VID_LAYOUT_ROWS; row++)
        for (int s = -RING_MARGIN; s < PANEL_COLUMNS + RING_MARGIN; s++)
            *cell(l, row, s) = s >= 0 && s < PANEL_COLUMNS ? (uint8_t)s : VID_LAYOUT_NOTHING;
    return l;
}

void panel_show_columns(PanelLayout *l, PanelRows rows, uint8_t from, uint8_t count, int8_t at)
{
    for (unsigned row = rows.first; row < (unsigned)rows.first + rows.count; row++)
        for (unsigned i = 0; i < count && from + i < PANEL_COLUMNS; i++)
            if (in_the_ring(at + (int)i))
                *cell(l, row, at + (int)i) = (uint8_t)(from + i);
}

void panel_show_columns_as_they_lie(PanelLayout *l, PanelRows rows, uint8_t from, uint8_t count, int8_t at)
{
    panel_show_columns(l, rows, from, count, at);
    for (unsigned row = rows.first; row < (unsigned)rows.first + rows.count; row++)
        for (unsigned i = 0; i < count; i++)
            if (in_the_ring(at + (int)i))
                *cell(l, row, at + (int)i) |= VID_LAYOUT_AS_IT_LIES;
}

void panel_show_nothing(PanelLayout *l, PanelRows rows, int8_t at, uint8_t count)
{
    for (unsigned row = rows.first; row < (unsigned)rows.first + rows.count; row++)
        for (unsigned i = 0; i < count; i++)
            if (in_the_ring(at + (int)i))
                *cell(l, row, at + (int)i) = VID_LAYOUT_NOTHING;
}

void panel_stretch_window(PanelLayout *l, PanelRows rows, uint8_t left, uint8_t right, int8_t to_left, int8_t to_right)
{
    unsigned inside = (unsigned)(right - left - 1);
    unsigned round = inside < PATTERN_COLUMNS ? inside : PATTERN_COLUMNS;
    panel_show_columns(l, rows, left, 1, to_left);
    panel_show_columns(l, rows, right, 1, to_right);
    for (int s = to_left + 1; s < to_right && round; s++)
        panel_show_columns(l, rows, (uint8_t)(left + 1 + (unsigned)(s - to_left - 1) % round), 1, (int8_t)s);
}

void panel_layout_send(const PanelLayout *l, Platform *pf, uint16_t map_base)
{
    vid_lay_out_a_map_across(pf, map_base, &l->map);
}
