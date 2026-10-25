/* The sprite-object mode's objects that change the screen rather than move on
 * it: the screen's fades, the mosaic, the palette's loads and fades, and the
 * tile streamer.
 *
 * A script starts each with a command (menu/object_script.c) and the new
 * object takes on the starter's state, the script's place included, so it
 * reads its parameters from the command that started it. Each then runs a
 * frame at a time, its handler called by the objects' loop (menu/objects.c),
 * until it says it is done:
 *
 * - The fades take the screen's brightness a step down to black, or up from
 *   black to full, every so many frames; the screen is turned off as a fade
 *   out ends and on as a fade in begins. One fade of each kind runs at a time
 *   (menu_fades_running), and a fade out stops a fade in.
 * - The mosaic grows, the screen breaking into ever larger blocks on the
 *   layers the command names, or clears, a size a step.
 * - The palette loader puts a row of sixteen colours into the palette at
 *   once, or hands over to a palette fade, which takes the row's colours to
 *   the new ones over 32 steps. A colour's red, green and blue each move a
 *   unit a step: a part that falls does it on the fade's first steps, one
 *   that rises on its last. While a row is loaded or faded it is counted as
 *   in use (menu_palette_row_users), and a fade that finds another load on
 *   its row gives the row up to it.
 * - The tile streamer sends tiles to VRAM a batch a frame through the mode's
 *   upload queue, so a large set of tiles doesn't hold up a frame.
 *
 * The palette the loads and fades change is the mode's image of CGRAM
 * (menu_object_palette), which the vblank handler sends when asked. */
#include "menu/screen_objects.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/menu_data.h"
#include "menu/objects.h"
#include "menu/vram.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

/* ---- The screen ------------------------------------------------------------------------------ */

enum { SCREEN_OFF = 0x80, SCREEN_ON = 0x00 };
enum { BLACK = 0, FULL_BRIGHTNESS = 15 };

bool menu_screen_is_off(const GameState *g) { return (menu_object_display_blank(g) & SCREEN_OFF) != 0; }

uint8_t menu_screen_display(const GameState *g)
{
    return (uint8_t)(menu_object_display_blank(g) | menu_object_display_brightness(g));
}

static void turn_the_screen(GameState *g, bool on) { set_menu_object_display_blank(g, on ? SCREEN_ON : SCREEN_OFF); }
static uint8_t brightness(const GameState *g) { return menu_object_display_brightness(g); }
static void set_brightness(GameState *g, uint8_t b) { set_menu_object_display_brightness(g, b); }

/* The frame's state the vblank handler goes by: a frame finished to show, and
 * the palette to send with it. */
enum { PALETTE_TO_SEND = 0x02 };

void menu_send_the_palette(GameState *g)
{
    set_menu_object_frame_state(g, (uint8_t)(menu_object_frame_state(g) | PALETTE_TO_SEND));
}

/* ---- What started the object ----------------------------------------------------------------- */

/* An operand of the command that started the object: the object's script is
 * still at that command. */
static uint32_t starting_command(const GameState *g, MenuObject self) { return menu_object_slots_script(g, self); }

static uint8_t operand(const GameState *g, MenuObject self, unsigned n)
{
    return menu_script_byte(g, (starting_command(g, self) + n) & 0xFFFFFFu);
}

static uint16_t operand_word(const GameState *g, MenuObject self, unsigned n)
{
    return (uint16_t)(operand(g, self, n) | operand(g, self, n + 1) << 8);
}

/* The original reads the command through a pointer in the mode's cells. */
static void leave_command_read(GameState *g, MenuObject self)
{
    set_menu_object_scratch_screen_command(g, starting_command(g, self));
}

/* The fades and the mosaic step every so many frames, which the objects'
 * loop counts as their age; a step starts the count again. */
static bool time_for_a_step(const GameState *g, MenuObject self, uint8_t frames_a_step)
{
    return menu_object_slots_frames_since_step(g, self) >= frames_a_step;
}

static void start_counting(GameState *g, MenuObject self) { set_menu_object_slots_frames_since_step(g, self, 0); }

/* ---- The fades ------------------------------------------------------------------------------- */

enum { FADING_IN = 0x01, FADING_OUT = 0x02 };

static bool fading(const GameState *g, uint8_t which) { return (menu_fades_running(g) & which) != 0; }

static void set_fading(GameState *g, uint8_t which, bool on)
{
    uint8_t running = menu_fades_running(g);
    set_menu_fades_running(g, (uint8_t)(on ? running | which : running & ~which));
}

/* The command gives the frames between steps; none is the whole fade at
 * once. */
static uint8_t frames_a_step(const GameState *g, MenuObject self) { return menu_object_slots_frames_a_step(g, self); }

enum { FADE_STARTS = 0, FADE_OUT_DIMS = 1, FADE_OUT_STEPS = 2 };

// $C2:20A2
/* Take the screen down to black a step every so many frames and turn it off.
 * A fade out already running keeps the screen to itself. Done at once, the
 * screen goes straight off, and the fade is still counted as running: the
 * original never clears it on that path, so no other fade out starts until a
 * fade out runs to its end. */
bool menu_fade_out(GameState *g, MenuObject self)
{
    if (menu_object_slots_step(g, self) == FADE_STARTS) {
        if (fading(g, FADING_OUT))
            return true;
        set_fading(g, FADING_OUT, true);
        set_menu_object_slots_step(g, self, FADE_OUT_DIMS);
        ORIGINAL_EFFECT(leave_command_read(g, self));
        uint8_t frames = operand(g, self, 1);
        if (frames == 0) {
            turn_the_screen(g, false);
            set_brightness(g, BLACK);
            return true;
        }
        set_menu_object_slots_frames_a_step(g, self, frames);
        start_counting(g, self);
    }
    if (!time_for_a_step(g, self, frames_a_step(g, self)))
        return false;
    start_counting(g, self);
    if (brightness(g) != BLACK) {
        set_brightness(g, (uint8_t)(brightness(g) - 1));
        if (brightness(g) != BLACK)
            return false;
    }
    turn_the_screen(g, false);
    set_fading(g, FADING_OUT, false);
    return true;
}

/* A fade in's steps: the first turns the screen on (unless it is already
 * lit), the rest brighten it to full. Done at once, it is full at once. */
enum { FADE_IN_TURNS_THE_SCREEN_ON = 1, FADE_IN_BRIGHTENS = 2, FADE_IN_ENDS = 3, FADE_IN_STEPS = 4 };

static bool stop_fading_in(GameState *g)
{
    set_fading(g, FADING_IN, false);
    return true;
}

// $C2:2105
/* Bring the screen up from black to full a step every so many frames. It
 * doesn't start while any fade runs, and a fade out stops it. */
bool menu_fade_in(GameState *g, MenuObject self)
{
    switch (menu_object_slots_step(g, self)) {
    case FADE_STARTS: {
        if (menu_fades_running(g) != 0)
            return stop_fading_in(g);
        set_fading(g, FADING_IN, true);
        ORIGINAL_EFFECT(leave_command_read(g, self));
        uint8_t frames = operand(g, self, 1);
        if (frames == 0) {
            turn_the_screen(g, true);
            set_brightness(g, FULL_BRIGHTNESS);
            set_menu_object_slots_step(g, self, FADE_IN_ENDS);
            return false;
        }
        set_menu_object_slots_frames_a_step(g, self, frames);
        set_menu_object_slots_step(g, self, brightness(g) == BLACK ? FADE_IN_TURNS_THE_SCREEN_ON : FADE_IN_BRIGHTENS);
        start_counting(g, self);
        return false;
    }
    case FADE_IN_TURNS_THE_SCREEN_ON:
        if (fading(g, FADING_OUT))
            return stop_fading_in(g);
        if (time_for_a_step(g, self, frames_a_step(g, self))) {
            set_menu_object_slots_step(g, self, FADE_IN_BRIGHTENS);
            start_counting(g, self);
            turn_the_screen(g, true);
            set_brightness(g, (uint8_t)(brightness(g) + 1));
        }
        return false;
    case FADE_IN_BRIGHTENS:
        if (fading(g, FADING_OUT))
            return stop_fading_in(g);
        if (time_for_a_step(g, self, frames_a_step(g, self))) {
            start_counting(g, self);
            set_brightness(g, (uint8_t)(brightness(g) + 1));
            if (brightness(g) == FULL_BRIGHTNESS)
                return stop_fading_in(g);
        }
        return false;
    default: /* FADE_IN_ENDS */
        return stop_fading_in(g);
    }
}

/* ---- The mosaic ------------------------------------------------------------------------------ */

/* The mode's mosaic: the layers it works on in the low nibble, the size of
 * its blocks less one in the high. */
enum { MOSAIC_LAYERS = 0x0F, MOSAIC_SIZE = 0xF0, MOSAIC_SIZE_STEP = 0x10, LARGEST_MOSAIC = 0xF0 };
enum { MOSAIC_STARTS = 0, MOSAIC_STEPS = 1 };

/* The command names the layers (added to those already in the mosaic) and
 * the frames between steps; with none, false: the change is made at once. */
static bool start_the_mosaic(GameState *g, MenuObject self)
{
    set_menu_object_slots_step(g, self, MOSAIC_STEPS);
    ORIGINAL_EFFECT(leave_command_read(g, self));
    set_menu_object_shadows_mosaic(g, (uint8_t)(menu_object_shadows_mosaic(g) | operand(g, self, 1)));
    uint8_t frames = operand(g, self, 2);
    if (frames == 0)
        return false;
    set_menu_object_slots_frames_a_step(g, self, frames);
    start_counting(g, self);
    return true;
}

// $C2:2194
/* Clear the mosaic a size a step, every so many frames, down to none. */
bool menu_clear_mosaic(GameState *g, MenuObject self)
{
    if (menu_object_slots_step(g, self) == MOSAIC_STARTS && !start_the_mosaic(g, self)) {
        set_menu_object_shadows_mosaic(g, (uint8_t)(menu_object_shadows_mosaic(g) & MOSAIC_LAYERS));
        return true;
    }
    if (!time_for_a_step(g, self, frames_a_step(g, self)))
        return false;
    start_counting(g, self);
    uint8_t mosaic = menu_object_shadows_mosaic(g);
    if (!(mosaic & MOSAIC_SIZE))
        return true;
    set_menu_object_shadows_mosaic(g, (uint8_t)(mosaic - MOSAIC_SIZE_STEP));
    return false;
}

// $C2:21F8
/* Grow the mosaic a size a step, every so many frames, up to the largest. */
bool menu_grow_mosaic(GameState *g, MenuObject self)
{
    if (menu_object_slots_step(g, self) == MOSAIC_STARTS && !start_the_mosaic(g, self)) {
        set_menu_object_shadows_mosaic(g, (uint8_t)(menu_object_shadows_mosaic(g) | LARGEST_MOSAIC));
        return true;
    }
    if (!time_for_a_step(g, self, frames_a_step(g, self)))
        return false;
    start_counting(g, self);
    uint8_t mosaic = (uint8_t)(menu_object_shadows_mosaic(g) + MOSAIC_SIZE_STEP);
    set_menu_object_shadows_mosaic(g, mosaic);
    return (mosaic & MOSAIC_SIZE) == LARGEST_MOSAIC;
}

/* ---- The palette ----------------------------------------------------------------------------- */

/* The palette is sixteen rows of sixteen colours, a word each. */
enum { PALETTE_ROWS = 16, ROW_COLOURS = 16, COLOUR_BYTES = 2, ROW_BYTES = ROW_COLOURS * COLOUR_BYTES };
enum { PALETTE_AT = (uint16_t)RAM_MENU_OBJECT_PALETTE, PALETTE_BANK = 0x7E };

static uint16_t colour_in(const GameState *g, unsigned row, unsigned i)
{
    unsigned at = (row * ROW_COLOURS + i) * COLOUR_BYTES;
    return (uint16_t)(menu_object_palette_byte(g, at) | menu_object_palette_byte(g, at + 1) << 8);
}

static void put_colour(GameState *g, unsigned row, unsigned i, uint16_t colour)
{
    unsigned at = (row * ROW_COLOURS + i) * COLOUR_BYTES;
    set_menu_object_palette_byte(g, at, (uint8_t)colour);
    set_menu_object_palette_byte(g, at + 1, (uint8_t)(colour >> 8));
}

static uint16_t row_address(unsigned row) { return (uint16_t)(PALETTE_AT + row * ROW_BYTES); }

static void row_taken_up(GameState *g, unsigned row)
{
    set_menu_palette_row_users(g, row, (uint8_t)(menu_palette_row_users(g, row) + 1));
}

static void row_let_go(GameState *g, unsigned row)
{
    set_menu_palette_row_users(g, row, (uint8_t)(menu_palette_row_users(g, row) - 1));
}

// $C2:1DB5
void menu_clear_the_palette(GameState *g)
{
    for (unsigned at = 0; at < MENU_OBJECT_PALETTE_BYTES; at++)
        set_menu_object_palette_byte(g, at, 0);
    for (unsigned row = 0; row < PALETTE_ROWS; row++)
        set_menu_palette_row_users(g, row, 0);
}

/* The loader's row: a word the script's command sets from a byte. */
static unsigned loader_row(const GameState *g, MenuObject self) { return menu_object_slots_palette_row(g, self); }

/* The loader writes its colours through the work RAM port, which it leaves
 * past the row. */
static void leave_row_written(GameState *g, unsigned row, uint32_t source)
{
    set_menu_object_scratch_palette_source(g, source);
    hw_port_seek(g, (uint16_t)(row_address(row) + ROW_BYTES), 0);
}

enum { LOADER_LOADS = 0 };

// $C2:1DD4
/* Put a row of colours into the palette, sent with the next frame, and hold
 * the row in use for the frame; or, where the command asks for a fade, start
 * the palette's fade on the row in its place. */
bool menu_load_palette_row(GameState *g, MenuObject self)
{
    unsigned row = loader_row(g, self);
    if (menu_object_slots_step(g, self) != LOADER_LOADS) {
        row_let_go(g, row);
        return true;
    }
    if (menu_object_slots_palette_frames_a_step(g, self) != 0) {
        MenuObject fade;
        TIMED_AS(g, 0xC2048A, (void)menu_start_object(g, MENU_PALETTE_FADE, MENU_ORDINARY_SLOT, &fade));
        return true;
    }
    set_menu_object_slots_step(g, self, LOADER_LOADS + 1);
    row_taken_up(g, row);
    uint32_t source = menu_object_slots_palette_source(g, self);
    for (unsigned i = 0; i < ROW_COLOURS; i++)
        put_colour(g, row, i, menu_colour(g, (source + i * COLOUR_BYTES) & 0xFFFFFFu));
    ORIGINAL_EFFECT(leave_row_written(g, row, source));
    menu_send_the_palette(g);
    return false;
}

/* A fade takes each part of each colour of its row a unit a step towards the
 * new colour, over FADE_STEPS steps, by a count kept for each part
 * (fade_red, fade_green, fade_blue): none where the part doesn't change;
 * for a part that rises by n units, a count up from n, which moves it once
 * the count has reached FADE_STEPS, on the last n steps; for one that falls
 * by n, a count down from FADE_STEPS - 1 + n, marked FALLS, which moves it
 * while the count is still FADE_STEPS or more, on the first n steps. */
typedef uint8_t FadeCount;
enum { FADE_STEPS = 32, FALLS = 0x80, FALL_COUNT = 0x3F, NO_CHANGE = 0 };

static FadeCount count_for(int16_t change)
{
    if (change == 0)
        return NO_CHANGE;
    if (change > 0)
        return (FadeCount)change;
    return (FadeCount)(FALLS | (uint8_t)(FADE_STEPS - 1 - change));
}

/* A part's move on this step, and its count on. */
static int8_t move_on(FadeCount *count)
{
    FadeCount c = *count;
    if (c == NO_CHANGE)
        return 0;
    if (c & FALLS) {
        *count = (FadeCount)(c - 1);
        return (c & FALL_COUNT) >= FADE_STEPS ? -1 : 0;
    }
    *count = (FadeCount)(c + 1);
    return c >= FADE_STEPS ? 1 : 0;
}

/* A colour's parts, five bits each: red in the low bits, then green, then
 * blue. */
enum { RED = 0x001F, GREEN = 0x03E0, GREEN_UNIT = 0x0020, BLUE_HIGH = 0x7C, BLUE_HIGH_UNIT = 0x04 };

static int16_t red_of(uint16_t c) { return (int16_t)(c & RED); }
static int16_t green_of(uint16_t c) { return (int16_t)((c & GREEN) / GREEN_UNIT); }
static int16_t blue_of(uint16_t c) { return (int16_t)(((c >> 8) & BLUE_HIGH) / BLUE_HIGH_UNIT); }

/* A colour moved by a unit or none of each part. Each part's move stays in
 * its own byte of the colour as the original makes it, a red moved past its
 * bits carrying into nothing and a blue wrapping in the high byte; a fade the
 * game starts never takes a part past its bits. */
static uint16_t moved(uint16_t colour, int8_t red, int8_t green, int8_t blue)
{
    uint8_t low = (uint8_t)(colour + red);
    uint16_t c = (uint16_t)(((colour & 0xFF00) | low) + green * GREEN_UNIT);
    uint8_t high = (uint8_t)((c >> 8) + blue * BLUE_HIGH_UNIT);
    return (uint16_t)(high << 8 | (c & 0x00FF));
}

static FadeCount red_count(const GameState *g, MenuObject self, unsigned i)
{
    return menu_object_slots_fade_red_byte(g, self, i);
}
static FadeCount green_count(const GameState *g, MenuObject self, unsigned i)
{
    return menu_object_slots_fade_green_byte(g, self, i);
}
static FadeCount blue_count(const GameState *g, MenuObject self, unsigned i)
{
    return menu_object_slots_fade_blue_byte(g, self, i);
}
static void set_counts(GameState *g, MenuObject self, unsigned i, FadeCount red, FadeCount green, FadeCount blue)
{
    set_menu_object_slots_fade_red_byte(g, self, i, red);
    set_menu_object_slots_fade_green_byte(g, self, i, green);
    set_menu_object_slots_fade_blue_byte(g, self, i, blue);
}

/* The fade's row, and where it starts in the palette as the fade keeps it. */
static unsigned fade_row(const GameState *g, MenuObject self) { return menu_object_slots_fade_row(g, self); }

/* The colour the fade takes a colour of the row to: through a pointer whose
 * address steps on a colour at a time within its bank. */
static uint16_t target_colour(const GameState *g, MenuObject self, unsigned i)
{
    uint32_t colours = menu_object_slots_fade_colours(g, self);
    return menu_colour(g, (colours & 0xFF0000u) | (uint16_t)(colours + i * COLOUR_BYTES));
}

/* Where in the slot each part's counts are, as the original's pointers to
 * them run. */
static uint16_t counts_at(MenuObject self, unsigned part, unsigned i)
{
    enum { FIRST_COUNTS = 0x10, PART_COUNTS = 0x10 };
    return (uint16_t)(menu_object_address(self) + FIRST_COUNTS + part * PART_COUNTS + i);
}

static void leave_counted(GameState *g, MenuObject self, uint16_t current, uint16_t target)
{
    unsigned row = fade_row(g, self);
    uint32_t colours = menu_object_slots_fade_colours(g, self);
    set_menu_object_scratch_fade_colours_left(g, 0);
    set_menu_object_scratch_fade_colour(g, current);
    set_menu_object_scratch_fade_target(g, target);
    set_menu_object_scratch_fade_parts(g, (uint16_t)((current & GREEN) & 0xFF00) | (uint16_t)blue_of(current));
    set_menu_object_scratch_fade_row_next(g, (uint16_t)(row_address(row) + ROW_BYTES));
    set_menu_object_scratch_fade_red_next(g, counts_at(self, 0, ROW_COLOURS));
    set_menu_object_scratch_fade_green_next(g, counts_at(self, 1, ROW_COLOURS));
    set_menu_object_scratch_fade_blue_next(g, counts_at(self, 2, ROW_COLOURS));
    set_menu_object_scratch_fade_target_next(g, (colours & 0xFF0000u) | (uint16_t)(colours + ROW_BYTES));
}

// $C2:1EC0, $C2:1F11
/* Count how far each part of each colour of the row has to go. */
static void count_the_changes(GameState *g, MenuObject self)
{
    unsigned row = fade_row(g, self);
    uint16_t current = 0, target = 0;
    for (unsigned i = 0; i < ROW_COLOURS; i++) {
        current = colour_in(g, row, i);
        target = target_colour(g, self, i);
        set_counts(g, self, i, count_for((int16_t)(red_of(target) - red_of(current))),
                   count_for((int16_t)(green_of(target) - green_of(current))),
                   count_for((int16_t)(blue_of(target) - blue_of(current))));
    }
    ORIGINAL_EFFECT(leave_counted(g, self, current, target));
}

static void leave_stepped(GameState *g, MenuObject self, uint16_t last)
{
    set_menu_object_scratch_fade_colours_left(g, 0);
    set_menu_object_scratch_fade_colour(g, last);
    set_menu_object_scratch_fade_red_next(g, counts_at(self, 0, ROW_COLOURS));
    set_menu_object_scratch_fade_green_next(g, counts_at(self, 1, ROW_COLOURS));
    set_menu_object_scratch_fade_blue_next(g, counts_at(self, 2, ROW_COLOURS));
    set_menu_object_scratch_fade_step_next(g, (uint16_t)(menu_object_slots_fade_row_at(g, self) + ROW_BYTES));
}

// $C2:1F85, $C2:1FCC
/* A step of the fade: every colour of the row moved on by its counts. */
static void step_the_colours(GameState *g, MenuObject self)
{
    unsigned row = fade_row(g, self);
    uint16_t colour = 0;
    for (unsigned i = 0; i < ROW_COLOURS; i++) {
        FadeCount red = red_count(g, self, i), green = green_count(g, self, i), blue = blue_count(g, self, i);
        int8_t r = move_on(&red), gr = move_on(&green), b = move_on(&blue);
        set_counts(g, self, i, red, green, blue);
        colour = moved(colour_in(g, row, i), r, gr, b);
        put_colour(g, row, i, colour);
    }
    ORIGINAL_EFFECT(leave_stepped(g, self, colour));
}

enum { FADE_COUNTS = 0, FADE_STEPS_ON = 1, FADE_LETS_GO = 2, PALETTE_FADE_STEPS = 3 };

// $C2:1E38
/* Fade a row of the palette to new colours, a step every so many frames,
 * each sent with the next frame. The fade takes on the loader's state (the
 * row, the colours, how fast) and holds the row in use while it runs; a newer
 * load or fade on the row takes over from it. */
bool menu_fade_palette_row(GameState *g, MenuObject self)
{
    switch (menu_object_slots_step(g, self)) {
    case FADE_COUNTS: {
        set_menu_object_slots_step(g, self, FADE_STEPS_ON);
        unsigned row = loader_row(g, self);
        row_taken_up(g, row);
        set_menu_object_slots_fade_frames_a_step(g, self, menu_object_slots_palette_frames_a_step(g, self));
        set_menu_object_slots_fade_row(g, self, (uint16_t)row);
        set_menu_object_slots_fade_colours(g, self, menu_object_slots_palette_source(g, self));
        set_menu_object_slots_age(g, self, 0);
        set_menu_object_slots_fade_steps(g, self, 0);
        set_menu_object_slots_fade_row_at(g, self, row_address(row));
        count_the_changes(g, self);
        return false;
    }
    case FADE_STEPS_ON: {
        unsigned row = fade_row(g, self);
        if (menu_palette_row_users(g, row) != 1) {
            row_let_go(g, row);
            return true;
        }
        if (!time_for_a_step(g, self, menu_object_slots_fade_frames_a_step(g, self)))
            return false;
        step_the_colours(g, self);
        start_counting(g, self);
        menu_send_the_palette(g);
        uint16_t steps = (uint16_t)(menu_object_slots_fade_steps(g, self) + 1);
        set_menu_object_slots_fade_steps(g, self, steps);
        if (steps == FADE_STEPS)
            set_menu_object_slots_step(g, self, FADE_LETS_GO);
        return false;
    }
    default: /* FADE_LETS_GO */
        row_let_go(g, fade_row(g, self));
        return true;
    }
}

/* ---- The tile streamer ----------------------------------------------------------------------- */

/* Tiles of sixteen colours, 32 bytes each; a VRAM address counts words, so
 * a tile takes sixteen of them. */
enum { TILE_BYTES = 32, BYTES_A_VRAM_WORD = 2 };
enum { STREAM_STARTS = 0, STREAM_SENDS = 1, STREAM_STEPS = 2 };

/* The command: where the tiles come from, the VRAM word they go to, how many
 * a frame (none for all at once) and how many in all. */
static void take_up_the_stream(GameState *g, MenuObject self)
{
    ORIGINAL_EFFECT(leave_command_read(g, self));
    uint32_t source = (uint32_t)operand(g, self, 3) << 16 | operand_word(g, self, 1);
    uint16_t vram = operand_word(g, self, 4), a_frame = operand_word(g, self, 6), in_all = operand_word(g, self, 8);
    set_menu_object_slots_stream_source(g, self, source);
    set_menu_object_slots_stream_vram(g, self, vram);
    set_menu_object_slots_stream_tiles_a_frame(g, self, a_frame != 0 ? a_frame : in_all);
    set_menu_object_slots_stream_tiles_left(g, self, in_all);
}

static void leave_batch(GameState *g, uint16_t tiles, uint16_t bytes)
{
    set_menu_object_scratch_stream_batch(g, tiles);
    set_menu_object_scratch_stream_batch_bytes(g, bytes);
}

// $C2:1CF5
/* Send a set of tiles to VRAM a batch a frame, each through the upload
 * queue, until all are sent. */
bool menu_stream_tiles(GameState *g, MenuObject self)
{
    if (menu_object_slots_step(g, self) == STREAM_STARTS) {
        set_menu_object_slots_step(g, self, STREAM_SENDS);
        take_up_the_stream(g, self);
    }
    uint16_t left = menu_object_slots_stream_tiles_left(g, self);
    uint16_t batch = menu_object_slots_stream_tiles_a_frame(g, self);
    if (batch > left)
        batch = left;
    uint16_t bytes = (uint16_t)(batch * TILE_BYTES);
    uint32_t source = menu_object_slots_stream_source(g, self);
    uint16_t vram = menu_object_slots_stream_vram(g, self);
    menu_queue_object_upload(g, source, vram, bytes);
    ORIGINAL_EFFECT(leave_batch(g, batch, bytes));
    left = (uint16_t)(left - batch);
    if (left == 0)
        return true;
    set_menu_object_slots_stream_tiles_left(g, self, left);
    set_menu_object_slots_stream_source(g, self, (source & 0xFF0000u) | (uint16_t)(source + bytes));
    set_menu_object_slots_stream_vram(g, self, (uint16_t)(vram + bytes / BYTES_A_VRAM_WORD));
    return false;
}

/* How long the commands that start them are. */
enum { FADE_COMMAND_BYTES = 2, MOSAIC_COMMAND_BYTES = 3, STREAM_COMMAND_BYTES = 10 };

/* ---- The entries ----------------------------------------------------------------------------- */

/* Each handler is entered from the objects' loop with its slot in X and
 * says whether it is done with the carry, the only thing the loop reads of
 * what it leaves (the registry declares the rest, the accumulator's width
 * among them: the loop sets it again first). */
static void run_handler(Snes *s, bool (*handler)(GameState *, MenuObject))
{
    GameState *g = state_of(s);
    m_set_flag(s, CPU_FLAG_C, handler(g, menu_object_running(g)));
    m_rts(s);
}

// $C2:1CF5
void menu_stream_tiles_entry(Snes *s) { run_handler(s, menu_stream_tiles); }
// $C2:1DD4
void menu_load_palette_row_entry(Snes *s) { run_handler(s, menu_load_palette_row); }
// $C2:1E38
void menu_fade_palette_row_entry(Snes *s) { run_handler(s, menu_fade_palette_row); }
// $C2:20A2
void menu_fade_out_entry(Snes *s) { run_handler(s, menu_fade_out); }
// $C2:2105
void menu_fade_in_entry(Snes *s) { run_handler(s, menu_fade_in); }
// $C2:2194
void menu_clear_mosaic_entry(Snes *s) { run_handler(s, menu_clear_mosaic); }
// $C2:21F8
void menu_grow_mosaic_entry(Snes *s) { run_handler(s, menu_grow_mosaic); }

// $C2:1DB5
/* Its caller reads nothing it leaves (the registry declares it). */
void menu_clear_the_palette_entry(Snes *s)
{
    menu_clear_the_palette(state_of(s));
    m_rts(s);
}

/* ---- The guards ------------------------------------------------------------------------------ */

/* What a screen object reads is memory, not the machine's registers, and
 * apart from the mode's page, the palette and the objects it writes: the
 * game's commands and colours are. */
static bool apart_at(uint32_t at)
{
    enum { WORK_RAM = 0x7E, PAGE_END = 0x0100, LOW_RAM_END = 0x2000, REGISTERS_END = 0x8000, OBJECTS_END = 0x1B30 };
    uint8_t bank = (uint8_t)(at >> 16);
    uint16_t address = (uint16_t)at;
    bool system_bank = (bank & 0x7F) < 0x40;
    if (system_bank && address >= LOW_RAM_END && address < REGISTERS_END)
        return false;
    bool low_ram = bank == WORK_RAM || (system_bank && address < LOW_RAM_END);
    return !low_ram || address >= OBJECTS_END || (address >= PAGE_END && address < PALETTE_AT);
}

static bool apart(uint32_t at, unsigned bytes)
{
    return apart_at(at) && apart_at((at + bytes - 1) & 0xFFFFFFu);
}

/* Each keeps to the steps of its own table, which the original jumps
 * through by the step's number, and starts from a command it can read. */
static bool on_step(const GameState *g, MenuObject self, unsigned steps, unsigned command_bytes)
{
    uint8_t step = menu_object_slots_step(g, self);
    return step < steps && (step != 0 || command_bytes == 0 || apart(starting_command(g, self), command_bytes));
}

static bool colours_apart(uint32_t at) { return apart(at, ROW_BYTES); }

/* The streamer's batches go on through the queue, which the game keeps
 * whole. */
bool menu_tile_streamer_takes(const GameState *g, MenuObject self)
{
    return on_step(g, self, STREAM_STEPS, STREAM_COMMAND_BYTES) && menu_object_queue_is_whole(g);
}

/* A row of the palette's sixteen. */
bool menu_palette_loader_takes(const GameState *g, MenuObject self)
{
    return loader_row(g, self) < PALETTE_ROWS && colours_apart(menu_object_slots_palette_source(g, self));
}

/* A fade under way works on one of the sixteen rows where it keeps it, its
 * counts as a fade makes them (a rising one within the original's table of
 * when to move, as far as the fade's steps take it), and has steps to go. */
static bool counts_are_sound(const GameState *g, MenuObject self)
{
    for (unsigned i = 0; i < ROW_COLOURS; i++) {
        const FadeCount counts[3] = { red_count(g, self, i), green_count(g, self, i), blue_count(g, self, i) };
        for (unsigned p = 0; p < 3; p++)
            if (!(counts[p] & FALLS) && counts[p] >= 2 * FADE_STEPS)
                return false;
    }
    return true;
}

bool menu_palette_fade_takes(const GameState *g, MenuObject self)
{
    if (!on_step(g, self, PALETTE_FADE_STEPS, 0))
        return false;
    switch (menu_object_slots_step(g, self)) {
    case FADE_COUNTS:
        return loader_row(g, self) < PALETTE_ROWS && colours_apart(menu_object_slots_palette_source(g, self));
    case FADE_STEPS_ON:
        return fade_row(g, self) < PALETTE_ROWS
               && menu_object_slots_fade_row_at(g, self) == row_address(fade_row(g, self))
               && menu_object_slots_fade_steps(g, self) < FADE_STEPS && counts_are_sound(g, self);
    default:
        return fade_row(g, self) < PALETTE_ROWS;
    }
}

bool menu_fade_out_takes(const GameState *g, MenuObject self)
{
    return on_step(g, self, FADE_OUT_STEPS, FADE_COMMAND_BYTES);
}

bool menu_fade_in_takes(const GameState *g, MenuObject self) { return on_step(g, self, FADE_IN_STEPS, FADE_COMMAND_BYTES); }

bool menu_mosaic_takes(const GameState *g, MenuObject self)
{
    return on_step(g, self, MOSAIC_STEPS + 1, MOSAIC_COMMAND_BYTES);
}

bool menu_tile_streamer_runs(Snes *s) { return menu_object_handler_takes(s, menu_tile_streamer_takes); }
bool menu_palette_loader_runs(Snes *s) { return menu_object_handler_takes(s, menu_palette_loader_takes); }
bool menu_palette_fade_runs(Snes *s) { return menu_object_handler_takes(s, menu_palette_fade_takes); }
bool menu_fade_out_runs(Snes *s) { return menu_object_handler_takes(s, menu_fade_out_takes); }
bool menu_fade_in_runs(Snes *s) { return menu_object_handler_takes(s, menu_fade_in_takes); }
bool menu_mosaic_runs(Snes *s) { return menu_object_handler_takes(s, menu_mosaic_takes); }

bool menu_palette_can_be_cleared(Snes *s) { return menu_objects_on_page(s); }
