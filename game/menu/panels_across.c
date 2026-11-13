/* The menus: their screens laid out across a wider view.
 *
 * Every menu screen is drawn for the original's screen: three layers of tiles
 * 32 columns across -- the backdrop's pattern, the windows (a frame round a
 * patterned inside) and the text -- and sprites over them. On a wider view
 * (vid_view) each row of the screen is parted in two, at a column near its
 * middle where nothing is cut through, and the two sides move out to the
 * view's sides (core/panel_layout.h). Where a layer runs on in a pattern up to
 * the parting -- the backdrop, a window's inside, its top and bottom edges --
 * the pattern is repeated across the gap; elsewhere the gap shows nothing. So
 * a window across the middle of the screen stretches to the view's width with
 * its text kept together, the windows beside it move out with their text,
 * and the backdrop reaches the view's edges.
 *
 * Each row's parting is chosen from what the layers hold there: a tile that
 * is neither the row's most common one nor part of a pattern (a letter, a
 * window's side) is never parted from its neighbour, and a parting needs two
 * such quiet tiles on one side, so that words a single blank apart stay
 * together. Rows that run on from each other share their parting where they
 * can, so a list's columns and a window's sides stay in line.
 *
 * The layout is built from VRAM as the vblank's uploads left it, each layer's
 * rows found where the last picture showed them; the sprites move with the
 * row and side they stand in, a frame later. All of it reads the game's
 * state outside its time and keeps its own beside the game's memory. */
#include "menu/panels_across.h"

#include "charge.h"
#include "core/panel_layout.h"
#include "core/sprite_table.h"
#include "menu/menu.h"
#include "platform/video.h"
#include "vars.h"

enum { LAYERS = MENU_PANEL_LAYERS, ROW_DOTS = 8, MIDDLE = PANEL_COLUMNS / 2, PATTERN = 4, NO_PARTING = PANEL_COLUMNS };

typedef MenuLayerRow LayerRow;

/* The map's word where its row `row` starts: a map two screens high has its
 * second 32 rows a screen's words on. */
enum { SCREEN_WORDS = PANEL_COLUMNS * 32 };
static uint16_t row_start(uint16_t map, uint8_t row)
{
    return (uint16_t)(map + (row & 31) * PANEL_COLUMNS + (row & 32 ? SCREEN_WORDS : 0));
}

/* The layer's row at a line of the screen (1-224), where the last picture
 * showed it there unscrolled across. */
static void read_layer_row(const Platform *pf, unsigned layer, uint16_t line, LayerRow *r)
{
    VidBg bg;
    r->shown = vid_layer_last_shown(pf, (VidLayer)layer, line, &bg) && bg.map_w == PANEL_COLUMNS &&
               (bg.hofs & (PANEL_COLUMNS * ROW_DOTS - 1)) == 0;
    if (!r->shown)
        return;
    r->map = bg.map_base;
    r->row = (uint8_t)(((line + bg.vofs) / ROW_DOTS) & (bg.map_h - 1u));
    vid_read_vram(pf, row_start(r->map, r->row), r->cell, PANEL_COLUMNS);
}

/* ---- Where a row may be parted ------------------------------------------------------------ */

static uint16_t most_common(const uint16_t *cell)
{
    uint16_t best = cell[0];
    unsigned best_count = 0;
    for (unsigned i = 0; i < PANEL_COLUMNS; i++) {
        unsigned count = 0;
        for (unsigned j = 0; j < PANEL_COLUMNS; j++)
            count += cell[j] == cell[i];
        if (count > best_count)
            best = cell[i], best_count = count;
    }
    return best;
}

/* A tile that stands out: not the row's common one, nor the pattern's repeat. */
static uint32_t standing_out(const uint16_t *cell)
{
    uint16_t common = most_common(cell);
    uint32_t patterned = 0, out = 0;
    for (unsigned from = 0; from + 2 * PATTERN <= PANEL_COLUMNS; from++) {
        bool twice = true;
        for (unsigned i = 0; i < PATTERN; i++)
            twice &= cell[from + i] == cell[from + PATTERN + i];
        if (twice)
            patterned |= 0xFFu << from;
    }
    for (unsigned c = 0; c < PANEL_COLUMNS; c++)
        if (cell[c] != common && !(patterned >> c & 1))
            out |= 1u << c;
    return out;
}

static bool quiet(uint32_t out, int c) { return c < 0 || c >= PANEL_COLUMNS || !(out >> c & 1); }

/* The partings the layer allows, as a set of the columns a right side may
 * start at. */
static uint32_t partings(const uint16_t *cell)
{
    uint32_t out = standing_out(cell), allowed = 0;
    for (int c = 1; c < PANEL_COLUMNS; c++) {
        bool cuts_through = !quiet(out, c - 1) && !quiet(out, c);
        bool room = (quiet(out, c - 1) && quiet(out, c - 2)) || (quiet(out, c) && quiet(out, c + 1));
        if (!cuts_through && room)
            allowed |= 1u << c;
    }
    return allowed;
}

/* The allowed parting nearest the middle, the right of it first; none where
 * nothing is allowed, the whole row then moving left. */
static uint8_t nearest_the_middle(uint32_t allowed)
{
    for (int away = 0; away < MIDDLE; away++) {
        if (allowed >> (MIDDLE + away) & 1)
            return (uint8_t)(MIDDLE + away);
        if (away > 0 && allowed >> (MIDDLE - away) & 1)
            return (uint8_t)(MIDDLE - away);
    }
    return NO_PARTING;
}

/* ---- The layout --------------------------------------------------------------------------- */

/* What fills the gap at a parting, in each layer: the pattern that runs up to
 * it from the left or on from it to the right, repeated; or else the row's
 * most common tile (a text layer's blank, a window's inside). */
static bool repeats_over(const uint16_t *cell, int from)
{
    if (from < 0 || from + 2 * PATTERN > PANEL_COLUMNS)
        return false;
    for (int i = 0; i < PATTERN; i++)
        if (cell[from + i] != cell[from + PATTERN + i])
            return false;
    return true;
}

static uint8_t column_of(const uint16_t *cell, uint16_t tile)
{
    uint8_t c = 0;
    while (c < PANEL_COLUMNS - 1 && cell[c] != tile)
        c++;
    return c;
}

static void part_the_row(PanelLayout *l, const LayerRow *r, uint8_t cut, int8_t moves)
{
    PanelRows row = { r->row, 1 };
    panel_show_nothing(l, row, 0, PANEL_COLUMNS);
    panel_show_columns(l, row, 0, cut, (int8_t)-moves);
    if (cut < PANEL_COLUMNS)
        panel_show_columns(l, row, cut, (uint8_t)(PANEL_COLUMNS - cut), (int8_t)(cut + moves));
    bool from_left = repeats_over(r->cell, cut - 2 * PATTERN), from_right = !from_left && repeats_over(r->cell, cut);
    uint8_t common = column_of(r->cell, most_common(r->cell));
    for (int s = cut - moves; s < cut + moves; s++) {
        uint8_t column = common;
        if (from_left)
            column = (uint8_t)(cut - PATTERN + (s - (cut - moves)) % PATTERN);
        else if (from_right)
            column = (uint8_t)(cut + ((s - (cut + moves)) % PATTERN + PATTERN) % PATTERN);
        panel_show_columns(l, row, column, 1, (int8_t)s);
    }
}

/* A framed row: the columns outside the frame's edges move out to the view's
 * sides where the layer runs on in a four-column pattern across the edge (a
 * window's inside, the backdrop -- never text), and the gap repeats that
 * pattern. Where it doesn't, the row stays and its two outermost columns go
 * on into the margin by turns (the backdrop's two-column pattern, or a
 * blank). What lies between the edges stays where the original drew it. */
enum { FRAME_EDGE = 4, OUTERMOST = 2 };

static void frame_the_row(PanelLayout *l, const LayerRow *r, int8_t moves)
{
    PanelRows row = { r->row, 1 };
    const uint16_t *cell = r->cell;
    uint8_t left = FRAME_EDGE, right = PANEL_COLUMNS - FRAME_EDGE;
    panel_show_nothing(l, row, 0, PANEL_COLUMNS);
    panel_show_columns(l, row, 0, PANEL_COLUMNS, 0);
    if (cell[left - 1] == cell[left + PATTERN - 1]) {
        panel_show_columns(l, row, 0, left, (int8_t)-moves);
        for (int s = left - moves; s < left; s++)
            panel_show_columns(l, row, (uint8_t)(left + ((s - left) % PATTERN + PATTERN) % PATTERN), 1, (int8_t)s);
    } else {
        for (int s = -moves; s < 0; s++)
            panel_show_columns_as_they_lie(l, row, (uint8_t)(s & (OUTERMOST - 1)), 1, (int8_t)s);
    }
    if (cell[right] == cell[right - PATTERN]) {
        for (int s = right; s < right + moves; s++)
            panel_show_columns(l, row, (uint8_t)(right - PATTERN + (s - right) % PATTERN), 1, (int8_t)s);
        panel_show_columns(l, row, right, FRAME_EDGE, (int8_t)(right + moves));
    } else {
        for (int s = PANEL_COLUMNS; s < PANEL_COLUMNS + moves; s++)
            panel_show_columns_as_they_lie(l, row, (uint8_t)(PANEL_COLUMNS - OUTERMOST + (s & (OUTERMOST - 1))), 1,
                                           (int8_t)s);
    }
}

/* The map a layer shows, laid out: every row of it the last picture showed
 * parted as the screen's row it showed on was (the first such), unless an
 * earlier layer showing the same map has laid it out. */
enum { LINES = VID_ORIGINAL_VIEW_HEIGHT };

static bool shown_by_an_earlier_layer(const Platform *pf, unsigned layer, uint16_t map)
{
    for (unsigned other = 0; other < layer; other++)
        for (uint16_t line = 1; line <= LINES; line++) {
            VidBg bg;
            if (vid_layer_last_shown(pf, (VidLayer)other, line, &bg) && bg.map_base == map)
                return true;
        }
    return false;
}

static void lay_out_a_layers_map(GameState *g, Platform *pf, VidView view, unsigned layer, bool framed)
{
    const MenuPanels *p = menu_panels(g);
    LayerRow r;
    uint16_t line = 1;
    for (; line <= LINES; line++) {
        read_layer_row(pf, layer, line, &r);
        if (r.shown)
            break;
    }
    if (line > LINES)
        return;
    uint16_t map = r.map;
    if (shown_by_an_earlier_layer(pf, layer, map))
        return;
    PanelLayout *l = panel_layout_begin(g, view);
    uint32_t done[2] = { 0, 0 };
    for (unsigned each = layer; each < LAYERS; each++)
        for (line = 1; line <= LINES; line++) {
            read_layer_row(pf, each, line, &r);
            if (!r.shown || r.map != map || (done[r.row / 32] >> (r.row % 32) & 1))
                continue;
            done[r.row / 32] |= 1u << (r.row % 32);
            if (framed)
                frame_the_row(l, &r, p->moves);
            else
                part_the_row(l, &r, p->cut[(line - 1) / ROW_DOTS], p->moves);
        }
    panel_layout_send(l, pf, map);
}

/* ---- Each screen's layout ---------------------------------------------------------------- */

/* How each screen and step is laid out across a wider view, checked picture
 * by picture at 16:9, one screen at a time: parted near the middle (the
 * rule above), framed (the windows at the screen's sides and the backdrop
 * reach out to the view's sides, everything inside staying where the
 * original drew it), or kept in the middle of the view as the original
 * draws it (issue #284). */
typedef enum { KEPT_IN_THE_MIDDLE, PARTED, FRAMED } ScreenLayout;

static ScreenLayout layout_of_the_screen(const GameState *g)
{
    switch ((MenuScreen)menu_screen(g)) {
    case MENU_SCREEN_LOAD: return PARTED;
    case MENU_SCREEN_MAIN_MENU:
    case MENU_SCREEN_EQUIPMENT:
    case MENU_SCREEN_SAVE:
    case MENU_SCREEN_SAVE_POINT: return FRAMED;
    default: return KEPT_IN_THE_MIDDLE;
    }
}

/* The rows' partings for the rule: each row's, narrowed to its neighbours'
 * where they share any, so that a line of text two rows tall is parted as
 * one, and shared by runs of rows that can share one. */
static void part_the_rows(MenuPanels *p, Platform *pf)
{
    LayerRow (*rows)[LAYERS] = p->rows;
    uint32_t own[MENU_PANEL_ROWS], allowed[MENU_PANEL_ROWS];
    for (unsigned y = 0; y < MENU_PANEL_ROWS; y++) {
        own[y] = ~1u;
        for (unsigned layer = 0; layer < LAYERS; layer++) {
            read_layer_row(pf, layer, (uint16_t)(y * ROW_DOTS + ROW_DOTS / 2), &rows[y][layer]);
            if (rows[y][layer].shown)
                own[y] &= partings(rows[y][layer].cell);
        }
    }
    for (unsigned y = 0; y < MENU_PANEL_ROWS; y++) {
        uint32_t near = own[y];
        if (y > 0)
            near &= own[y - 1];
        if (y + 1 < MENU_PANEL_ROWS)
            near &= own[y + 1];
        allowed[y] = near ? near : own[y];
    }
    uint32_t together = allowed[0];
    unsigned run_start = 0;
    for (unsigned y = 1; y <= MENU_PANEL_ROWS; y++) {
        if (y < MENU_PANEL_ROWS && (together & allowed[y])) {
            together &= allowed[y];
            continue;
        }
        for (unsigned k = run_start; k < y; k++)
            p->cut[k] = nearest_the_middle(together);
        if (y < MENU_PANEL_ROWS)
            run_start = y, together = allowed[y];
    }
}

void menu_lay_out_the_screen_across(GameState *g, Platform *pf)
{
    MenuPanels *p = menu_panels(g);
    VidView view = vid_view(pf);
    uint32_t mark = charge_uncounted_begin(g);
    ScreenLayout layout = vid_view_is_wider(view) ? layout_of_the_screen(g) : KEPT_IN_THE_MIDDLE;
    charge_uncounted_end(g, mark);
    bool parted = layout == PARTED;
    if (p->laid_out && !parted)
        sprite_table_forget_places(g);
    p->laid_out = parted;
    if (layout == KEPT_IN_THE_MIDDLE)
        return;
    PanelLayout *l = panel_layout_begin(g, view);
    VidWindowParting parting = { 0 };
    if (parted) {
        part_the_rows(p, pf);
        /* Each side moves out an even number of columns, so a pattern four
         * columns round runs on across the gap into the right side. */
        p->moves = (int8_t)((l->margin + 1) & ~1);
        for (unsigned y = 0; y < MENU_PANEL_ROWS && y < VID_PARTING_ROWS; y++)
            parting.left_side[y] = parting.right_side[y] = (uint16_t)(p->cut[y] * ROW_DOTS);
    } else {
        p->moves = l->margin;
        for (unsigned y = 0; y < VID_PARTING_ROWS; y++) {
            parting.left_side[y] = FRAME_EDGE * ROW_DOTS;
            parting.right_side[y] = (PANEL_COLUMNS - FRAME_EDGE) * ROW_DOTS;
        }
    }
    parting.moves = (int16_t)(p->moves * ROW_DOTS);
    vid_part_the_windows(pf, &parting);
    for (unsigned layer = 0; layer < LAYERS; layer++)
        lay_out_a_layers_map(g, pf, view, layer, !parted);
}

/* ---- The sprites -------------------------------------------------------------------------- */

enum { SPRITE_MIDDLE = 8 };

void menu_move_the_sprites_across(GameState *g)
{
    const MenuPanels *p = menu_panels(g);
    if (!p->laid_out)
        return;
    for (unsigned sprite = 0; sprite < OAM_BUFFER_COUNT; sprite++) {
        uint32_t mark = charge_uncounted_begin(g);
        uint8_t y = oam_buffer_y(g, sprite);
        uint8_t high = (uint8_t)(oam_high_table_byte(g, sprite / VID_OAM_SPRITES_A_HIGH_BYTE) >>
                                 (VID_OAM_HIGH_BITS * (sprite % VID_OAM_SPRITES_A_HIGH_BYTE)));
        int16_t x = vid_oam_x(oam_buffer_x(g, sprite), high);
        charge_uncounted_end(g, mark);
        if (y >= VID_OAM_HIDDEN_Y)
            continue;
        unsigned row = (unsigned)(y + SPRITE_MIDDLE) / ROW_DOTS;
        if (row >= MENU_PANEL_ROWS)
            row = MENU_PANEL_ROWS - 1;
        int side = x + SPRITE_MIDDLE < p->cut[row] * ROW_DOTS ? -1 : 1;
        sprite_table_move_as_it_stands(g, sprite, (int16_t)(side * p->moves * ROW_DOTS));
    }
}

void menu_panels_closed(GameState *g)
{
    menu_panels(g)->laid_out = false;
    sprite_table_forget_places(g);
}
