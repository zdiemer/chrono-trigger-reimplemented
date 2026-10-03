/* Battle: the screen script's window commands.
 *
 * The screen script's window commands shape the windows' line table
 * (battle/screen_lines.h): a band that opens about the first animation
 * position, a diagonal wipe, waves down both windows, per-line nudges, a band
 * about the middle of the battlefield, copies between the windows and plain
 * fills. A few of them (the waves, the wipe) always write the first buffer,
 * and start a line early, in the border. */
#include "battle/screen_script.h"

#include "battle/screen_data.h"
#include "battle/screen_lines.h"
#include "charge.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "port/native.h"

/* Two counts the effects keep between calls, each the one effect's that
 * runs at a time: the first is the band's half width, the waves' damping so
 * far, or the wipe's calls; the second the band's half height or the counted
 * wave's calls. The wipe counts its calls as a word over both. */
enum { FIRST_COUNT, SECOND_COUNT };

static uint8_t band_half_width(const GameState *g) { return battle_window_steps(g, FIRST_COUNT); }
static void set_band_half_width(GameState *g, uint8_t v) { set_battle_window_steps(g, FIRST_COUNT, v); }
static uint8_t band_half_height(const GameState *g) { return battle_window_steps(g, SECOND_COUNT); }
static void set_band_half_height(GameState *g, uint8_t v) { set_battle_window_steps(g, SECOND_COUNT, v); }
static uint8_t wave_damping(const GameState *g) { return battle_window_steps(g, FIRST_COUNT); }
static void set_wave_damping(GameState *g, uint8_t v) { set_battle_window_steps(g, FIRST_COUNT, v); }
static uint8_t counted_wave_calls(const GameState *g) { return battle_window_steps(g, SECOND_COUNT); }
static void set_counted_wave_calls(GameState *g, uint8_t v) { set_battle_window_steps(g, SECOND_COUNT, v); }
static uint16_t wipe_calls(const GameState *g)
{
    return (uint16_t)(battle_window_steps(g, FIRST_COUNT) | battle_window_steps(g, SECOND_COUNT) << 8);
}
static void set_wipe_calls(GameState *g, uint16_t v)
{
    set_battle_window_steps(g, FIRST_COUNT, (uint8_t)v);
    set_battle_window_steps(g, SECOND_COUNT, (uint8_t)(v >> 8));
}

/* ---- Bands ------------------------------------------------------------------------------------- */

/* The first animation position, the band's center. */
static uint8_t position_across(const GameState *g) { return (uint8_t)battle_animation_positions(g, 0); }
static uint8_t position_down(const GameState *g) { return (uint8_t)(battle_animation_positions(g, 0) >> 8); }

enum { BAND_WIDEST = 8, BAND_TALLEST = 9 };

/* The original works out the band's edges in two cells, and leaves the end
 * of its lines there after a band down. */
static void leave_band(GameState *g, uint8_t low, uint8_t high)
{
    screen_leave_cell(g, 0x45, low);
    screen_leave_cell(g, 0x46, high);
}

// $D1:EDCD
/* Screen command $EE: open a band about the first animation position, a step
 * wider each frame: across (operand 0), window 1 on every line of the
 * battlefield covers the columns within the band's half width of the
 * position, up to 8; otherwise window 2 covers whole lines within its half
 * height, up to 9, down to the battlefield's last line. */
static void open_band_across(GameState *g)
{
    uint8_t center = position_across(g), half = band_half_width(g);
    uint8_t left = center >= half ? (uint8_t)(center - half) : 0;
    uint8_t right = center + half > 0xFF ? 0xFF : (uint8_t)(center + half);
    if (half < BAND_WIDEST)
        set_band_half_width(g, (uint8_t)(half + 1));
    for (unsigned line = 0; line < BATTLEFIELD_LINES; line++)
        set_line_edges(g, coming_line(g, line), WINDOW_1, edges_between(left, right));
    ORIGINAL_EFFECT(leave_band(g, left, right));
}

static void open_band_down(GameState *g)
{
    uint8_t center = position_down(g), half = band_half_height(g);
    uint8_t top = center >= half ? (uint8_t)(center - half) : 0;
    uint8_t bottom = center + half >= BATTLEFIELD_LINES ? BATTLEFIELD_LINES : (uint8_t)(center + half);
    if (half < BAND_TALLEST)
        set_band_half_height(g, (uint8_t)(half + 1));
    for (unsigned line = top; line < bottom; line++)
        set_line_edges(g, coming_line(g, line), WINDOW_2, EDGES_FULL);
    ORIGINAL_EFFECT(screen_leave_word(g, 0x45, (uint16_t)(bottom * LINE_BYTES)));
}

static void open_band(GameState *g, bool across)
{
    if (across)
        open_band_across(g);
    else
        open_band_down(g);
}

enum { BAND_MIDDLE_LINE = 0x6A };

// $D1:FB68
/* Screen command $E9: move the middle band's left edge by the operand (its
 * right edge mirrors it). */
static void move_middle_band(GameState *g, uint8_t by)
{
    set_battle_column_band_left(g, (uint8_t)(battle_column_band_left(g) + by));
}

// $D1:FB72
/* Screen command $E8: grow the middle band's half height by the operand and
 * draw it: window 1 on the lines within its half height of the battlefield's
 * middle line covers the columns from its left edge to as far from the right
 * of the screen. */
static void draw_middle_band(GameState *g, uint8_t grow_by)
{
    uint8_t half = (uint8_t)(battle_column_band_half_height(g) + grow_by);
    set_battle_column_band_half_height(g, half);
    uint8_t left = battle_column_band_left(g), right = (uint8_t)~left;
    uint8_t top = (uint8_t)(BAND_MIDDLE_LINE - half), bottom = (uint8_t)(BAND_MIDDLE_LINE + half);
    for (unsigned line = top; line < bottom; line++)
        set_line_edges(g, coming_line(g, line), WINDOW_1, edges_between(left, right));
    ORIGINAL_EFFECT(screen_leave_word(g, 0x45, (uint16_t)(bottom * LINE_BYTES)));
    ORIGINAL_EFFECT(screen_leave_word(g, 0x49, edges_between(left, right)));
}

// $D1:EEA5
/* Screen command $EC: a diagonal wipe that climbs a line each call. On the
 * first buffer's lines from $BF less the calls so far up to the top border,
 * window 2's left edge steps a column right each line up, from 0, its right
 * edge at 0. Past $C0 calls the lines start below the buffer's end, in the
 * second buffer. The call count is a word across both step counts. */
enum { WIPE_FROM = 0xC0 };

static void wipe_diagonally(GameState *g)
{
    uint16_t calls = wipe_calls(g);
    uint8_t lowest = (uint8_t)(WIPE_FROM - calls);
    for (unsigned n = 0; n <= lowest; n++)
        set_line_edges(g, (TableLine)lowest - 1 - (TableLine)n, WINDOW_2, edges_between((uint8_t)n, 0));
    set_wipe_calls(g, (uint16_t)(calls + 1));
}

/* ---- Waves -------------------------------------------------------------------------------------- */

/* The waves shape both windows of the first buffer down 212 lines, a pair of
 * lines at a time (the first pair starting in the border): each window's
 * edges, as one word, are its base moved by its amplitude times the sine of
 * its phase, and the phases step once a pair. As the edges move as a word,
 * the left edge waves and the right one moves only when the left wraps. */
enum { WAVE_PAIRS = BATTLE_WAVE_EDGES_COUNT };

typedef struct {
    uint8_t phase[2], amplitude[2];   /* by window */
    Edges base[2];
    bool base1_per_pair;              /* window 1's base from battle_wave_edges, pair by pair */
} Waves;

/* A wave's offset: its amplitude times the sine of the phase, in whole
 * columns, taken as a signed byte. */
static int16_t wave_offset(const GameState *g, uint8_t phase, uint8_t amplitude)
{
    return (int8_t)screen_scaled_sine(g, phase, amplitude);
}

static void set_pair_edges(GameState *g, unsigned pair, Window w, Edges e)
{
    set_line_edges(g, 2 * (TableLine)pair - 1, w, e);
    set_line_edges(g, 2 * (TableLine)pair, w, e);
}

static Edges waved(Edges base, int16_t offset) { return (Edges)(base + offset); }

/* What the waves leave in their cells, and the multiplier its last product:
 * the phases where they stopped, the amplitudes and the bases. */
static void leave_waves(GameState *g, const Waves *w)
{
    screen_leave_word(g, 0x45, w->phase[WINDOW_2]);
    screen_leave_word(g, 0x47, w->phase[WINDOW_1]);
    screen_leave_cell(g, 0x4B, w->amplitude[WINDOW_1]);
    screen_leave_cell(g, 0x4D, w->amplitude[WINDOW_2]);
    screen_leave_word(g, 0x4F, w->base[WINDOW_1]);
    screen_leave_word(g, 0x51, w->base[WINDOW_2]);
    hw_set_multiplicand(g, battle_sine_magnitude(g, (uint8_t)(w->phase[WINDOW_2] - 1)));
    hw_multiply_by(g, w->amplitude[WINDOW_2]);
}

static void draw_waves(GameState *g, Waves w)
{
    for (unsigned pair = 0; pair < WAVE_PAIRS; pair++) {
        Edges base1 = w.base1_per_pair ? battle_wave_edges(g, pair) : w.base[WINDOW_1];
        set_pair_edges(g, pair, WINDOW_1, waved(base1, wave_offset(g, w.phase[WINDOW_1], w.amplitude[WINDOW_1])));
        set_pair_edges(g, pair, WINDOW_2,
                       waved(w.base[WINDOW_2], wave_offset(g, w.phase[WINDOW_2], w.amplitude[WINDOW_2])));
        w.phase[WINDOW_1]++;
        w.phase[WINDOW_2]++;
    }
    ORIGINAL_EFFECT(leave_waves(g, &w));
}

/* The waves die down as their operands add up: the amplitude is $60 less the
 * sum so far. */
enum { WAVE_AMPLITUDE = 0x60 };

static uint8_t damped_amplitude(GameState *g, uint8_t damping)
{
    uint8_t damped = (uint8_t)(wave_damping(g) + damping);
    set_wave_damping(g, damped);
    return (uint8_t)(WAVE_AMPLITUDE - damped);
}

/* The bases: channel 0's layer position and offset, across for window 1 and
 * down for window 2. */
static void bases_from_layer(const GameState *g, Waves *w)
{
    w->base[WINDOW_1] = (Edges)(battle_screen_layer_x(g, 0) + battle_screen_layer_offset_x(g, 0));
    w->base[WINDOW_2] = (Edges)(battle_screen_layer_y(g, 0) + battle_screen_layer_offset_y(g, 0));
}

/* Phases from the frame count: window 1's moves two steps a frame, window
 * 2's four. */
static void phases_from_frame(const GameState *g, Waves *w)
{
    uint8_t frame = (uint8_t)battle_frame_count(g);
    w->phase[WINDOW_1] = (uint8_t)(frame << 1);
    w->phase[WINDOW_2] = (uint8_t)(frame << 2);
}

// $D1:EEC4
/* Screen command $ED: waves on window 1 about the per-line edges of
 * battle_wave_edges, and on window 2 about the layer's vertical position at
 * half the amplitude, flowing with the frame count. The operand damps them. */
static void wave_about_edges(GameState *g, uint8_t damping)
{
    Waves w = { .base1_per_pair = true };
    w.amplitude[WINDOW_1] = damped_amplitude(g, damping);
    w.amplitude[WINDOW_2] = w.amplitude[WINDOW_1] >> 1;
    phases_from_frame(g, &w);
    bases_from_layer(g, &w);
    draw_waves(g, w);
}

/* Both windows waving about the layer's position with one amplitude. */
static void wave_about_layer(GameState *g, uint8_t damping, Waves w)
{
    w.amplitude[WINDOW_1] = w.amplitude[WINDOW_2] = damped_amplitude(g, damping);
    bases_from_layer(g, &w);
    draw_waves(g, w);
}

/* The counted wave's first phase: its calls so far, from $A8, two steps a
 * call. */
enum { COUNTED_WAVE_START = 0xA8 };

// $D1:EFD0
/* Screen command $F3: waves about the layer's position, flowing with a count
 * of the command's calls rather than the frames. */
static void wave_counted(GameState *g, uint8_t damping)
{
    uint8_t calls = counted_wave_calls(g);
    Waves w = { 0 };
    w.phase[WINDOW_1] = (uint8_t)((uint8_t)(calls + COUNTED_WAVE_START) << 1);
    w.phase[WINDOW_2] = (uint8_t)(w.phase[WINDOW_1] << 1);
    set_counted_wave_calls(g, (uint8_t)(calls + 1));
    wave_about_layer(g, damping, w);
}

// $D1:EFE3
/* Screen command $EB: the same, flowing with the frame count. */
static void wave_with_frames(GameState *g, uint8_t damping)
{
    Waves w = { 0 };
    phases_from_frame(g, &w);
    wave_about_layer(g, damping, w);
}

/* ---- Nudges and copies ----------------------------------------------------------------------- */

/* The lines the nudges move: two runs of 64, from line 48 and from line 112. */
enum { NUDGED_LINES = 64, FIRST_NUDGED = 48, SECOND_NUDGED = 112 };

/* Window 2's nudges are a second table, which starts at another place on odd
 * frames than on even ones; both overlap window 1's. */
enum { WINDOW_2_NUDGES_ODD = 0x40, WINDOW_2_NUDGES_EVEN = 0x20 };

typedef enum { W1_LEFT, W1_RIGHT, W2_LEFT, W2_RIGHT, LINE_EDGES } Edge;

static uint8_t edge(const GameState *g, TableLine line, Edge e)
{
    switch (e) {
    case W1_LEFT: return battle_window_lines_w1_left(g, line);
    case W1_RIGHT: return battle_window_lines_w1_right(g, line);
    case W2_LEFT: return battle_window_lines_w2_left(g, line);
    default: return battle_window_lines_w2_right(g, line);
    }
}

static void set_edge(GameState *g, TableLine line, Edge e, uint8_t v)
{
    switch (e) {
    case W1_LEFT: set_battle_window_lines_w1_left(g, line, v); break;
    case W1_RIGHT: set_battle_window_lines_w1_right(g, line, v); break;
    case W2_LEFT: set_battle_window_lines_w2_left(g, line, v); break;
    default: set_battle_window_lines_w2_right(g, line, v); break;
    }
}

/* One edge nudged. The original adds the amounts as one long chain, each
 * addition carrying its overflow into the next, so an edge after one that
 * went past 255 moves one column further: with a negative amount (a byte
 * above $7F), nearly every edge but the first does. `spilled` is that
 * overflow, from the edge before and for the edge after. */
static uint8_t nudged(uint8_t at, uint8_t by, bool *spilled)
{
    unsigned moved = at + by + *spilled;
    *spilled = moved > 0xFF;
    return (uint8_t)moved;
}

// $D1:F108
/* Screen command $80, operation 13: nudge the edges of the coming frame's
 * lines 48-175 by the per-line amounts in battle_window_line_deltas, the
 * same amount on the two runs of lines, window 1's by the first table and
 * window 2's by the second. */
static void nudge_edges(GameState *g)
{
    uint8_t second_table = odd_frame(g) ? WINDOW_2_NUDGES_ODD : WINDOW_2_NUDGES_EVEN;
    const unsigned runs[2] = { FIRST_NUDGED, SECOND_NUDGED };
    bool spilled = false;
    for (unsigned n = 0; n < NUDGED_LINES; n++) {
        uint8_t by[2] = { battle_window_line_deltas(g, n), battle_window_line_deltas(g, second_table + n) };
        for (unsigned r = 0; r < 2; r++) {
            TableLine line = coming_line(g, runs[r] + n);
            for (Edge e = W1_LEFT; e < LINE_EDGES; e++)
                set_edge(g, line, e, nudged(edge(g, line, e), by[e >= W2_LEFT], &spilled));
        }
    }
}

// $D1:F5F5
/* Screen command $DB: the coming frame's window 2 takes window 1 as it is on
 * the screen now, on every line. */
static void copy_window_1_across(GameState *g)
{
    for (unsigned line = 0; line < SCREEN_LINES; line++)
        set_line_edges(g, coming_line(g, line), WINDOW_2, line_edges(g, shown_line(g, line), WINDOW_1));
}

/* Window 1's edges turned inside out: the left edge where the right one
 * stops, the right where the left starts. */
static Edges inside_out(Edges e) { return (Edges)(((uint16_t)~e & 0xFF) << 8 | (uint16_t)~e >> 8); }

// $D1:FD7E
/* Screen command $CF: as $DB, with window 1 turned inside out. The operand
 * shifts the lines taken, which the guard keeps to no shift. */
enum { NO_SHIFT = 0x7F };

static void copy_window_1_inside_out(GameState *g)
{
    for (unsigned line = 0; line < SCREEN_LINES; line++)
        set_line_edges(g, coming_line(g, line), WINDOW_2, inside_out(line_edges(g, shown_line(g, line), WINDOW_1)));
    /* the original copies the lines in eight blocks of 28, and leaves the end
     * of a block's in its cell */
    ORIGINAL_EFFECT(screen_leave_word(g, 0x45, SCREEN_LINES / 8 * LINE_BYTES));
}

/* ---- Fills --------------------------------------------------------------------------------------- */

typedef enum { BOTH_EMPTY, WINDOW_1_EMPTY, WINDOW_2_EMPTY, WINDOW_1_FULL, FILLS } Fill;

static const struct {
    Edges edges;
    bool window_1, window_2;
} fills[FILLS] = {
    [BOTH_EMPTY] = { EDGES_EMPTY, true, true },
    [WINDOW_1_EMPTY] = { EDGES_EMPTY, true, false },
    [WINDOW_2_EMPTY] = { EDGES_EMPTY, false, true },
    [WINDOW_1_FULL] = { EDGES_FULL, true, false },
};

// $D1:FCF4 $D1:FC38 $D1:FC96 $D1:FBDA
/* Empty or fill the coming frame's windows on every line. */
static void fill_windows(GameState *g, Fill f)
{
    for (unsigned line = 0; line < SCREEN_LINES; line++) {
        if (fills[f].window_1)
            set_line_edges(g, coming_line(g, line), WINDOW_1, fills[f].edges);
        if (fills[f].window_2)
            set_line_edges(g, coming_line(g, line), WINDOW_2, fills[f].edges);
    }
}

/* ---- The ripple ----------------------------------------------------------------------------------- */

/* The ripple's two waves: window 1's, 50 pixels deep, its phase four steps
 * on each frame; window 2's, 15 deep and a step on, about 32 below channel
 * 0's layer. */
enum { RIPPLE_PAIRS = 106, RIPPLE_DEPTH = 0x32, RIPPLE_SPEED = 4, SWELL_DEPTH = 0x0F, SWELL_BELOW = 0x20 };

static uint8_t wave_at(const GameState *g, uint8_t phase, uint8_t depth)
{
    uint8_t v = (uint8_t)(battle_sine_magnitude(g, phase) * depth >> 8);
    return (phase & SCREEN_HALF_TURN) ? (uint8_t)-v : v;
}

static Edges widened(uint8_t v) { return (Edges)(int16_t)(int8_t)v; }

static void leave_ripple(GameState *g, uint8_t frame, uint16_t base)
{
    screen_leave_word(g, 0x45, (uint8_t)(frame + RIPPLE_PAIRS));
    screen_leave_word(g, 0x47, (uint8_t)(frame * RIPPLE_SPEED + RIPPLE_PAIRS));
    screen_leave_word(g, 0x49, base);
    hw_set_multiplicand(g, battle_sine_magnitude(g, (uint8_t)(frame + RIPPLE_PAIRS - 1)));
    hw_multiply_by(g, SWELL_DEPTH);
}

// $CD:1FC8
/* Screen command $B3: ripple both windows down the first buffer's lines, a
 * pair at a time from the border's last: window 1 from the ripple's offset to
 * the screen's edge on its side (its edges the offset as a signed word), and
 * window 2 at channel 0's layer's height, the swell's offset and 32 below
 * it. */
static void ripple_windows(GameState *g)
{
    uint8_t frame = (uint8_t)battle_frame_count(g);
    uint16_t base = (uint16_t)(battle_screen_layer_y(g, 0) + battle_screen_layer_offset_y(g, 0));
    for (unsigned pair = 0; pair < RIPPLE_PAIRS; pair++) {
        Edges w1 = widened(wave_at(g, (uint8_t)(frame * RIPPLE_SPEED + pair), RIPPLE_DEPTH));
        Edges w2 = (Edges)(widened((uint8_t)(wave_at(g, (uint8_t)(frame + pair), SWELL_DEPTH) + SWELL_BELOW)) + base);
        for (TableLine line = 2 * (TableLine)pair - 1; line <= 2 * (TableLine)pair; line++) {
            set_line_edges(g, line, WINDOW_1, w1);
            set_line_edges(g, line, WINDOW_2, w2);
        }
    }
    ORIGINAL_EFFECT(leave_ripple(g, frame, base));
}

/* ---- The commands' rows (battle/screen_script.h) --------------------------------------------------- */

static ScreenFlow ripple(GameState *g)
{
    ripple_windows(g);
    return screen_on();
}
SCREEN_OPERATION_TAKING_NOTHING(ripple)

static ScreenFlow open_the_band(GameState *g, uint8_t value)
{
    open_band(g, value == 0);
    return screen_on();
}
SCREEN_OPERATION(open_the_band, value)

static bool band_fits(const GameState *g, const ScreenCommand *c)
{
    return c->value == 0 || position_down(g) <= BATTLEFIELD_LINES + band_half_height(g);
}

static ScreenFlow wipe(GameState *g)
{
    wipe_diagonally(g);
    return screen_on();
}
SCREEN_OPERATION_TAKING_NOTHING(wipe)

/* The waves, about the window's own edges, by a count of calls, and by the
 * frames: each damped by the operand. */
typedef enum { WAVE_ABOUT_EDGES, WAVE_COUNTED, WAVE_WITH_FRAMES } WaveKind;

static ScreenFlow wave(GameState *g, uint8_t kind, uint8_t amount)
{
    switch ((WaveKind)kind) {
    case WAVE_ABOUT_EDGES: wave_about_edges(g, amount); break;
    case WAVE_COUNTED: wave_counted(g, amount); break;
    case WAVE_WITH_FRAMES: wave_with_frames(g, amount); break;
    }
    return screen_on();
}
SCREEN_OPERATION(wave, kind, amount)

static ScreenFlow move_band(GameState *g, uint8_t by)
{
    move_middle_band(g, by);
    return screen_on();
}
SCREEN_OPERATION(move_band, by)

static ScreenFlow draw_band(GameState *g, uint8_t by)
{
    draw_middle_band(g, by);
    return screen_on();
}
SCREEN_OPERATION(draw_band, by)

static bool middle_band_fits(const GameState *g, const ScreenCommand *c)
{
    return (uint8_t)(battle_column_band_half_height(g) + c->by) <= BAND_MIDDLE_LINE;
}

static ScreenFlow copy_across(GameState *g)
{
    copy_window_1_across(g);
    return screen_on();
}
SCREEN_OPERATION_TAKING_NOTHING(copy_across)

static ScreenFlow copy_inside_out(GameState *g)
{
    copy_window_1_inside_out(g);
    return screen_on();
}
SCREEN_OPERATION_TAKING_NOTHING(copy_inside_out)

static bool unshifted(const GameState *g, const ScreenCommand *c)
{
    (void)g;
    return (c->value & NO_SHIFT) == 0;
}

/* Each fill's work is timed as its own routine's. */
static ScreenFlow fill(GameState *g, uint8_t fill)
{
    switch ((Fill)fill) {
    case BOTH_EMPTY: TIMED_AS(g, 0xD1FCF4, fill_windows(g, BOTH_EMPTY)); break;
    case WINDOW_1_EMPTY: TIMED_AS(g, 0xD1FC38, fill_windows(g, WINDOW_1_EMPTY)); break;
    case WINDOW_2_EMPTY: TIMED_AS(g, 0xD1FC96, fill_windows(g, WINDOW_2_EMPTY)); break;
    default: TIMED_AS(g, 0xD1FBDA, fill_windows(g, WINDOW_1_FULL)); break;
    }
    return screen_on();
}
SCREEN_OPERATION(fill, fill)


SCREEN_OPCODES(screen_window_opcodes,
    { 0xB3, OPERATION(ripple), .length = 1 },
    { 0xEE, OPERATION(open_the_band), .needs = band_fits, .length = 1, .value = BYTE(0) },
    { 0xEC, OPERATION(wipe), .length = 1 },
    { 0xED, OPERATION(wave), .length = 1, .kind = CONSTANT(WAVE_ABOUT_EDGES), .amount = BYTE(0) },
    { 0xF3, OPERATION(wave), .length = 1, .kind = CONSTANT(WAVE_COUNTED), .amount = BYTE(0) },
    { 0xEB, OPERATION(wave), .length = 1, .kind = CONSTANT(WAVE_WITH_FRAMES), .amount = BYTE(0) },
    { 0xE9, OPERATION(move_band), .length = 1, .by = BYTE(0) },
    { 0xE8, OPERATION(draw_band), .needs = middle_band_fits, .length = 1, .by = BYTE(0) },
    { 0xDB, OPERATION(copy_across), .length = 1 },
    { 0xCF, OPERATION(copy_inside_out), .needs = unshifted, .length = 1, .value = BYTE(0) },
    { 0x88, OPERATION(fill), .length = 1, .fill = LOW_SEVEN(0) },
);

/* ---- The entries ------------------------------------------------------------------------ */

/* The guards. The band down stays above the position's reach of the
 * battlefield's end, and the middle band within the battlefield's top half,
 * or the original's line loop runs round all of work RAM; the inside-out copy
 * takes the lines as they are (an operand of 0 or $80); the fill is one of
 * four; the wave's line pair is one of the 106. None of the others' reaches a
 * game makes. */
bool battle_screen_band_fits(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool fits = screen_command_operand(g, 0) == 0 || position_down(g) <= BATTLEFIELD_LINES + band_half_height(g);
    vars_peek(false);
    return battle_screen_page_a_clear(s) && fits;
}

bool battle_screen_middle_band_fits(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool fits = (uint8_t)(battle_column_band_half_height(g) + screen_command_operand(g, 0)) <= BAND_MIDDLE_LINE;
    vars_peek(false);
    return battle_screen_page_a_clear(s) && fits;
}

bool battle_screen_copy_unshifted(Snes *s)
{
    vars_peek(true);
    bool unshifted = (screen_command_operand(state_of(s), 0) & NO_SHIFT) == 0;
    vars_peek(false);
    return battle_screen_page_a_clear(s) && unshifted;
}

bool battle_screen_fill_is_one(Snes *s) { return battle_screen_page_a_clear(s) && s->cpu.x < 2 * FILLS && s->cpu.x % 2 == 0; }

enum { PAIR_BYTES = 2 * LINE_BYTES };

bool battle_wave_pair_is_one(Snes *s)
{
    return battle_screen_page_a_clear(s) && s->cpu.y % PAIR_BYTES == 0 && s->cpu.y / PAIR_BYTES < WAVE_PAIRS;
}

void battle_screen_open_band(Snes *s)
{
    GameState *g = state_of(s);
    open_band(g, screen_command_operand(g, 0) == 0);
    screen_command_done(s);
}

void battle_screen_wipe_from_left(Snes *s)
{
    wipe_diagonally(state_of(s));
    screen_command_done(s);
}

void battle_screen_wave_windows(Snes *s)
{
    GameState *g = state_of(s);
    wave_about_edges(g, screen_command_operand(g, 0));
    screen_command_done(s);
}

void battle_screen_wave_counted(Snes *s)
{
    GameState *g = state_of(s);
    wave_counted(g, screen_command_operand(g, 0));
    screen_command_done(s);
}

void battle_screen_wave_framed(Snes *s)
{
    GameState *g = state_of(s);
    wave_with_frames(g, screen_command_operand(g, 0));
    screen_command_done(s);
}

/* $D1:EF63, $D1:F07E and $D1:F0E9, the waves' steps as the original calls
 * them for each pair of lines: the offset in A, taken as negative by the N
 * flag its making left; the pair in Y; the phases, amplitudes and bases in
 * the cells the waves leave them in. The first two go on to start window 2's
 * product in the multiplier, and return the carry set for window 2's phase in
 * the second half of the turn. */
static int16_t offset_in_a(const Snes *s)
{
    uint8_t v = (uint8_t)s->cpu.a;
    return (s->cpu.p & CPU_FLAG_N) ? (int16_t)(v - 0x100) : (int16_t)v;
}

static unsigned pair_in_y(const Snes *s) { return s->cpu.y / PAIR_BYTES; }


static void hand_on_to_window_2(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t phase = screen_cell(g, 0x45);
    hw_set_multiplicand(g, battle_sine_magnitude(g, phase));
    hw_multiply_by(g, screen_cell(g, 0x4D));
    s->cpu.a = 0;
    m_set_flag(s, CPU_FLAG_C, phase & SCREEN_HALF_TURN);
    m_rts(s);
}

void battle_wave_w1_from_table(Snes *s)
{
    GameState *g = state_of(s);
    unsigned pair = pair_in_y(s);
    set_pair_edges(g, pair, WINDOW_1, waved(battle_wave_edges(g, pair), offset_in_a(s)));
    hand_on_to_window_2(s);
}

void battle_wave_w1(Snes *s)
{
    GameState *g = state_of(s);
    set_pair_edges(g, pair_in_y(s), WINDOW_1, waved(screen_cell_word(g, 0x4F), offset_in_a(s)));
    hand_on_to_window_2(s);
}

void battle_wave_w2(Snes *s)
{
    GameState *g = state_of(s);
    set_pair_edges(g, pair_in_y(s), WINDOW_2, waved(screen_cell_word(g, 0x51), offset_in_a(s)));
    m_rts(s);
}

/* The nudges' guard has the carry clear, as the operation's dispatch leaves
 * it. */
bool battle_screen_nudge_dispatched(Snes *s) { return battle_screen_page_a_clear(s) && !m_flag(s, CPU_FLAG_C); }

void battle_screen_add_line_deltas(Snes *s)
{
    nudge_edges(state_of(s));
    s->cpu.a = 0;
    m_rtl(s);
}

void battle_screen_copy_window1_to_window2(Snes *s)
{
    copy_window_1_across(state_of(s));
    screen_command_done(s);
}

void battle_screen_copy_window1_inverted(Snes *s)
{
    copy_window_1_inside_out(state_of(s));
    screen_command_done(s);
}

void battle_screen_move_column_band(Snes *s)
{
    GameState *g = state_of(s);
    move_middle_band(g, screen_command_operand(g, 0));
    screen_command_done(s);
}

void battle_screen_draw_column_band(Snes *s)
{
    GameState *g = state_of(s);
    draw_middle_band(g, screen_command_operand(g, 0));
    screen_command_done(s);
}

/* A fill returns with its edges in A, which its callers read the high byte
 * of. */
static void filled(Snes *s, Fill f)
{
    fill_windows(state_of(s), f);
    s->cpu.a = fills[f].edges;
}

// $D1:FBCE
/* Screen command $88: fill the windows as the operand says (X, doubled),
 * each fill's work timed as its own routine's. */
void battle_screen_fill_windows(Snes *s)
{
    switch ((Fill)(s->cpu.x / 2)) {
    case BOTH_EMPTY: NATIVE_TIME_AS(s, 0xD1FCF4, filled(s, BOTH_EMPTY)); break;
    case WINDOW_1_EMPTY: NATIVE_TIME_AS(s, 0xD1FC38, filled(s, WINDOW_1_EMPTY)); break;
    case WINDOW_2_EMPTY: NATIVE_TIME_AS(s, 0xD1FC96, filled(s, WINDOW_2_EMPTY)); break;
    default: NATIVE_TIME_AS(s, 0xD1FBDA, filled(s, WINDOW_1_FULL)); break;
    }
    m_rtl(s);
}

void battle_fill_windows_empty(Snes *s)
{
    filled(s, BOTH_EMPTY);
    m_rts(s);
}

void battle_fill_window1_empty(Snes *s)
{
    filled(s, WINDOW_1_EMPTY);
    m_rts(s);
}

void battle_fill_window2_empty(Snes *s)
{
    filled(s, WINDOW_2_EMPTY);
    m_rts(s);
}

void battle_fill_window1_full(Snes *s)
{
    filled(s, WINDOW_1_FULL);
    m_rts(s);
}
