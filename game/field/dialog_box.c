/* Field mode: the dialog window laid out again for a smaller view.
 *
 * The original's window is the screen's width, 256 dots, and its lines of
 * text reach 240 of them. A 240-dot view shows it with both its borders by
 * laying it out again: the frame two columns narrower, and each line that
 * no longer fits broken again between its words onto the next line. A page
 * so broken can need more than the window's four lines. While the page is
 * typed the window keeps four, the newest at the foot (the lines move up as
 * a new one starts); once it is all typed and waits for the player, the
 * window grows to show every line of it.
 *
 * Nothing of the game changes: it types the page into its own tiles in VRAM
 * as at the original's view, a glyph a frame, and waits for the same
 * buttons. Each frame this reads what the game drew -- the text area's map
 * and tiles, and the frame's map -- outside the game's time, and draws the
 * window afresh in the panel's own memory (vid_send_panel_vram), which only
 * the view shows (vid_draw_a_panel_in_view). It keeps nothing between frames.
 *
 * The lines are broken by what is drawn, not by the string: a word ends
 * where the line's ink leaves a gap a space's width or wider, so a name or
 * an item put into the text breaks as its glyphs do. */
#include "field/dialog_box.h"

#include "field/dialog_window.h"
#include "field/field.h"
#include "field/line_effects.h"
#include "platform/video.h"
#include "vars.h"

/* ---- The page as the game drew it -------------------------------------------------------------- */

/* A line of text is two rows of the text area's map, 32 tiles of 2bpp
 * across, 16 lines down. */
enum {
    COLUMNS = 32, TILE_DOTS = 8, LINE_DOTS = COLUMNS * TILE_DOTS, LINE_LINES = 16,
    TILE_WORDS_2BPP = 8, TILE_BYTES_2BPP = 16, TILE_NUMBER = 0x03FF, ENTRY_LOOKS = 0xFC00,
    ENTRY_FLIPPED_ACROSS = 0x4000,
};

/* Where the game's text starts on a line, and how far it may reach in a
 * window two columns narrower: the original's 8 dots short of its right
 * border, which is 16 dots further in. */
enum { MARGIN = 8, INDENT = 0x14, NARROWER = 2 * TILE_DOTS, REACH = LINE_DOTS - 2 * TILE_DOTS - NARROWER };
/* A gap in a line's ink this wide or wider is a space between words: a
 * space's 4 dots, where the glyphs' own gaps are narrower. */
enum { WORD_GAP = 4 };

/* A tile of the text area as the game left it, the last one read kept. */
typedef struct {
    uint16_t chars;          /* BG3's tiles */
    uint16_t entry;
    bool read;
    uint8_t bytes[TILE_BYTES_2BPP];
} TileReader;

static uint16_t map_entry(const Platform *pf, unsigned row, unsigned column)
{
    uint16_t w = 0;
    vid_read_vram(pf, (uint16_t)(DIALOG_TEXT_AREA_MAP + row * COLUMNS + column), &w, 1);
    return w;
}

/* The colour (0-3) the game's text area holds at a dot of a line. */
static uint8_t text_dot(const Platform *pf, TileReader *t, unsigned line, unsigned x, unsigned y)
{
    uint16_t entry = map_entry(pf, 2 * line + y / TILE_DOTS, x / TILE_DOTS);
    if (!t->read || entry != t->entry) {
        uint16_t words[TILE_WORDS_2BPP];
        vid_read_vram(pf, (uint16_t)(t->chars + (entry & TILE_NUMBER) * TILE_WORDS_2BPP), words, TILE_WORDS_2BPP);
        for (unsigned i = 0; i < TILE_WORDS_2BPP; i++) {
            t->bytes[2 * i] = (uint8_t)words[i];
            t->bytes[2 * i + 1] = (uint8_t)(words[i] >> 8);
        }
        t->entry = entry;
        t->read = true;
    }
    unsigned across = x % TILE_DOTS, row = y % TILE_DOTS;
    unsigned bit = (entry & ENTRY_FLIPPED_ACROSS) ? across : 7 - across;
    return (uint8_t)(((t->bytes[2 * row] >> bit) & 1) | (((t->bytes[2 * row + 1] >> bit) & 1) << 1));
}

/* Which of a line's columns of tiles hold ink, a byte a column (bit 7 its
 * first dot), from both rows' tiles. */
static void line_ink(const Platform *pf, TileReader *t, unsigned line, uint8_t ink[COLUMNS])
{
    for (unsigned column = 0; column < COLUMNS; column++) {
        uint8_t any = 0;
        for (unsigned half = 0; half < 2; half++) {
            uint16_t entry = map_entry(pf, 2 * line + half, column);
            uint16_t words[TILE_WORDS_2BPP];
            vid_read_vram(pf, (uint16_t)(t->chars + (entry & TILE_NUMBER) * TILE_WORDS_2BPP), words, TILE_WORDS_2BPP);
            uint8_t bits = 0;
            for (unsigned i = 0; i < TILE_WORDS_2BPP; i++)
                bits |= (uint8_t)(words[i] | words[i] >> 8);
            if (entry & ENTRY_FLIPPED_ACROSS) {
                uint8_t r = 0;
                for (unsigned b = 0; b < 8; b++)
                    r |= (uint8_t)(((bits >> b) & 1) << (7 - b));
                bits = r;
            }
            any |= bits;
        }
        ink[column] = any;
    }
}

static bool inked(const uint8_t ink[COLUMNS], int x) { return (ink[x / TILE_DOTS] >> (7 - x % TILE_DOTS)) & 1; }

/* ---- The page set again -------------------------------------------------------------------------- */

/* A line of the window as laid out again: pieces of the game's lines, each
 * its dots from `from`, `dots` of them, put `to` across the window. */
enum { PIECES = 3, MOST_LINES = 12 };
typedef struct {
    uint8_t line, from, dots, to;
} Piece;
typedef struct {
    uint8_t count;
    Piece piece[PIECES];
} WindowLine;

typedef struct {
    uint8_t count;
    WindowLine line[MOST_LINES];
} Page;

/* The game's lines as drawn: each one's ink, and where its ink starts and ends. */
typedef struct {
    uint8_t ink[DIALOG_TEXT_LINES][COLUMNS];
    int start[DIALOG_TEXT_LINES], end[DIALOG_TEXT_LINES];
    bool has[DIALOG_TEXT_LINES];
    int last;   /* the last with ink, -1 for none */
} Drawn;

/* Where the line's ink starts and ends, false where it has none. */
static bool ink_span(const uint8_t ink[COLUMNS], int *start, int *end)
{
    *start = 0;
    while (*start < LINE_DOTS && !inked(ink, *start))
        (*start)++;
    if (*start == LINE_DOTS)
        return false;
    *end = LINE_DOTS;
    while (!inked(ink, *end - 1))
        (*end)--;
    return true;
}

/* Where the word from `x` ends (its last inked dot's next), and where the
 * next one starts: past a gap of WORD_GAP or more, or `end`. */
static int word_end(const uint8_t ink[COLUMNS], int x, int end, int *next)
{
    while (x < end) {
        while (x < end && inked(ink, x))
            x++;
        int gap = x;
        while (x < end && !inked(ink, x))
            x++;
        if (x - gap >= WORD_GAP || x == end) {
            *next = x;
            return gap;
        }
    }
    *next = end;
    return end;
}

static WindowLine *new_line(Page *p)
{
    if (p->count == MOST_LINES)
        return NULL;
    WindowLine *l = &p->line[p->count++];
    l->count = 0;
    return l;
}

/* A word set at `to`: onto the line's last piece where it goes on from it in
 * the game's line as it does here, otherwise a piece of its own. */
static bool set_word(WindowLine *l, uint8_t line, int from, int dots, int to)
{
    Piece *last = l->count ? &l->piece[l->count - 1] : NULL;
    if (last && last->line == line && last->from + (to - last->to) == from) {
        last->dots = (uint8_t)(from + dots - last->from);
        return true;
    }
    if (l->count == PIECES)
        return false;
    l->piece[l->count++] = (Piece){ line, (uint8_t)from, (uint8_t)dots, (uint8_t)to };
    return true;
}

/* The space between a word that ends one of the game's lines and the first
 * of the next, joined on one line: a space glyph's 4 dots and a glyph's gap. */
enum { SPACE_BETWEEN_LINES = 5 };

/* The game's lines `first` to `last`, one paragraph: as they are, where all
 * fit; otherwise their words set again from the first's start, each going
 * on the next line where it would pass the window's reach, a line going on
 * at the indent of the paragraph's later lines. A word too wide for a line
 * of its own is cut at the reach. */
static void set_the_paragraph(Page *p, const Drawn *d, int first, int last)
{
    bool fits = true;
    for (int line = first; line <= last; line++)
        fits = fits && d->end[line] <= REACH;
    if (fits) {
        for (int line = first; line <= last; line++) {
            WindowLine *l = new_line(p);
            if (l)
                set_word(l, (uint8_t)line, d->start[line], d->end[line] - d->start[line], d->start[line]);
        }
        return;
    }
    int indent = last > first ? d->start[first + 1] : INDENT;
    WindowLine *l = new_line(p);
    int x = d->start[first];
    for (int line = first; line <= last && l; line++) {
        int from = d->start[line], previous_end = from, next;
        bool first_of_line = true;
        while (from < d->end[line] && l) {
            int end = word_end(d->ink[line], from, d->end[line], &next);
            int before = first_of_line ? SPACE_BETWEEN_LINES : from - previous_end;
            int to = l->count ? x + before : x;
            if (l->count && to + (end - from) > REACH) {
                l = new_line(p);
                to = indent;
            }
            int dots = end - from > REACH - to ? REACH - to : end - from;
            if (l && !set_word(l, (uint8_t)line, from, dots, to)) {
                l = new_line(p);
                to = indent;
                if (l)
                    set_word(l, (uint8_t)line, from, dots, to);
            }
            x = to + dots;
            previous_end = end;
            from = next;
            first_of_line = false;
        }
    }
}

/* The page as the game has typed it so far, set again for the window: the
 * game's lines after the last with ink are not typed yet. A line indented
 * as far as the one starting its paragraph, or further, and at the indent,
 * goes on that paragraph (a speaker's later lines); where `may_join` is false
 * (a question's choices, each a line of its own) every line is a paragraph
 * of its own. */
static void set_the_page(const Platform *pf, TileReader *t, bool may_join, Page *p)
{
    Drawn d;
    d.last = -1;
    for (unsigned line = 0; line < DIALOG_TEXT_LINES; line++) {
        line_ink(pf, t, line, d.ink[line]);
        d.has[line] = ink_span(d.ink[line], &d.start[line], &d.end[line]);
        if (d.has[line])
            d.last = (int)line;
    }
    p->count = 0;
    for (int line = 0; line <= d.last;) {
        if (!d.has[line]) {
            new_line(p);
            line++;
            continue;
        }
        int last = line;
        while (may_join && last < d.last && d.has[last + 1] && d.start[last + 1] >= INDENT - 1 &&
               d.start[last + 1] >= d.start[line])
            last++;
        set_the_paragraph(p, &d, line, last);
        line = last + 1;
    }
}

/* ---- The window drawn afresh -------------------------------------------------------------------- */

/* The window's own memory: its frame's map (32 by up to 2 + 2 * MOST_LINES
 * rows, over the text area's and the frame's own maps, which the window
 * reads from VRAM as the game left it), its text's tiles from where the
 * game's own are, and its text's map past them. */
enum { OWN_FRAME_MAP = DIALOG_TEXT_AREA_MAP, OWN_TEXT_TILES = 0x100, OWN_TEXT_MAP = 0x7000, TILES_A_LINE = 0x40 };
/* The frame: a border row, then rows of its pattern, eight of them repeating;
 * the text starts 6 lines down, inside the top border. */
enum { BORDER_ROWS = 1, PATTERN_ROWS = DIALOG_FRAME_ROWS - 2, TEXT_DOWN = 6 };
/* The window's columns: the frame's first and last as its borders, the 28
 * after the first between them. */
enum { WINDOW_COLUMNS = COLUMNS - NARROWER / TILE_DOTS };

static uint16_t frame_entry(const Platform *pf, unsigned row, unsigned column)
{
    uint16_t w = 0;
    vid_read_vram(pf, (uint16_t)(DIALOG_FRAME_MAP + row * COLUMNS + column), &w, 1);
    return w;
}

/* The frame for `lines` lines of text, two map rows a line between its borders. */
static void send_the_frame(Platform *pf, unsigned lines)
{
    unsigned rows = 2 * BORDER_ROWS + 2 * lines;
    for (unsigned row = 0; row < rows; row++) {
        unsigned from = row == 0 ? 0 : row == rows - 1 ? DIALOG_FRAME_ROWS - 1 : 1 + (row - 1) % PATTERN_ROWS;
        uint16_t entries[COLUMNS] = { 0 };
        for (unsigned column = 0; column < WINDOW_COLUMNS; column++)
            entries[column] = frame_entry(pf, from, column == WINDOW_COLUMNS - 1 ? COLUMNS - 1 : column);
        vid_send_panel_vram(pf, (uint16_t)(OWN_FRAME_MAP + row * COLUMNS), (PfData){ .ptr = (const uint8_t *)entries },
                            sizeof entries);
    }
}

/* The tile number of a line's column and row of tiles, as the game numbers its own. */
static uint16_t line_tile(unsigned line, unsigned column, unsigned half)
{
    return (uint16_t)(line * TILES_A_LINE + (column / 16) * 0x20 + half * 0x10 + column % 16);
}

/* A window line's tiles, its pieces' dots where they go and nothing elsewhere. */
static void send_the_line(Platform *pf, TileReader *t, const WindowLine *l, unsigned at, uint16_t chars)
{
    for (unsigned column = 0; column < COLUMNS; column++)
        for (unsigned half = 0; half < 2; half++) {
            uint8_t bytes[TILE_BYTES_2BPP] = { 0 };
            for (unsigned k = 0; l && k < l->count; k++) {
                const Piece *piece = &l->piece[k];
                for (unsigned across = 0; across < TILE_DOTS; across++) {
                    int x = (int)(column * TILE_DOTS + across) - piece->to;
                    if (x < 0 || x >= piece->dots)
                        continue;
                    for (unsigned row = 0; row < TILE_DOTS; row++) {
                        uint8_t dot = text_dot(pf, t, piece->line, (unsigned)(piece->from + x), half * TILE_DOTS + row);
                        bytes[2 * row] |= (uint8_t)((dot & 1) << (7 - across));
                        bytes[2 * row + 1] |= (uint8_t)((dot >> 1) << (7 - across));
                    }
                }
            }
            uint16_t tile = (uint16_t)(OWN_TEXT_TILES + line_tile(at, column, half));
            vid_send_panel_vram(pf, (uint16_t)(chars + tile * TILE_WORDS_2BPP), (PfData){ .ptr = bytes }, sizeof bytes);
        }
}

/* The text's map for `lines` lines, as the game's own names its tiles, with
 * the looks (palette and priority) of the game's; its other rows, which the
 * window's borders show over, a blank tile. */
enum { MAP_ROWS = 32, BLANK_TILE = TILE_NUMBER };

static void send_the_text_map(Platform *pf, unsigned lines, uint16_t chars)
{
    uint16_t looks = (uint16_t)(map_entry(pf, 0, 0) & ENTRY_LOOKS);
    static const uint8_t blank[TILE_BYTES_2BPP];
    vid_send_panel_vram(pf, (uint16_t)(chars + BLANK_TILE * TILE_WORDS_2BPP), (PfData){ .ptr = blank }, sizeof blank);
    for (unsigned row = 0; row < MAP_ROWS; row++) {
        uint16_t entries[COLUMNS];
        for (unsigned column = 0; column < COLUMNS; column++)
            entries[column] = (uint16_t)(looks | (row < 2 * lines ? OWN_TEXT_TILES + line_tile(row / 2, column, row % 2)
                                                                    : BLANK_TILE));
        vid_send_panel_vram(pf, (uint16_t)(OWN_TEXT_MAP + row * COLUMNS), (PfData){ .ptr = (const uint8_t *)entries },
                            sizeof entries);
    }
}

/* ---- The window in the view --------------------------------------------------------------------- */

/* How many lines the window shows, from which: four while the page is typed
 * (the newest at the foot), every line once it waits for the player, as
 * many as the view has room for. */
static unsigned lines_shown(const GameState *g, const Page *p, VidView view, unsigned *first)
{
    unsigned most = ((unsigned)view.height - 2 * BORDER_ROWS * TILE_DOTS) / LINE_LINES;
    unsigned lines = DIALOG_TEXT_LINES;
    if (dialog_state(g) == DIALOG_IS_READ && p->count > lines)
        lines = p->count < most ? p->count : most;
    *first = p->count > lines ? p->count - lines : 0;
    return lines;
}

/* Where the view can't draw a window afresh, the screen's band of it kept in
 * view instead, its middle across, opening from its middle as on the screen. */
static ViewLines keep_the_screens_window(Platform *pf, VidView view, WindowPosition position, FieldWindowBand band)
{
    int16_t middle = position == WINDOW_AT_TOP ? (int16_t)band.full : (int16_t)(view.height - band.full);
    view_keep_lines(pf, (int16_t)(band.middle - band.half), (uint16_t)(2 * band.half), (int16_t)(middle - band.half));
    uint16_t open = (uint16_t)(view.height - 2 * band.full);
    int16_t clear_of = (int16_t)(band.middle - band.full), clear_to = (int16_t)(band.middle + band.full);
    return position == WINDOW_AT_TOP ? (ViewLines){ (int16_t)(2 * band.full), open, clear_of, clear_to }
                                     : (ViewLines){ 0, open, clear_of, clear_to };
}

ViewLines field_lay_out_the_dialog_box(const GameState *g, Platform *pf, VidView view)
{
    WindowPosition position = dialog_window_at(g);
    if (position != WINDOW_AT_TOP && position != WINDOW_AT_BOTTOM)
        return view_all_lines(view);
    FieldWindowBand band = field_dialog_window_band(g, (uint8_t)position);
    if (!vid_view_draws_panels(pf))
        return keep_the_screens_window(pf, view, position, band);
    VidBg frame_layer, text_layer;
    uint16_t middle_line = (uint16_t)(band.middle + 1);
    vid_layer_last_shown(pf, VID_BG1, middle_line, &frame_layer);
    vid_layer_last_shown(pf, VID_BG3, middle_line, &text_layer);
    TileReader t = { text_layer.char_base, 0, false, { 0 } };
    Page page;
    set_the_page(pf, &t, !field_choice_pointer_shown(g), &page);
    unsigned first, lines = lines_shown(g, &page, view, &first);

    send_the_frame(pf, lines);
    send_the_text_map(pf, lines, text_layer.char_base);
    for (unsigned k = 0; k < lines; k++)
        send_the_line(pf, &t, first + k < page.count ? &page.line[first + k] : NULL, k, text_layer.char_base);

    /* The window opens and shuts from its middle, as far as the screen's
     * band has; it stands at the view's top or foot. */
    int height = (int)(2 * BORDER_ROWS * TILE_DOTS + lines * LINE_LINES);
    int shown = band.full ? height * band.half / band.full : height;
    int top = height / 2 - shown / 2;
    int at = position == WINDOW_AT_TOP ? 0 : (int)view.height - height;
    VidDrawnPanel panel = { (int16_t)(at + top), (uint16_t)shown, (int16_t)(band.middle - band.half),
                            (uint16_t)(2 * band.half), VID_LAYER_BIT(VID_BG1) | VID_LAYER_BIT(VID_BG3), { { 0 } } };
    panel.bg[VID_BG1] = (VidBg){ OWN_FRAME_MAP, 32, 32, frame_layer.char_base, 0, (uint16_t)top, false, false };
    panel.bg[VID_BG3] = (VidBg){ OWN_TEXT_MAP, 32, 32, text_layer.char_base, 0, (uint16_t)(top - TEXT_DOWN), false, false };
    if (shown > 0)
        vid_draw_a_panel_in_view(pf, &panel);
    uint16_t open = (uint16_t)((int)view.height - height);
    int16_t clear_of = (int16_t)(band.middle - band.full), clear_to = (int16_t)(band.middle + band.full);
    return position == WINDOW_AT_TOP ? (ViewLines){ (int16_t)height, open, clear_of, clear_to }
                                     : (ViewLines){ 0, open, clear_of, clear_to };
}
