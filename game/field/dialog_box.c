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
 * buttons. This reads what the game drew -- the text area's map and tiles,
 * and the frame's map -- outside the game's time, and draws the window in
 * the panel's own memory (vid_send_panel_vram), which only the view shows
 * (vid_draw_a_panel_in_view). What it last read and sent it keeps in the
 * view's memory (vid_view_memory), so a frame reads only the line the game
 * changed and sends only what changed in the window (below, "What the
 * window last sent").
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

#include <string.h>

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

/* The text area as the game left it: the map's two rows of the last line
 * read, and the last two tiles read, each as it shows: one the map turns
 * round across is kept turned round, so a row's first dot is always bit 7
 * of its two bitplanes' bytes. */
typedef struct {
    uint16_t entry;
    bool read;
    uint8_t bytes[TILE_BYTES_2BPP];   /* each row's two bitplanes, row after row */
} TextTile;
enum { NO_LINE = 0xFF };
typedef struct {
    uint16_t chars;          /* BG3's tiles */
    uint8_t older;           /* the tile read over next */
    uint8_t line;            /* the line whose map rows are kept, or NO_LINE */
    TextTile tile[2];
    uint16_t entries[2][COLUMNS];
} TileReader;

static uint16_t map_entry(const Platform *pf, unsigned row, unsigned column)
{
    uint16_t w = 0;
    vid_read_vram(pf, (uint16_t)(DIALOG_TEXT_AREA_MAP + row * COLUMNS + column), &w, 1);
    return w;
}

static uint8_t turned_round(uint8_t dots)
{
    uint8_t r = 0;
    for (unsigned b = 0; b < 8; b++)
        r |= (uint8_t)(((dots >> b) & 1) << (7 - b));
    return r;
}

/* The tile at a column of one of the text area's map rows, as it shows. */
static const uint8_t *text_tile(const Platform *pf, TileReader *t, unsigned row, unsigned column)
{
    if (t->line != row / 2) {
        t->line = (uint8_t)(row / 2);
        vid_read_vram(pf, (uint16_t)(DIALOG_TEXT_AREA_MAP + t->line * 2 * COLUMNS), t->entries[0], 2 * COLUMNS);
    }
    uint16_t entry = t->entries[row % 2][column];
    for (unsigned k = 0; k < 2; k++)
        if (t->tile[k].read && t->tile[k].entry == entry) {
            t->older = (uint8_t)(1 - k);
            return t->tile[k].bytes;
        }
    TextTile *tile = &t->tile[t->older];
    t->older = (uint8_t)(1 - t->older);
    uint16_t words[TILE_WORDS_2BPP];
    vid_read_vram(pf, (uint16_t)(t->chars + (entry & TILE_NUMBER) * TILE_WORDS_2BPP), words, TILE_WORDS_2BPP);
    for (unsigned i = 0; i < TILE_WORDS_2BPP; i++) {
        uint8_t low = (uint8_t)words[i], high = (uint8_t)(words[i] >> 8);
        tile->bytes[2 * i] = entry & ENTRY_FLIPPED_ACROSS ? turned_round(low) : low;
        tile->bytes[2 * i + 1] = entry & ENTRY_FLIPPED_ACROSS ? turned_round(high) : high;
    }
    tile->entry = entry;
    tile->read = true;
    return tile->bytes;
}

/* Which of a line's columns of tiles hold ink, a byte a column (bit 7 its
 * first dot), from both rows' tiles; and a sum of each column's dots, which
 * changes where they do. */
static void line_ink(const Platform *pf, TileReader *t, unsigned line, uint8_t ink[COLUMNS], uint16_t sums[COLUMNS])
{
    for (unsigned column = 0; column < COLUMNS; column++) {
        uint8_t any = 0;
        uint16_t sum = 0;
        for (unsigned half = 0; half < 2; half++) {
            const uint8_t *bytes = text_tile(pf, t, 2 * line + half, column);
            for (unsigned i = 0; i < TILE_BYTES_2BPP; i++) {
                any |= bytes[i];
                sum = (uint16_t)((sum << 3 | sum >> 13) ^ bytes[i]);
            }
        }
        ink[column] = any;
        sums[column] = sum;
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
    const uint8_t (*ink)[COLUMNS];   /* DIALOG_TEXT_LINES of them */
    int start[DIALOG_TEXT_LINES], end[DIALOG_TEXT_LINES];
    bool has[DIALOG_TEXT_LINES];
    int last;   /* the last with ink, -1 for none */
} Drawn;

/* Where the line's ink starts and ends, false where it has none: the blank
 * columns at either side passed over a column at a time. */
static bool ink_span(const uint8_t ink[COLUMNS], int *start, int *end)
{
    unsigned first = 0, last = COLUMNS;
    while (first < COLUMNS && !ink[first])
        first++;
    if (first == COLUMNS)
        return false;
    while (!ink[last - 1])
        last--;
    *start = (int)(first * TILE_DOTS);
    while (!inked(ink, *start))
        (*start)++;
    *end = (int)(last * TILE_DOTS);
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

/* The page as the game has typed it so far (each line's ink), set again for
 * the window: the game's lines after the last with ink are not typed yet. A
 * line indented as far as the one starting its paragraph, or further, and
 * at the indent, goes on that paragraph (a speaker's later lines); where
 * `may_join` is false (a question's choices, each a line of its own) every
 * line is a paragraph of its own. */
static void set_the_page(const uint8_t ink[DIALOG_TEXT_LINES][COLUMNS], bool may_join, Page *p)
{
    Drawn d;
    d.ink = ink;
    d.last = -1;
    for (unsigned line = 0; line < DIALOG_TEXT_LINES; line++) {
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
 * game's own are past the text area's -- a blank one, then each line's, the
 * window's columns of them -- and its text's map past them. It is sent as a
 * host that takes the sends as they come needs it (vid_send_panel_vram):
 * the panel first, then the tiles, then the maps. */
enum { OWN_FRAME_MAP = DIALOG_TEXT_AREA_MAP, OWN_TEXT_TILES = 0x100, OWN_TEXT_MAP = 0x7000 };
/* The frame: a border row, then rows of its pattern, eight of them repeating;
 * the text starts 6 lines down, inside the top border. */
enum { BORDER_ROWS = 1, PATTERN_ROWS = DIALOG_FRAME_ROWS - 2, TEXT_DOWN = 6 };
/* The window's columns: the frame's first and last as its borders, the 28
 * after the first between them. */
enum { WINDOW_COLUMNS = COLUMNS - NARROWER / TILE_DOTS };
enum { BLANK_TILE = OWN_TEXT_TILES, OWN_LINE_TILES = OWN_TEXT_TILES + 1, TILES_A_LINE = 2 * WINDOW_COLUMNS };

/* The frame for `lines` lines of text, two map rows a line between its borders. */
static void send_the_frame(Platform *pf, unsigned lines)
{
    unsigned rows = 2 * BORDER_ROWS + 2 * lines;
    for (unsigned row = 0; row < rows; row++) {
        unsigned from = row == 0 ? 0 : row == rows - 1 ? DIALOG_FRAME_ROWS - 1 : 1 + (row - 1) % PATTERN_ROWS;
        uint16_t frame[COLUMNS], entries[COLUMNS] = { 0 };
        vid_read_vram(pf, (uint16_t)(DIALOG_FRAME_MAP + from * COLUMNS), frame, COLUMNS);
        for (unsigned column = 0; column < WINDOW_COLUMNS; column++)
            entries[column] = frame[column == WINDOW_COLUMNS - 1 ? COLUMNS - 1 : column];
        vid_send_panel_vram(pf, (uint16_t)(OWN_FRAME_MAP + row * COLUMNS), (PfData){ .ptr = (const uint8_t *)entries },
                            sizeof entries);
    }
}

/* The tile of a window line's column and row of tiles. */
static uint16_t line_tile(unsigned line, unsigned column, unsigned half)
{
    return (uint16_t)(OWN_LINE_TILES + line * TILES_A_LINE + half * WINDOW_COLUMNS + column);
}

/* The dots of a piece that fall in one of a window line's tiles, put in its
 * rows: each row's eight from the game's line where the piece starts them,
 * from the one or two of its tiles they lie in. */
static void put_the_piece(const Platform *pf, TileReader *t, const Piece *piece, unsigned column, unsigned half,
                          uint8_t bytes[TILE_BYTES_2BPP])
{
    int x = (int)(column * TILE_DOTS) - piece->to;   /* the tile's first dot, in the piece */
    int first = x < 0 ? 0 : x, end = x + TILE_DOTS < piece->dots ? x + TILE_DOTS : piece->dots;
    if (first >= end)
        return;
    unsigned from = piece->from + (unsigned)first, into = (unsigned)(first - x), shift = from % TILE_DOTS;
    uint8_t mask = (uint8_t)((uint8_t)(0xFF00u >> (end - first)) >> into);
    unsigned row = 2 * piece->line + half;
    const uint8_t *left = text_tile(pf, t, row, from / TILE_DOTS);
    const uint8_t *right = shift && from / TILE_DOTS + 1 < COLUMNS ? text_tile(pf, t, row, from / TILE_DOTS + 1) : NULL;
    for (unsigned i = 0; i < TILE_BYTES_2BPP; i++) {
        unsigned dots = (unsigned)left[i] << 8 | (right ? right[i] : 0);
        bytes[i] |= (uint8_t)((uint8_t)(dots << shift >> 8) >> into) & mask;
    }
}

/* Whether a window line's pieces reach into a column: the others are blank. */
static bool line_reaches(const WindowLine *l, unsigned column)
{
    for (unsigned k = 0; l && k < l->count; k++)
        if (l->piece[k].to < (column + 1) * TILE_DOTS && l->piece[k].to + l->piece[k].dots > column * TILE_DOTS)
            return true;
    return false;
}

/* A window line's tiles at a column, its pieces' dots where they go. */
static void send_the_tiles(Platform *pf, TileReader *t, const WindowLine *l, unsigned at, unsigned column, uint16_t chars)
{
    for (unsigned half = 0; half < 2; half++) {
        uint8_t bytes[TILE_BYTES_2BPP] = { 0 };
        for (unsigned k = 0; k < l->count; k++)
            put_the_piece(pf, t, &l->piece[k], column, half, bytes);
        uint16_t tile = line_tile(at, column, half);
        vid_send_panel_vram(pf, (uint16_t)(chars + tile * TILE_WORDS_2BPP), (PfData){ .ptr = bytes }, sizeof bytes);
    }
}

/* A window line's tiles where its pieces reach. */
static void send_the_line(Platform *pf, TileReader *t, const WindowLine *l, unsigned at, uint16_t chars)
{
    for (unsigned column = 0; column < WINDOW_COLUMNS; column++)
        if (line_reaches(l, column))
            send_the_tiles(pf, t, l, at, column, chars);
}

static void send_the_blank_tile(Platform *pf, uint16_t chars)
{
    static const uint8_t blank[TILE_BYTES_2BPP];
    vid_send_panel_vram(pf, (uint16_t)(chars + BLANK_TILE * TILE_WORDS_2BPP), (PfData){ .ptr = blank }, sizeof blank);
}

/* A row of the text's map, with the looks (palette and priority) of the
 * game's: the window line's tiles where it reaches, the blank tile
 * elsewhere (`l` NULL: everywhere). */
enum { MAP_ROWS = 32 };

static void send_the_map_row(Platform *pf, const WindowLine *l, unsigned row, uint16_t looks)
{
    uint16_t entries[COLUMNS];
    for (unsigned column = 0; column < COLUMNS; column++)
        entries[column] = (uint16_t)(looks | (column < WINDOW_COLUMNS && line_reaches(l, column)
                                                  ? line_tile(row / 2, column, row % 2) : BLANK_TILE));
    vid_send_panel_vram(pf, (uint16_t)(OWN_TEXT_MAP + row * COLUMNS), (PfData){ .ptr = (const uint8_t *)entries },
                        sizeof entries);
}

static const WindowLine *window_line(const Page *page, unsigned first, unsigned k)
{
    return first + k < page->count ? &page->line[first + k] : NULL;
}

/* The text's map for `lines` lines from the page's `first`. Only the rows
 * the window shows are sent: its lines' and the two below them and the one
 * above, which its borders show over. */
static void send_the_text_map(Platform *pf, const Page *page, unsigned first, unsigned lines, uint16_t looks)
{
    for (unsigned row = 0; row < MAP_ROWS; row++)
        if (row <= 2 * lines + 1 || row == MAP_ROWS - 1)
            send_the_map_row(pf, row < 2 * lines ? window_line(page, first, row / 2) : NULL, row, looks);
}

/* ---- What the window last sent ---------------------------------------------------------------- *
 *
 * The panel's memory stays from frame to frame, so a frame sends only what
 * changed, against what the view's memory keeps (vid_view_memory): what the
 * window was drawn from (the game's string and choice, its place, the
 * layers' tiles), each of the game's lines' ink and sums, and the page as it
 * was set. The game changes its text area a line at a time, as the vblank
 * sends a text row (dialog_text_sending): only the line it sends this frame,
 * and the one it sent the last (which it may have sent after the window was
 * laid out), are read again. A column of a window line's tiles is sent
 * again where the pieces in it, or the game's dots they show there, changed;
 * its map rows where the columns it reaches did. */
enum { SENT = 0x5A, NO_ROW = 0xFF };
typedef struct {
    uint8_t kept;            /* SENT where the rest says what the panel's memory holds */
    uint8_t position, string, choice, lines, first, row_due;
    uint16_t frame_chars, text_chars, looks;
    uint32_t table;
    uint8_t ink[DIALOG_TEXT_LINES][COLUMNS];
    uint16_t sums[DIALOG_TEXT_LINES][COLUMNS];
    Page page;
} Sent;
_Static_assert(sizeof(Sent) <= VID_VIEW_MEMORY, "what the window sent fits the view's memory");

/* A piece's dots in a column of the window: from which game line, how far
 * across from where they show, and which of the window's dots. */
typedef struct {
    uint8_t line, lo, hi;
    int16_t across;
} Cover;

static unsigned cover_of(const WindowLine *l, unsigned column, Cover cover[PIECES])
{
    unsigned n = 0;
    for (unsigned k = 0; l && k < l->count; k++) {
        const Piece *p = &l->piece[k];
        int lo = p->to > column * TILE_DOTS ? p->to : (int)(column * TILE_DOTS);
        int hi = p->to + p->dots < (column + 1) * TILE_DOTS ? p->to + p->dots : (int)((column + 1) * TILE_DOTS);
        if (lo < hi)
            cover[n++] = (Cover){ p->line, (uint8_t)lo, (uint8_t)hi, (int16_t)(p->from - p->to) };
    }
    return n;
}

/* Whether a window line's column shows anything other than it did. */
static bool column_changed(const WindowLine *was, const WindowLine *now, unsigned column,
                           const uint32_t changed[DIALOG_TEXT_LINES])
{
    Cover a[PIECES], b[PIECES];
    unsigned n = cover_of(was, column, a);
    if (n != cover_of(now, column, b))
        return true;
    for (unsigned k = 0; k < n; k++) {
        if (a[k].line != b[k].line || a[k].lo != b[k].lo || a[k].hi != b[k].hi || a[k].across != b[k].across)
            return true;
        unsigned first = (unsigned)(b[k].lo + b[k].across) / TILE_DOTS, last = (unsigned)(b[k].hi - 1 + b[k].across) / TILE_DOTS;
        for (unsigned c = first; c <= last; c++)
            if (changed[b[k].line] >> c & 1)
                return true;
    }
    return false;
}

static uint32_t columns_reached(const WindowLine *l)
{
    uint32_t reached = 0;
    for (unsigned column = 0; column < WINDOW_COLUMNS; column++)
        if (line_reaches(l, column))
            reached |= 1u << column;
    return reached;
}

/* What of window line `at` changed since it was sent, sent. */
static void send_the_changes(Platform *pf, TileReader *t, const WindowLine *was, const WindowLine *now, unsigned at,
                             const uint32_t changed[DIALOG_TEXT_LINES], uint16_t chars, uint16_t looks)
{
    for (unsigned column = 0; now && column < WINDOW_COLUMNS; column++)
        if (line_reaches(now, column) && column_changed(was, now, column, changed))
            send_the_tiles(pf, t, now, at, column, chars);
    if (columns_reached(was) != columns_reached(now)) {
        send_the_map_row(pf, now, 2 * at, looks);
        send_the_map_row(pf, now, 2 * at + 1, looks);
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

    Sent sent;
    memcpy(&sent, vid_view_memory(pf), sizeof sent);
    bool may_join = !field_choice_pointer_shown(g);
    uint8_t choice = (uint8_t)(dialog_choice_pointer(g) | (may_join ? 0 : 0x80));
    uint16_t looks = (uint16_t)(map_entry(pf, 0, 0) & ENTRY_LOOKS);
    bool same = sent.kept == SENT && sent.position == (uint8_t)position && sent.string == dialog_string(g) &&
                sent.table == dialog_string_table(g) && sent.choice == choice &&
                sent.frame_chars == frame_layer.char_base && sent.text_chars == text_layer.char_base &&
                sent.looks == looks;
    bool sending = dialog_text_sending(g) != 0;
    unsigned row = dialog_text_row(g) % DIALOG_TEXT_LINES;
    unsigned read = !same ? 0xF : (sending ? 1u << row : 0) | (sent.row_due < DIALOG_TEXT_LINES ? 1u << sent.row_due : 0);
    uint32_t changed[DIALOG_TEXT_LINES] = { 0 };   /* the columns whose dots changed, a bit each */
    uint32_t any_changed = 0;
    bool inked_as_sent = true;   /* no line's ink moved */
    TileReader t = { .chars = text_layer.char_base, .line = NO_LINE };
    for (unsigned line = 0; line < DIALOG_TEXT_LINES; line++) {
        if (!(read >> line & 1))
            continue;
        uint8_t ink[COLUMNS];
        uint16_t sums[COLUMNS];
        line_ink(pf, &t, line, ink, sums);
        for (unsigned column = 0; column < COLUMNS; column++) {
            if (ink[column] != sent.ink[line][column])
                inked_as_sent = false;
            if (ink[column] != sent.ink[line][column] || sums[column] != sent.sums[line][column])
                changed[line] |= 1u << column;
        }
        any_changed |= changed[line];
        memcpy(sent.ink[line], ink, sizeof ink);
        memcpy(sent.sums[line], sums, sizeof sums);
    }
    /* The page is set again only where a line's ink moved since it was set:
     * the game types a glyph a frame at most, and a page that waits for the
     * player stays as it is. */
    Page page;
    if (same && inked_as_sent)
        page = sent.page;
    else
        set_the_page((const uint8_t(*)[COLUMNS])sent.ink, may_join, &page);
    unsigned first, lines = lines_shown(g, &page, view, &first);

    /* The window opens and shuts from its middle, as far as the screen's
     * band has; it stands at the view's top or foot. */
    int height = (int)(2 * BORDER_ROWS * TILE_DOTS + lines * LINE_LINES);
    int shown = band.full ? height * band.half / band.full : height;
    int top = height / 2 - shown / 2;
    int at = position == WINDOW_AT_TOP ? 0 : (int)view.height - height;
    VidDrawnPanel panel = { (int16_t)(at + top), (uint16_t)shown, (int16_t)(band.middle - band.half),
                            (uint16_t)(2 * band.half), VID_LAYER_BIT(VID_BG1) | VID_LAYER_BIT(VID_BG3),
                            VID_LAYER_BIT(VID_BG3), { { 0 } } };
    panel.bg[VID_BG1] = (VidBg){ OWN_FRAME_MAP, 32, 32, frame_layer.char_base, 0, (uint16_t)top, false, false };
    panel.bg[VID_BG3] = (VidBg){ OWN_TEXT_MAP, 32, 32, text_layer.char_base, 0, (uint16_t)(top - TEXT_DOWN), false, false };
    if (shown > 0) {
        vid_draw_a_panel_in_view(pf, &panel);
        if (!same || lines != sent.lines) {
            send_the_blank_tile(pf, text_layer.char_base);
            for (unsigned k = 0; k < lines; k++)
                send_the_line(pf, &t, window_line(&page, first, k), k, text_layer.char_base);
            send_the_text_map(pf, &page, first, lines, looks);
            send_the_frame(pf, lines);
        } else if (any_changed) {
            /* with no dot changed, the page and its lines are what was sent */
            for (unsigned k = 0; k < lines; k++)
                send_the_changes(pf, &t, window_line(&sent.page, sent.first, k), window_line(&page, first, k), k,
                                 changed, text_layer.char_base, looks);
        }
        sent.kept = SENT;
        sent.position = (uint8_t)position;
        sent.string = dialog_string(g);
        sent.table = dialog_string_table(g);
        sent.choice = choice;
        sent.frame_chars = frame_layer.char_base;
        sent.text_chars = text_layer.char_base;
        sent.looks = looks;
        sent.lines = (uint8_t)lines;
        sent.first = (uint8_t)first;
        sent.page = page;
    } else {
        sent.kept = 0;   /* nothing sent: the window opens afresh */
    }
    sent.row_due = sending ? (uint8_t)row : NO_ROW;
    memcpy(vid_view_memory(pf), &sent, sizeof sent);
    uint16_t open = (uint16_t)((int)view.height - height);
    int16_t clear_of = (int16_t)(band.middle - band.full), clear_to = (int16_t)(band.middle + band.full);
    return position == WINDOW_AT_TOP ? (ViewLines){ (int16_t)height, open, clear_of, clear_to }
                                     : (ViewLines){ 0, open, clear_of, clear_to };
}
