/* Field mode: the per-line effects.
 *
 * The field runs eight per-line effects: the layers' tile map addresses, BG1-BG3
 * scroll, the screens and their window masks, window 1's edges, the color math
 * layers with the fixed color, and one edge of window 2. Each reads a table in
 * bank $7F (field_line_effect_tables) in the indirect format the video
 * service's line effects take: entries of a line count and a 16-bit pointer to
 * the values in bank $7F, ended by a 0. The values hold for that many lines; a
 * count with LINES_EACH set gives each line values of its own, read on from the
 * pointer.
 *
 * There are two sets of tables. Every frame the field builds the set the line
 * effects aren't reading and flips field_display_flags bit 0, which makes the
 * next vblank program the set just built. What a set holds depends on the
 * dialog window: with none, every effect is the same over the whole screen;
 * with one open, it is a band dialog_window_state lines either side of line 48
 * (the top window) or line 171 (the bottom one), inside which BG1 and BG3 show
 * the window's tile map, the screens show the window's layers, and the color
 * math shades it (field_dialog_shading). Either layer can wave
 * (field_line_wave_bg2, field_line_wave_bg3): its scroll table then points into
 * field_line_wave every 16 lines, which the builder refills every frame from a
 * wave table in the ROM.
 *
 * The builders' arithmetic is 8-bit, as the original's: a window taller than
 * the space it has wraps the counts, and the tables say so. The per-line window
 * tables are filled by the event screen effects and the window shapes
 * (field/window_shape.c); the builders only point at them. Window 1 reads
 * field_window_edge_lines a word a line (and field_window_edge_lines_copy for
 * the second set); window 2 reads the same buffer a byte a line, as two
 * tables, its halves (window2_edge_table).
 *
 * The tables hold addresses in bank $7F as their data, which is the format the
 * line effects read: those are the one place the port spells where a variable
 * is (game/ram.h), as values it writes, never to reach memory. */
#include "field/line_effects.h"

#include "cpu_io.h"
#include "field/bridge.h"
#include "field/field.h"
#include "field/field_data.h"
#include "field/scratch.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

/* ---- Tables ------------------------------------------------------------------------- */

enum {
    LINES_EACH = 0x80,  /* a count with this bit: each line has values of its own */
    TABLE_END = 0x00,
    ENTRY_SIZE = 3,     /* a count, then a 16-bit pointer */
};

/* The eight effects, in the order of their tables within a set. */

/* A table being written: which one, and the offset of its next entry. */
typedef struct {
    unsigned table;
    uint16_t next;
} TableWriter;

static TableWriter table_of(uint8_t set, uint8_t slot) { return (TableWriter){ (unsigned)(set * SLOTS + slot), 0 }; }

static void put(GameState *g, TableWriter *t, uint8_t lines, uint16_t values)
{
    set_field_line_effect_tables_byte(g, t->table, t->next, lines);
    set_field_line_effect_tables_byte(g, t->table, t->next + 1u, (uint8_t)values);
    set_field_line_effect_tables_byte(g, t->table, t->next + 2u, (uint8_t)(values >> 8));
    t->next = (uint16_t)(t->next + ENTRY_SIZE);
}

static void end_table(GameState *g, TableWriter t) { set_field_line_effect_tables_byte(g, t.table, t.next, TABLE_END); }

/* ---- Where the values are, as the tables point at them ----------------------------------- */

enum { SCREENS_OUTSIDE, SCREENS_WINDOW_BODY, SCREENS_WINDOW_EDGE };
enum { WAVE_LINES = 16, WAVE_SET_LINES = 2 * WAVE_LINES, WAVE_ENTRY = RAM_FIELD_LINE_WAVE_STRIDE };

static uint16_t maps_values(bool in_window)
{
    return RAM_ABS(RAM_FIELD_LINE_BG_MAPS + in_window * RAM_FIELD_LINE_BG_MAPS_STRIDE);
}
static uint16_t scroll_values(uint8_t layer, uint8_t set)
{
    return RAM_ABS(RAM_FIELD_LINE_SCROLL + (layer * 2 + set) * RAM_FIELD_LINE_SCROLL_STRIDE);
}
/* BG1's or BG3's scroll inside a window at `position`: the top window's
 * two, then the bottom one's. */
static unsigned window_scroll(WindowPosition position, bool bg3)
{
    return (position == WINDOW_AT_BOTTOM) * 2u + bg3;
}
static uint16_t window_scroll_values(WindowPosition position, bool bg3)
{
    return RAM_ABS(RAM_FIELD_LINE_WINDOW_SCROLL + window_scroll(position, bg3) * RAM_FIELD_LINE_WINDOW_SCROLL_STRIDE);
}
static uint16_t screens_values(uint8_t which)
{
    return RAM_ABS(RAM_FIELD_LINE_SCREENS + which * RAM_FIELD_LINE_SCREENS_STRIDE);
}
static uint16_t color_math_values(uint8_t set, bool in_window)
{
    return RAM_ABS(RAM_FIELD_LINE_COLOR_MATH + (set + 2 * in_window) * RAM_FIELD_LINE_COLOR_MATH_STRIDE);
}
static uint16_t window1_edges_values(uint8_t set)
{
    return RAM_ABS(RAM_FIELD_LINE_WINDOW1_EDGES + set * RAM_FIELD_LINE_WINDOW1_EDGES_STRIDE);
}
static uint16_t shading_values(uint8_t set)
{
    return set ? RAM_ABS(RAM_FIELD_DIALOG_SHADING_COPY) : RAM_ABS(RAM_FIELD_DIALOG_SHADING);
}
static uint16_t window_edge_lines_values(uint8_t set)
{
    return set ? RAM_ABS(RAM_FIELD_WINDOW_EDGE_LINES_COPY) : RAM_ABS(RAM_FIELD_WINDOW_EDGE_LINES);
}
static uint16_t wave_values(uint8_t set, unsigned line)
{
    return (uint16_t)(RAM_ABS(RAM_FIELD_LINE_WAVE) + (set * WAVE_SET_LINES + line) * WAVE_ENTRY);
}

/* The two color math entries a set has: outside the window and inside it. */
static unsigned color_math(uint8_t set, bool in_window) { return set + 2u * in_window; }

/* ---- Parts of the builders ------------------------------------------------------------ */

enum { WHOLE_SCREEN_TOP = 100, WHOLE_SCREEN_BOTTOM = 112 };

/* The same values over the whole screen: two entries, since a count holds 127
 * lines at most. */
static void whole_screen(GameState *g, TableWriter t, uint16_t values)
{
    put(g, &t, WHOLE_SCREEN_TOP, values);
    put(g, &t, WHOLE_SCREEN_BOTTOM, values);
    end_table(g, t);
}

/* A waving layer over the whole screen: 13 entries of 16 lines, each reading
 * the 16 lines of the wave. */
enum { WAVE_SCREEN_PARTS = 13 };
static void wave_screen(GameState *g, TableWriter t, uint16_t wave)
{
    for (int i = 0; i < WAVE_SCREEN_PARTS; i++)
        put(g, &t, LINES_EACH | WAVE_LINES, wave);
    end_table(g, t);
}

/* Whether a layer waves, as the tables' layout sees it: the setting's low
 * byte. The values sent each frame look at the whole word. */
static bool bg2_waves(const GameState *g) { return (uint8_t)field_line_wave_bg2(g) != 0; }
static bool bg3_waves(const GameState *g) { return (uint8_t)field_line_wave_bg3(g) != 0; }

// $FD:C4B5 (set 0), $FD:C52E (set 1)
/* BG2's scroll table: BG2 has no window band, so its scroll is the same over
 * the whole screen, or waves. */
static void set_bg2_table(GameState *g, uint8_t set)
{
    TableWriter t = table_of(set, SLOT_BG2_SCROLL);
    if (!bg2_waves(g))
        whole_screen(g, t, scroll_values(FIELD_BG2, set));
    else
        wave_screen(g, t, wave_values(set, 0));
}

/* Window 2's edge tables: a byte a line for the screen's 224 lines, two of
 * them, which are the two halves of field_window_edge_lines read a byte at a
 * time (window 1 reads the buffer a word a line). */
enum { WINDOW2_TABLE_BYTES = 0xE0 };

static uint16_t window2_edge_table(uint8_t half)
{
    return (uint16_t)(window_edge_lines_values(0) + half * WINDOW2_TABLE_BYTES);
}

/* Set 0's window 2 reads the first table and set 1's the second, swapped
 * where a script asks (field_window_2_second_table, the high byte of the
 * scripts' variable $A8). */
enum { TABLES_SWAPPED = 0x01 };

static bool window2_choice(const GameState *g) { return (field_window_2_second_table(g) & TABLES_SWAPPED) != 0; }

static uint16_t window2_lines(const GameState *g, uint8_t set)
{
    return window2_edge_table(window2_choice(g) != (set != 0) ? 0 : 1);
}

// $FD:C439 (set 0), $FD:C477 (set 1)
static void set_window2_table(GameState *g, uint8_t set)
{
    uint16_t lines = window2_lines(g, set);
    TableWriter t = table_of(set, SLOT_WINDOW2);
    put(g, &t, LINES_EACH | WHOLE_SCREEN_TOP, lines);
    put(g, &t, LINES_EACH | WHOLE_SCREEN_BOTTOM, (uint16_t)(lines + WHOLE_SCREEN_TOP));
    end_table(g, t);
}

/* ---- The wave ------------------------------------------------------------------------- */

/* A waving layer's table reads the wave 16 lines to an entry. With the dialog
 * window open, the wave's line carries on from part to part, the band's lines
 * included, so the wave runs on unbroken behind the window. */

enum { WAVE_PHASE_STEP = 0x3E };   /* the phase's even steps into the offsets' table */

/* For the original's code and shadow mode (field/scratch.h): the original
 * counts the wave's line in the field page's cell the objects' motion keeps
 * its direction in (field_direction, which nothing moving uses while the
 * tables are built), and leaves it there. */
static void leave_wave_line(GameState *g, uint8_t line) { set_field_direction(g, line); }

// $FD:CC5E (set 0, BG3's table), $FD:CCB8 (set 1)
/* `lines` lines of the wave, 16 or fewer to an entry, each entry starting at
 * the wave's line, which moves on past them. */
static void wave_part(GameState *g, TableWriter *t, uint8_t set, uint8_t lines, uint8_t *line)
{
    for (;;) {
        uint8_t entry = lines < WAVE_LINES ? lines : WAVE_LINES;
        put(g, t, LINES_EACH | entry, wave_values(set, *line & (WAVE_LINES - 1)));
        *line = (uint8_t)(*line + entry);
        lines = (uint8_t)(lines - entry);
        if (lines == 0 || entry < WAVE_LINES)
            return;
    }
}

/* The wave's scroll, one line at a time: BG3's scroll moved by the wave's
 * offset for the line. The original adds the 16 lines as one running sum, so
 * a line whose sum passes the scroll's range moves the next line a pixel to
 * the right, and while window 2 reads its second table the first line moves
 * a pixel too (the choice's bit is still in the adder). On the screen that is
 * a one-pixel kink in the wave; this is the one place it is kept. */
static bool a_layer_waves(const GameState *g) { return field_line_wave_bg3(g) != 0 || field_line_wave_bg2(g) != 0; }

static uint16_t wave_start(uint8_t phase) { return phase & WAVE_PHASE_STEP; }

static void fill_wave_scroll(GameState *g, uint8_t set, uint16_t start, uint16_t scroll)
{
    bool nudge = window2_choice(g);
    for (unsigned i = 0; i < WAVE_LINES; i++) {
        uint16_t offset = field_wave_offset(g, (uint16_t)(start + i * WAVE_ENTRY));
        uint32_t sum = (uint32_t)offset + scroll + nudge;
        set_field_line_wave_h(g, set * WAVE_SET_LINES + i, (uint16_t)sum);
        nudge = sum > 0xFFFF;
    }
}

// $FD:C5A7 (set 0), $FD:C6F7 (set 1)
/* The values the set's tables point at that change every frame: the layers'
 * scroll, the wave when a layer waves (its phase moves on a line each
 * frame), and the color math. */
static void send_scroll(GameState *g, uint8_t set)
{
    for (uint8_t layer = FIELD_BG1; layer <= FIELD_BG2; layer++) {
        set_field_line_scroll_h(g, layer * 2u + set, field_layer_scroll_h(g, layer));
        set_field_line_scroll_v(g, layer * 2u + set, field_layer_scroll_v(g, layer));
    }
    uint16_t bg3_h = field_layer_scroll_h(g, FIELD_BG3), bg3_v = field_layer_scroll_v(g, FIELD_BG3);
    if (!a_layer_waves(g)) {
        set_field_line_scroll_h(g, FIELD_BG3 * 2u + set, bg3_h);
        set_field_line_scroll_v(g, FIELD_BG3 * 2u + set, bg3_v);
    } else {
        uint8_t phase = (uint8_t)(field_line_wave_phase(g) + 1);
        set_field_line_wave_phase(g, phase);
        fill_wave_scroll(g, set, wave_start(phase), bg3_h);
        for (unsigned i = 0; i < WAVE_LINES; i++)
            set_field_line_wave_v(g, set * WAVE_SET_LINES + i, bg3_v);
        /* The second 16 lines repeat the first, so an entry can read 16 lines
         * from any of them. */
        for (unsigned i = 0; i < WAVE_LINES; i++) {
            unsigned line = set * WAVE_SET_LINES + i;
            set_field_line_wave_h(g, line + WAVE_LINES, field_line_wave_h(g, line));
            set_field_line_wave_v(g, line + WAVE_LINES, field_line_wave_v(g, line));
        }
    }
    set_field_line_color_math_fixed_color(g, color_math(set, false), field_fixed_color(g));
    uint8_t layers = field_fixed_color_math_layers(g);
    set_field_line_color_math_layers(g, color_math(set, false), layers);
    set_field_line_color_math_layers(g, color_math(set, true), layers);
}

// $FD:C401 (set 0), $FD:C95D (set 1)
/* The same while the field's setup builds both sets (FIELD_DISPLAY_SETUP): the scroll from field_bg1_scroll_position and
 * field_auto_scroll_position, the color math layers from
 * field_color_math_layers_override, and no wave. */
static void send_scroll_override(GameState *g, uint8_t set)
{
    set_field_line_scroll_h(g, FIELD_BG1 * 2u + set, field_bg1_scroll_position_h(g));
    set_field_line_scroll_v(g, FIELD_BG1 * 2u + set, field_bg1_scroll_position_v(g));
    for (uint8_t layer = FIELD_BG2; layer <= FIELD_BG3; layer++) {
        uint16_t v = field_auto_scroll_position_y(g, layer - 1u);
        set_field_line_scroll_h(g, layer * 2u + set, field_auto_scroll_position_x(g, layer - 1u));
        set_field_line_scroll_v(g, layer * 2u + set, v);
    }
    set_field_line_color_math_fixed_color(g, color_math(set, false), field_fixed_color(g));
    uint8_t layers = field_color_math_layers_override(g);
    set_field_line_color_math_layers(g, color_math(set, false), layers);
    set_field_line_color_math_layers(g, color_math(set, true), layers);
}

static bool overridden(const GameState *g) { return field_display_flags(g) & FIELD_DISPLAY_SETUP; }

/* ---- The builders --------------------------------------------------------------------- */

// $FD:C2EB (set 0), $FD:C847 (set 1)
/* No dialog window: every effect the same over the whole screen. Window 1
 * reads its per-line table; BG2 and BG3 may wave. */
static void build_no_window(GameState *g, uint8_t set)
{
    whole_screen(g, table_of(set, SLOT_BG_MAPS), maps_values(false));
    whole_screen(g, table_of(set, SLOT_BG1_SCROLL), scroll_values(FIELD_BG1, set));
    set_bg2_table(g, set);
    if (!bg3_waves(g))
        whole_screen(g, table_of(set, SLOT_BG3_SCROLL), scroll_values(FIELD_BG3, set));
    else
        wave_screen(g, table_of(set, SLOT_BG3_SCROLL), wave_values(set, 0));
    whole_screen(g, table_of(set, SLOT_SCREENS), screens_values(SCREENS_OUTSIDE));
    TableWriter t = table_of(set, SLOT_WINDOW1);
    uint16_t edges = window_edge_lines_values(set);
    put(g, &t, LINES_EACH | WHOLE_SCREEN_TOP, edges);
    put(g, &t, LINES_EACH | WHOLE_SCREEN_BOTTOM, (uint16_t)(edges + 2 * WHOLE_SCREEN_TOP));
    end_table(g, t);
    whole_screen(g, table_of(set, SLOT_COLOR_MATH), color_math_values(set, false));
    set_window2_table(g, set);
    if (overridden(g))
        send_scroll_override(g, set);
    else
        send_scroll(g, set);
}

/* The dialog window: a band `half` lines either side of its middle. All of it
 * is 8-bit, as in the original. */
typedef struct {
    uint8_t half;
    uint8_t band;       /* 2 * half */
    uint8_t edge;       /* lines of the window's frame at its top and at its bottom */
    uint8_t body;       /* the lines between */
} Band;

enum { FRAME_LINES = 10 };

static Band dialog_band(uint8_t half)
{
    Band b = { half, (uint8_t)(half * 2), (uint8_t)(half - 1), 2 };
    if (half > FRAME_LINES) {
        b.edge = FRAME_LINES;
        b.body = (uint8_t)(half * 2 - 2 * FRAME_LINES);
    }
    return b;
}

/* How a table divides the screen around the band: the parts' line counts, the
 * one at `band` being the window's. */
typedef struct {
    uint8_t lines[6];
    uint8_t count;
    uint8_t band;
} Layout;

static void band_table(GameState *g, TableWriter t, const Layout *l, uint16_t outside, uint16_t inside)
{
    for (uint8_t i = 0; i < l->count; i++)
        put(g, &t, l->lines[i], i == l->band ? inside : outside);
    end_table(g, t);
}

/* A waving layer's scroll with the band in it: the wave's line carries on
 * across the parts, the band's lines included. */
static void wave_band_table(GameState *g, TableWriter t, uint8_t set, const Layout *l, uint16_t inside)
{
    uint8_t line = 0;
    for (uint8_t i = 0; i < l->count; i++) {
        if (i == l->band) {
            put(g, &t, l->lines[i], inside);
            line = (uint8_t)(line + l->lines[i]);
        } else {
            wave_part(g, &t, set, l->lines[i], &line);
        }
    }
    end_table(g, t);
    ORIGINAL_EFFECT(leave_wave_line(g, line));
}

/* The screens: the window's frame at the band's top and bottom, its body
 * between. */
static void screens_table(GameState *g, TableWriter t, const Layout *l, const Band *b)
{
    for (uint8_t i = 0; i < l->count; i++) {
        if (i == l->band) {
            put(g, &t, b->edge, screens_values(SCREENS_WINDOW_EDGE));
            put(g, &t, b->body, screens_values(SCREENS_WINDOW_BODY));
            put(g, &t, b->edge, screens_values(SCREENS_WINDOW_EDGE));
        } else {
            put(g, &t, l->lines[i], screens_values(SCREENS_OUTSIDE));
        }
    }
    end_table(g, t);
}

/* The color math: inside the band, one line of shading per line, read from
 * the middle of field_dialog_shading out, and its last line the window's own. */
enum { SHADING_MIDDLE = 0x28 };
static void color_math_table(GameState *g, TableWriter t, uint8_t set, const Layout *l, const Band *b)
{
    uint16_t outside = color_math_values(set, false);
    uint16_t first = (uint16_t)(((uint8_t)(SHADING_MIDDLE - b->half) & 0x3F) * RAM_FIELD_DIALOG_SHADING_STRIDE);
    for (uint8_t i = 0; i < l->count; i++) {
        if (i == l->band) {
            put(g, &t, (uint8_t)(b->band - 1) | LINES_EACH, (uint16_t)(shading_values(set) + first));
            put(g, &t, 1, color_math_values(set, true));
        } else {
            put(g, &t, l->lines[i], outside);
        }
    }
    end_table(g, t);
}

/* Where the two windows lie. The top one: 7 lines, then (41 - half) above the
 * band and (100 - half) below it, then 30. The bottom one: 100 + 29 lines, then
 * (42 - half) above the band and (40 - half) below it. */
enum {
    TOP_START = 7, TOP_ABOVE = 0x29, TOP_BELOW = 0x64, TOP_END = 0x1E,
    TOP_WAVE_BELOW = 0x82,      /* what a waving BG3's table has below the top window */
    TOP_WINDOW1_END = 0x47,     /* and window 1's last part */
    BOTTOM_START = 0x64, BOTTOM_NEXT = 0x1D, BOTTOM_EDGE = 0x29,
    BOTTOM_WAVE_START = 0x58,   /* a waving BG3's table divides the first 129 lines 88 + 41 */
};

/* For the original's code and shadow mode (field/scratch.h): window 1's
 * table keeps a count of lines beside the band ($DB) and its last part's
 * pointer ($D9). */
static void leave_window1_counts(GameState *g, uint8_t beside_band, uint16_t last_part)
{
    original_cell8(g, 0xDB, beside_band);
    original_cell16(g, 0xD9, last_part);
}

/* Window 1 follows its per-line table (two bytes a line) outside the band,
 * reading on past the band's lines, and covers the whole line inside it. The
 * top window's last part is longer than the other tables'. */
static void window1_table(GameState *g, uint8_t set, WindowPosition position, const Layout *plain)
{
    TableWriter t = table_of(set, SLOT_WINDOW1);
    uint16_t lines = window_edge_lines_values(set);
    uint8_t beside_band = 0;
    const uint8_t last = (uint8_t)(plain->count - 1);
    for (uint8_t i = 0; i < plain->count; i++) {
        uint8_t n = plain->lines[i];
        if (i == plain->band) {
            put(g, &t, n, window1_edges_values(set));
        } else {
            if (i == plain->band - 1 || (position == WINDOW_AT_TOP && i == plain->band + 1))
                beside_band = n;
            if (i == last) {
                ORIGINAL_EFFECT(leave_window1_counts(g, beside_band, lines));
                if (position == WINDOW_AT_TOP)
                    n = TOP_WINDOW1_END;
            }
            put(g, &t, LINES_EACH | n, lines);
        }
        lines = (uint16_t)(lines + 2 * n);
    }
    end_table(g, t);
}

// $FD:C995 (set 0), $FD:CD0C (set 1): the top window
// $FD:CFCF (set 0), $FD:D27E (set 1): the bottom window
static void build_window(GameState *g, uint8_t set, WindowPosition position)
{
    const Band b = dialog_band(dialog_window_state(g));
    Layout plain, waving, screens;
    if (position == WINDOW_AT_TOP) {
        const uint8_t above = (uint8_t)(TOP_ABOVE - b.half), below = (uint8_t)(TOP_BELOW - b.half);
        plain = (Layout){ { TOP_START, above, b.band, below, TOP_END }, 5, 2 };
        waving = (Layout){ { TOP_START, above, b.band, (uint8_t)(TOP_WAVE_BELOW - b.half), TOP_END }, 5, 2 };
        /* The screens table has the lines above the band again after it; that
         * only moves where its last part ends. */
        screens = (Layout){ { TOP_START, above, b.band, above, below, TOP_END }, 6, 2 };
    } else {
        const uint8_t above = (uint8_t)(BOTTOM_EDGE - b.half + 1), below = (uint8_t)(BOTTOM_EDGE - b.half - 1);
        plain = (Layout){ { BOTTOM_START, BOTTOM_NEXT, above, b.band, below }, 5, 3 };
        waving = (Layout){ { BOTTOM_WAVE_START, BOTTOM_EDGE, above, b.band, below }, 5, 3 };
        screens = plain;
    }

    band_table(g, table_of(set, SLOT_BG_MAPS), &plain, maps_values(false), maps_values(true));
    band_table(g, table_of(set, SLOT_BG1_SCROLL), &plain, scroll_values(FIELD_BG1, set),
               window_scroll_values(position, false));
    set_bg2_table(g, set);
    uint16_t bg3_inside = window_scroll_values(position, true);
    if (!bg3_waves(g))
        band_table(g, table_of(set, SLOT_BG3_SCROLL), &plain, scroll_values(FIELD_BG3, set), bg3_inside);
    else
        wave_band_table(g, table_of(set, SLOT_BG3_SCROLL), set, &waving, bg3_inside);
    screens_table(g, table_of(set, SLOT_SCREENS), &screens, &b);
    window1_table(g, set, position, &plain);
    color_math_table(g, table_of(set, SLOT_COLOR_MATH), set, &plain, &b);
    set_window2_table(g, set);
    send_scroll(g, set);
}

/* Whether the dialog window takes a band: it is at the top or the bottom,
 * and open. */
static bool window_open(const GameState *g, WindowPosition position)
{
    return position != WINDOW_NONE && dialog_window_state(g) != 0;
}

/* The builder for a window position. */
static void build_for(GameState *g, uint8_t set, WindowPosition position)
{
    if (!window_open(g, position))
        build_no_window(g, set);
    else
        build_window(g, set, position);
}

/* The set the line effects aren't reading, which is the one built. */
static uint8_t set_to_build(const GameState *g) { return (field_display_flags(g) & FIELD_SECOND_TABLE_SET) ? 0 : 1; }

// $FD:C2C1
/* Every frame: build the table set the line effects aren't reading, for
 * where the dialog window is, and make it the one they read next. */
static void build_line_effect_tables(GameState *g)
{
    build_for(g, set_to_build(g), dialog_window_at(g));
    set_field_display_flags(g, (uint8_t)(field_display_flags(g) ^ FIELD_SECOND_TABLE_SET));
}

// $FD:D52D
/* The dialog window's shading, a line at a time from the window's middle out:
 * the fixed color added to BG1 from intensity 8 down to 1, then subtracted
 * from 0 up to 9, each intensity for 40 / 8 lines (0 for the remainder's
 * lines too), and a copy of it for table set 1. The copy is a block move of
 * 256 bytes up into its own source, so its last $38 bytes repeat its first. */
static const VidColorMath SHADING_ADD = { .layers = VID_BG1_BIT };
static const VidColorMath SHADING_SUBTRACT = { .subtract = true, .layers = VID_BG1_BIT };
enum {
    ALL_CHANNELS = VID_RED | VID_GREEN | VID_BLUE,   /* the fixed color is a gray */
    SHADING_LINES = 0x28, SHADING_STEPS = 8, SHADING_DEEPEST = 9,
    SHADING_COPY_BYTES = 0x100,
};

/* `lines` lines of shading from `*line` on. */
static void shade(GameState *g, uint8_t *line, uint8_t lines, const VidColorMath *math, uint8_t intensity)
{
    for (uint8_t i = 0; i < lines; i++, *line = (uint8_t)(*line + 1)) {
        set_field_dialog_shading_fixed_color(g, *line, vid_line_fixed_color(ALL_CHANNELS, intensity));
        set_field_dialog_shading_layers(g, *line, vid_line_math_layers(math));
    }
}

/* Each intensity's lines, the lines past them the unshaded middle takes, and
 * the lines the shading covers. */
enum {
    SHADING_STEP = SHADING_LINES / SHADING_STEPS,
    SHADING_REST = SHADING_LINES % SHADING_STEPS,
    SHADED_LINES = SHADING_STEPS * SHADING_STEP + SHADING_STEP + SHADING_REST + SHADING_DEEPEST * SHADING_STEP,
};

/* For the original's code and shadow mode (field/scratch.h): the original
 * divides on the hardware divider, whose registers keep the division, and
 * keeps where its run would have ended next ($DB), its step and remainder in
 * bytes ($DD, $DF) and its last color ($E1) on the field's page. */
static void leave_shading(GameState *g)
{
    hw_set_dividend(g, SHADING_LINES);
    hw_divide_by(g, SHADING_STEPS);
    original_cell8(g, 0xDB, (uint8_t)((SHADED_LINES + SHADING_STEP) * 2));
    original_cell8(g, 0xDD, (uint8_t)(SHADING_STEP * 2));
    original_cell8(g, 0xDF, (uint8_t)(SHADING_REST * 2));
    original_cell8(g, 0xE1, vid_line_fixed_color(ALL_CHANNELS, SHADING_DEEPEST));
}

/* Table set 1's copy of the shading. The copy runs on past the shading's lines
 * into the start of the copy itself, which lies right after them, so its lines
 * past the shading's repeat its own first ones. */
static void copy_dialog_shading(GameState *g)
{
    for (unsigned i = 0; i < SHADING_COPY_BYTES / 2; i++) {
        uint16_t line;
        if (i < FIELD_DIALOG_SHADING_COUNT) {
            uint8_t layers = field_dialog_shading_layers(g, i);
            line = (uint16_t)(field_dialog_shading_fixed_color(g, i) << 8 | layers);
        } else
            line = field_dialog_shading_copy(g, i - FIELD_DIALOG_SHADING_COUNT);
        set_field_dialog_shading_copy(g, i, line);
    }
}

static void fill_dialog_shading(GameState *g)
{
    uint8_t line = 0;
    for (uint8_t intensity = SHADING_STEPS; intensity >= 1; intensity--)
        shade(g, &line, SHADING_STEP, &SHADING_ADD, intensity);
    shade(g, &line, SHADING_STEP + SHADING_REST, &SHADING_SUBTRACT, 0);
    for (uint8_t intensity = 1; intensity <= SHADING_DEEPEST; intensity++)
        shade(g, &line, SHADING_STEP, &SHADING_SUBTRACT, intensity);
    ORIGINAL_EFFECT(leave_shading(g));
    copy_dialog_shading(g);
}

/* The inside of the dialog window, which the field's setup fills once: BG1 and
 * BG3 read the window's tile map and scroll it into place, window 1 spans the
 * whole line, the window's body shows BG1 and BG3 and its frame BG1, and the
 * color math there is black. */
static const VidBg WINDOW_MAP = { VID_TILEMAP(0x1C00, 32, 32) };
enum { WHOLE_LINE_LEFT = 0x00, WHOLE_LINE_RIGHT = 0xFF };

/* How far BG1 and BG3 scroll the window's tile map down (up, where negative)
 * so that its rows land in the top window's band or the bottom one's. */
static const int16_t window_map_scroll_v[WINDOW_POSITIONS][2] = {
    [WINDOW_AT_TOP] = { 55, -15 },
    [WINDOW_AT_BOTTOM] = { -68, -138 },
};
enum { SCREENS_WINDOW_BODY_LAYERS = VID_BG1_BIT | VID_BG3_BIT, SCREENS_WINDOW_EDGE_LAYERS = VID_BG1_BIT };

static uint32_t bytes4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return (uint32_t)a | (uint32_t)b << 8 | (uint32_t)c << 16 | (uint32_t)d << 24;
}

// $FD:C124
static void init_line_effect_values(GameState *g)
{
    uint8_t maps[3];
    for (unsigned layer = 0; layer < 3; layer++)
        maps[layer] = (uint8_t)((field_layer_map_base(g, layer) >> 8) | field_layer_map_size(g, layer));
    set_field_line_bg_maps(g, 0, bytes4(maps[0], maps[1], maps[2], 0));
    uint8_t window_map = vid_line_bg_map(&WINDOW_MAP);
    set_field_line_bg_maps(g, 1, bytes4(window_map, (uint8_t)((field_layer_map_base(g, FIELD_BG2) >> 8) | FIELD_MAP_TWO_SCREENS_WIDE), window_map, 0));

    for (WindowPosition position = WINDOW_AT_TOP; position <= WINDOW_AT_BOTTOM; position++) {
        for (unsigned bg3 = 0; bg3 < 2; bg3++) {
            set_field_line_window_scroll_h(g, window_scroll(position, bg3), 0);
            set_field_line_window_scroll_v(g, window_scroll(position, bg3), (uint16_t)window_map_scroll_v[position][bg3]);
        }
    }
    for (uint8_t set = 0; set < 2; set++) {
        set_field_line_window1_edges(g, set, vid_line_window_edges(WHOLE_LINE_LEFT, WHOLE_LINE_RIGHT));
        set_field_line_color_math_fixed_color(g, color_math(set, true), vid_line_fixed_color(ALL_CHANNELS, 0));
    }
    set_field_line_screens(g, SCREENS_OUTSIDE, bytes4(field_main_screen(g), field_sub_screen(g), 0, 0));
    set_field_line_screens(g, SCREENS_WINDOW_BODY, bytes4(SCREENS_WINDOW_BODY_LAYERS, 0, 0, 0));
    set_field_line_screens(g, SCREENS_WINDOW_EDGE, bytes4(SCREENS_WINDOW_EDGE_LAYERS, 0, 0, 0));
    set_dialog_window_state(g, 0);
    fill_dialog_shading(g);
}

// $FD:C1EE
/* Program the field's eight line effects from the set field_display_flags
 * picks: the layers' tile map addresses, BG1-BG3 scroll, the
 * screens and window masks, window 1's edges, the color math layers with the
 * fixed color, and one edge of window 2 -- its left one, or its right one while
 * field_window2_edge bit 0 is set. The effects are turned on elsewhere; the
 * field NMI copies the enable shadow field_line_effect_enable every frame. */
uint8_t field_set_up_line_effects(GameState *g, Platform *pf)
{
    static const VidLineTarget targets[SLOTS] = {
        [SLOT_BG_MAPS] = VID_LINE_BG_MAPS,       [SLOT_BG1_SCROLL] = VID_LINE_SCROLL_BG1,
        [SLOT_BG2_SCROLL] = VID_LINE_SCROLL_BG2, [SLOT_BG3_SCROLL] = VID_LINE_SCROLL_BG3,
        [SLOT_SCREENS] = VID_LINE_SCREENS,       [SLOT_WINDOW1] = VID_LINE_WINDOW1,
        [SLOT_COLOR_MATH] = VID_LINE_MATH_LAYERS, [SLOT_WINDOW2] = VID_LINE_WINDOW2_LEFT,
    };
    static const uint32_t sites[SLOTS] = { 0xFDC1F9, 0xFDC1FD, 0xFDC201, 0xFDC205,
                                           0xFDC209, 0xFDC20D, 0xFDC211, 0xFDC215 };
    bool right_edge = field_window2_edge(g) & 0x01;
    uint8_t set = (field_display_flags(g) & FIELD_TABLE_SET_BITS) != 0;
    for (uint8_t slot = 0; slot < SLOTS; slot++) {
        const VidLineEffect e = {
            .target = slot == SLOT_WINDOW2 && right_edge ? VID_LINE_WINDOW2_RIGHT : targets[slot],
            .table = PF_GAME(RAM_FIELD_LINE_EFFECT_TABLES + (uint32_t)(set * SLOTS + slot) * RAM_FIELD_LINE_EFFECT_TABLES_STRIDE),
            .indirect = true,
            .indirect_bank = RAM_FIELD_LINE_EFFECT_TABLES >> 16,
        };
        vid_set_line_slot(pf, PF_AT(sites[slot]), slot, &e);
    }
    return set;
}

/* ---- Where it meets the original ------------------------------------------------------
 *
 * The translation between the original's registers and the C, which a build
 * for the 65816 replaces with its own calling convention. The builders'
 * callers read what the original leaves in X, A, the carry and the overflow,
 * which follow from the tables' last writes; they are worked out here from
 * what the builders left. */

static void set_widths(Snes *s, bool m8, bool x8)
{
    s->cpu.p = (uint8_t)((s->cpu.p & ~(CPU_FLAG_M | CPU_FLAG_X)) | (m8 ? CPU_FLAG_M : 0) | (x8 ? CPU_FLAG_X : 0));
}

/* What sending the scroll leaves in the registers: X the last pointer of
 * window 2's table (or, where a layer waves, where the wave's copy ended), A
 * the color math layers with the last scroll's high byte ($FF after a wave),
 * the carry and overflow of the wave's last line's sum (or window 2's table
 * choice, which the sum starts from, and what came in). */
typedef struct {
    uint16_t x;
    uint8_t a_low, a_high;
    bool carry, overflow;
} Leaves;

static Leaves leaves_after_send(const GameState *g, uint8_t set, bool setup, uint16_t x, bool overflow)
{
    bool choice = window2_choice(g);
    if (setup)
        return (Leaves){ x, field_color_math_layers_override(g), (uint8_t)(field_auto_scroll_position_y(g, 1) >> 8),
                         choice, overflow };
    vars_peek(true);
    bool waved = a_layer_waves(g);
    vars_peek(false);
    if (!waved)
        return (Leaves){ x, field_fixed_color_math_layers(g), (uint8_t)(field_layer_scroll_v(g, FIELD_BG3) >> 8),
                         choice, overflow };
    /* The wave's last line: its offset and the scroll it was added to, and
     * the pixel the line before carried into it, the rest of its sum. */
    vars_peek(true);
    uint16_t a = field_wave_offset(g, (uint16_t)(wave_start(field_line_wave_phase(g)) + (WAVE_LINES - 1) * WAVE_ENTRY));
    uint16_t b = field_layer_scroll_h(g, FIELD_BG3);
    bool carried = (uint16_t)(field_line_wave_h(g, set * WAVE_SET_LINES + WAVE_LINES - 1) - a - b) != 0;
    vars_peek(false);
    return (Leaves){ wave_values(set, WAVE_LINES), field_fixed_color_math_layers(g), 0xFF,
                     (uint32_t)a + b + carried > 0xFFFF, field_add_with_carry_overflows16(a, b, carried) };
}

/* The same after a whole builder: the overflow in, which only a window's
 * lines change. */
static Leaves leaves_after_build(const GameState *g, uint8_t set, WindowPosition position, bool overflow)
{
    if (window_open(g, position)) {
        uint8_t half = dialog_window_state(g);
        overflow = position == WINDOW_AT_TOP ? field_subtract_overflows8(TOP_BELOW, half)
                                             : field_subtract_overflows8(BOTTOM_EDGE, half);
    }
    uint16_t last = (uint16_t)(window2_lines(g, set) + WHOLE_SCREEN_TOP);
    return leaves_after_send(g, set, !window_open(g, position) && overridden(g), last, overflow);
}

static void leave(Snes *s, Leaves l, uint8_t a_low)
{
    s->cpu.x = (uint8_t)l.x;
    s->cpu.y &= 0xFF;
    s->cpu.a = (uint16_t)(l.a_high << 8 | a_low);
    m_set_flag(s, CPU_FLAG_C, l.carry);
    m_set_flag(s, CPU_FLAG_V, l.overflow);
    set_widths(s, true, true);
}

/* The guards: the routines reach the field's page through the direct page and
 * the tables through the data bank $7F, which their callers set. $FD:C2C1
 * runs with the caller's data bank, through which the builders read
 * dialog_window_state once ($FD:C995), and dispatches on
 * dialog_window_position through a table of three; a position past it jumps
 * into bytes that aren't code, which stays the original's. */
enum { LINE_BANK = 0x7F };
bool field_line_effects_page_is_field(Snes *s) { return s->cpu.d == FIELD_PAGE; }
bool field_line_effects_bank_is_7f(Snes *s) { return s->cpu.db == LINE_BANK; }
bool field_line_effects_page_and_bank(Snes *s) { return s->cpu.d == FIELD_PAGE && s->cpu.db == LINE_BANK; }

/* Sending the scroll is entered with window 2's table choice in the carry,
 * which the port takes from the choice itself. */
bool field_line_effects_scroll_after_choice(Snes *s)
{
    if (!field_line_effects_page_and_bank(s))
        return false;
    vars_peek(true);
    bool ok = window2_choice(state_of(s)) == m_flag(s, CPU_FLAG_C);
    vars_peek(false);
    return ok;
}
bool field_line_effects_page_reaches_ram(Snes *s) { return field_on_page_low_bank(s); }
bool field_line_effects_can_build(Snes *s)
{
    if (!field_line_effects_page_reaches_ram(s))
        return false;
    vars_peek(true);
    bool ok = dialog_window_position(state_of(s)) < WINDOW_POSITIONS;
    vars_peek(false);
    return ok;
}

void field_build_line_effect_tables(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t set = set_to_build(g);
    WindowPosition position = dialog_window_at(g);
    build_line_effect_tables(g);
    leave(s, leaves_after_build(g, set, position, m_flag(s, CPU_FLAG_V)), 0x01);
    m_rtl(s);
}

/* The builders, each an entry of its own: they return with 8-bit index
 * registers, whatever they were entered with. */
static void builder_entry(Snes *s, uint8_t set, WindowPosition position)
{
    GameState *g = state_of(s);
    build_for(g, set, position);
    Leaves l = leaves_after_build(g, set, position, m_flag(s, CPU_FLAG_V));
    leave(s, l, l.a_low);
    m_rts(s);
}
void field_build_no_window_0(Snes *s) { builder_entry(s, 0, WINDOW_NONE); }
void field_build_no_window_1(Snes *s) { builder_entry(s, 1, WINDOW_NONE); }
void field_build_top_window_0(Snes *s) { builder_entry(s, 0, WINDOW_AT_TOP); }
void field_build_top_window_1(Snes *s) { builder_entry(s, 1, WINDOW_AT_TOP); }
void field_build_bottom_window_0(Snes *s) { builder_entry(s, 0, WINDOW_AT_BOTTOM); }
void field_build_bottom_window_1(Snes *s) { builder_entry(s, 1, WINDOW_AT_BOTTOM); }

/* The shading's block move leaves the overflow of its last run's end (and the
 * data bank its destination's, which only the fill hands back). */
static void after_shading(Snes *s)
{
    m_set_flag(s, CPU_FLAG_V, field_add_overflows8((uint8_t)(SHADED_LINES * 2), (uint8_t)(SHADING_STEP * 2)));
}

void field_init_line_effect_values(Snes *s)
{
    init_line_effect_values(state_of(s));
    after_shading(s);
    m_rtl(s);
}

void field_fill_dialog_shading(Snes *s)
{
    fill_dialog_shading(state_of(s));
    after_shading(s);
    s->cpu.db = LINE_BANK;
    m_rts(s);
}

static void window2_entry(Snes *s, uint8_t set)
{
    GameState *g = state_of(s);
    set_window2_table(g, set);
    s->cpu.x = (uint16_t)(window2_lines(g, set) + WHOLE_SCREEN_TOP);
    m_set_flag(s, CPU_FLAG_C, window2_choice(g));
    m_rts(s);
}
void field_set_window2_table_0(Snes *s) { window2_entry(s, 0); }
void field_set_window2_table_1(Snes *s) { window2_entry(s, 1); }

/* A's low byte is left the last count the table was written with. */
static void bg2_entry(Snes *s, uint8_t set)
{
    GameState *g = state_of(s);
    set_bg2_table(g, set);
    m_set_a8(s, !bg2_waves(g) ? TABLE_END : (uint8_t)(LINES_EACH | WAVE_LINES));
    m_rts(s);
}
void field_set_bg2_table_0(Snes *s) { bg2_entry(s, 0); }
void field_set_bg2_table_1(Snes *s) { bg2_entry(s, 1); }

/* Entered with window 2's table's last pointer in X. */
static void send_scroll_entry(Snes *s, uint8_t set)
{
    GameState *g = state_of(s);
    send_scroll(g, set);
    Leaves l = leaves_after_send(g, set, false, s->cpu.x, m_flag(s, CPU_FLAG_V));
    leave(s, l, field_fixed_color_math_layers(g));
    m_rts(s);
}
void field_send_scroll_0(Snes *s) { send_scroll_entry(s, 0); }
void field_send_scroll_1(Snes *s) { send_scroll_entry(s, 1); }

/* The wave's pieces, each an entry of its own, pass the wave's line on in
 * the cell the original counts it in (leave_wave_line). */
static uint8_t wave_line_kept(const GameState *g) { return (uint8_t)field_direction(g); }
static void keep_wave_line(GameState *g, uint8_t line)
{
    set_field_direction(g, (uint16_t)((field_direction(g) & 0xFF00) | line));
}

// $FD:CC58
void field_start_wave_table(Snes *s)
{
    set_field_direction(state_of(s), 0);
    s->cpu.y = 0;
    m_rts(s);
}

/* $FD:CC5E writes set 0's BG3 table, $FD:CCB8 set 1's; A is the lines and Y
 * the entry's offset in the table. A's high byte is left the last pointer's,
 * and its low byte the wave's line, or 0 where the lines ended with a whole
 * entry; $D9 the last entry's lines, the same way. */
static void wave_part_entry(Snes *s, uint8_t set)
{
    GameState *g = state_of(s);
    uint8_t lines = (uint8_t)s->cpu.a, line = wave_line_kept(g);
    TableWriter t = { table_of(set, SLOT_BG3_SCROLL).table, s->cpu.y };
    wave_part(g, &t, set, lines, &line);
    keep_wave_line(g, line);
    bool whole = lines >= WAVE_LINES && lines % WAVE_LINES == 0;
    /* where the last entry points: the values of the line it started at */
    uint8_t last_entry = lines == 0 ? 0 : whole ? WAVE_LINES : lines % WAVE_LINES;
    uint16_t values = wave_values(set, (uint8_t)(line - last_entry) & (WAVE_LINES - 1));
    original_cell8(g, 0xD9, whole ? 0 : lines % WAVE_LINES);
    s->cpu.y = t.next;
    s->cpu.a = (uint16_t)((values & 0xFF00) | (whole ? 0 : line));
    m_rts(s);
}
void field_put_wave_part_0(Snes *s) { wave_part_entry(s, 0); }
void field_put_wave_part_1(Snes *s) { wave_part_entry(s, 1); }

// $FD:CCB2
/* Lines that don't wave (the window band) still move the wave's line on. */
void field_skip_wave_lines(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t line = (uint8_t)(wave_line_kept(g) + (uint8_t)s->cpu.a);
    keep_wave_line(g, line);
    m_set_a8(s, line);
    m_rts(s);
}

/* The original reaches the transfer registers through the direct page, which
 * it loads through A and restores; X keeps the last table it programmed. */
void field_setup_line_effects(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t set = field_set_up_line_effects(g, m_platform(s));
    s->cpu.x = RAM_ABS(RAM_FIELD_LINE_EFFECT_TABLES + (uint32_t)(set * SLOTS + SLOT_WINDOW2) * RAM_FIELD_LINE_EFFECT_TABLES_STRIDE);
    s->cpu.a = (uint16_t)(M_DP_DMA | (field_display_flags(g) & FIELD_TABLE_SET_BITS));
    m_rtl(s);
}
