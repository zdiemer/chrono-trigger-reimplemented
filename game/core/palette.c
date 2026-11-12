/* Palette animation: the colors that move on their own, in every game mode.
 *
 * A torch flickering, water shimmering, a screen flashing white: the game keeps
 * twelve animation slots (palette_animations), each changing a run of colors
 * of the working palette, the one sent to the screen every frame
 * (palette_working), most of them from the palette as loaded (palette_base).
 * Once a frame (run_animations) every slot counts a frame, and a slot whose
 * count reaches its period starts counting again and takes a step of what it
 * does (AnimationType):
 *
 * - cycle frames: show the next of a few frames of colors kept in the ROM,
 *   in order, or in the order a table in the ROM gives;
 * - rotate: every color of the run takes its neighbour's, the one at the end
 *   coming round to the other end, in both palettes;
 * - fade toward white or black: a level moves toward a target and the colors
 *   are the loaded ones lightened or darkened by it; reaching the target ends
 *   the fade, and fading back past where the colors are as loaded restores
 *   them and ends it;
 * - pulse white or black: the level swings back and forth, and the colors
 *   lighten or darken by how far it has swung.
 *
 * A fade or pulse can change some of the channels and leave the others
 * (FadeChannels). Each location has a set of animations in the ROM, which
 * palette_load_location_animations starts in the first six slots when the location is entered;
 * the modes put their own in the others.
 *
 * The functions here take the game's state; the compatibility entries at
 * the end translate the original's registers. */
#include "core/palette.h"

#include "charge.h"
#include "core/palette_data.h"
#include "core/program.h"
#include "platform/video.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- A slot ----------------------------------------------------------------------------- */

enum { SLOTS = PALETTE_ANIMATIONS_COUNT, LOCATION_SLOTS = 6, PALETTE_COLORS = PALETTE_WORKING_COUNT };

/* What a slot does, the high nibble of its kind; 9-15 play as 8, but the
 * frame they are on doesn't step. For the frame types the low nibble is the
 * number of frames less one. */
typedef enum {
    NO_ANIMATION = 0,
    CYCLE_FRAMES = 1,
    ROTATE_DOWN = 2,   /* toward the first color */
    ROTATE_UP = 3,     /* toward the last */
    FADE_TO_WHITE = 4,
    FADE_TO_BLACK = 5,
    PULSE_WHITE = 6,
    PULSE_BLACK = 7,
    FRAMES_FROM_TABLE = 8,
} AnimationType;
enum { TYPE_SHIFT = 4, FRAMES_LESS_ONE = 0x0F };

static AnimationType type_of(const GameState *g, unsigned slot)
{
    return (AnimationType)(palette_animations_kind(g, slot) >> TYPE_SHIFT);
}

static void stop(GameState *g, unsigned slot) { set_palette_animations_kind(g, slot, NO_ANIMATION); }

static bool plays_frames(AnimationType type) { return type == CYCLE_FRAMES || type >= FRAMES_FROM_TABLE; }

/* Only these two step through their frames; 9-15 hold theirs. */
static bool steps_through_frames(AnimationType type) { return type == CYCLE_FRAMES || type == FRAMES_FROM_TABLE; }

static uint8_t frame_count(const GameState *g, unsigned slot)
{
    return (uint8_t)((palette_animations_kind(g, slot) & FRAMES_LESS_ONE) + 1);
}

static uint8_t first_color(const GameState *g, unsigned slot) { return palette_animations_first_color(g, slot); }
static uint8_t color_count(const GameState *g, unsigned slot) { return palette_animations_color_count(g, slot); }

/* Whether the slot's run of colors lies in the palette for what it does: a
 * rotation takes the color after its count too, and the frame types keep a
 * frame's size as a byte (2 per color). The frame table is in the cartridge.
 * The original's loops take a count of 0 as 65536 and run on past the palette
 * into what follows it, and the game makes neither: the entries refuse those
 * slots (their guards), so the original runs them. */
static bool slot_fits(const GameState *g, unsigned slot, AnimationType type)
{
    enum { LARGEST_FRAME = 127, CARTRIDGE_BANKS = 0xC0 };
    unsigned first = first_color(g, slot), count = color_count(g, slot);
    if (type == NO_ANIMATION)
        return true;
    if (count == 0)
        return false;
    if (type == ROTATE_DOWN || type == ROTATE_UP)
        return count <= LARGEST_FRAME && first + count < PALETTE_COLORS;
    if (first + count > PALETTE_COLORS)
        return false;
    if (!plays_frames(type))
        return true;
    return count <= LARGEST_FRAME
           && (type == CYCLE_FRAMES || palette_animations_frame_table(g, slot) >> 16 >= CARTRIDGE_BANKS);
}

/* ---- Cycling frames ----------------------------------------------------------------------- */

/* A frame is the slot's colors one after another in the ROM's frames, and
 * the slot's frames follow each other from its frames' offset. */
static uint8_t frame_bytes(const GameState *g, unsigned slot) { return (uint8_t)(color_count(g, slot) * 2); }

static uint16_t frame_offset(const GameState *g, unsigned slot, uint8_t frame)
{
    return (uint16_t)(palette_animations_frames(g, slot) + frame * frame_bytes(g, slot));
}

/* The frame the slot is on: its step, or for a table the table's entry at
 * its step. */
static uint8_t current_frame(const GameState *g, unsigned slot, AnimationType type)
{
    uint8_t step = palette_animations_step(g, slot);
    if (type == CYCLE_FRAMES)
        return step;
    uint32_t table = palette_animations_frame_table(g, slot);
    return palette_frame_table_entry(g, (table & 0xFF0000u) | (uint16_t)(table + step));
}

/* The next frame, back to the first after the last. */
static void step_frame(GameState *g, unsigned slot)
{
    uint8_t step = (uint8_t)(palette_animations_step(g, slot) + 1);
    set_palette_animations_step(g, slot, step == frame_count(g, slot) ? 0 : step);
}

// $FD:E437, $FD:E485
/* Show the frame the slot is on: its colors into the working palette. */
static void show_frame(GameState *g, unsigned slot, AnimationType type)
{
    uint8_t frame = current_frame(g, slot, type);
    uint16_t at = frame_offset(g, slot, frame);
    uint8_t first = first_color(g, slot), count = color_count(g, slot);
    for (uint8_t i = 0; i < count; i++)
        set_palette_working(g, (unsigned)(first + i), palette_frame_color(g, (uint16_t)(at + 2 * i)));
}

/* ---- Rotating ------------------------------------------------------------------------------ */

// $FD:E532 (down), $FD:E4E8 (up)
/* Rotate the colors from the first to the first plus the count, in the
 * palette as loaded and the working one alike: down, each takes the next
 * one's and the first comes round to the end; up, the other way. */
static void rotate_colors(GameState *g, unsigned slot, AnimationType type)
{
    uint8_t first = first_color(g, slot), last = (uint8_t)(first + color_count(g, slot));
    bool down = type == ROTATE_DOWN;
    uint8_t from = down ? first : last, to = down ? last : first;
    int step = down ? 1 : -1;
    uint16_t carried_base = palette_base(g, from), carried_working = palette_working(g, from);
    for (unsigned i = from; i != to; i = (unsigned)((int)i + step)) {
        set_palette_base(g, i, palette_base(g, (unsigned)((int)i + step)));
        set_palette_working(g, i, palette_working(g, (unsigned)((int)i + step)));
    }
    set_palette_base(g, to, carried_base);
    set_palette_working(g, to, carried_working);
}

/* ---- Fading --------------------------------------------------------------------------------- */

enum { RED = 0x001F, GREEN = 0x03E0, BLUE = 0x7C00 };

/* Each channel is scaled in a byte, where the original holds it: blue as the
 * color's high byte has it, red at the foot of the low byte and green three
 * bits up. Where it lies sets how the fraction rounds, which the player sees
 * as a shade's difference, so the places are kept. In the order the original
 * scales them. */
static const struct {
    uint16_t bits;
    uint8_t shift;
} CHANNELS[] = { { BLUE, 8 }, { RED, 0 }, { GREEN, 2 } };
enum { CHANNEL_COUNT = sizeof CHANNELS / sizeof CHANNELS[0] };

/* What a fade changes: the channels it scales; those it keeps from the color
 * as loaded (two of the lightening modes keep one channel fewer than they
 * leave: red and green clear blue, green and blue clear red); and the part of
 * the working color it leaves as it was (the lightening of red alone and of
 * blue alone write one byte of the color). The slot's fade mode picks one; a
 * mode past the last is the last. */
typedef struct {
    uint16_t scales, keeps, keeps_working;
} FadeChannels;
enum { FADE_MODES = 7, LOW_BYTE = 0x00FF, HIGH_BYTE = 0xFF00 };

static const FadeChannels DARKENING[FADE_MODES] = {
    { RED | GREEN | BLUE, 0, 0 }, { GREEN | BLUE, RED, 0 }, { RED | BLUE, GREEN, 0 }, { BLUE, RED | GREEN, 0 },
    { RED | GREEN, BLUE, 0 },     { GREEN, RED | BLUE, 0 }, { RED, GREEN | BLUE, 0 },
};
static const FadeChannels LIGHTENING[FADE_MODES] = {
    { RED | GREEN | BLUE, 0, 0 }, { RED, GREEN | BLUE, HIGH_BYTE }, { GREEN, RED | BLUE, 0 }, { RED | GREEN, 0, 0 },
    { BLUE, GREEN, LOW_BYTE },    { RED | BLUE, GREEN, 0 },         { GREEN | BLUE, 0, 0 },
};

static uint8_t fade_mode(const GameState *g, unsigned slot)
{
    uint8_t mode = palette_animations_fade_mode(g, slot);
    return mode < FADE_MODES ? mode : FADE_MODES - 1;
}

/* A channel lightened toward full or darkened toward none by `fraction` / 256. */
static uint8_t fade_channel(uint8_t v, uint8_t full, uint8_t fraction, bool lighten)
{
    return lighten ? (uint8_t)(v + ((full - v) * fraction >> 8)) : (uint8_t)(v * fraction >> 8);
}

static uint16_t faded(uint16_t color, const FadeChannels *m, uint8_t fraction, bool lighten)
{
    uint16_t out = color & m->keeps;
    for (unsigned c = 0; c < CHANNEL_COUNT; c++) {
        if (!(m->scales & CHANNELS[c].bits))
            continue;
        uint8_t full = (uint8_t)(CHANNELS[c].bits >> CHANNELS[c].shift);
        uint8_t v = (uint8_t)((color & CHANNELS[c].bits) >> CHANNELS[c].shift);
        out |= (uint16_t)(fade_channel(v, full, fraction, lighten) << CHANNELS[c].shift) & CHANNELS[c].bits;
    }
    return out;
}

static uint32_t fade_loop_routine(bool lighten, uint8_t mode);
static void charge_fade(GameState *g, bool lighten, uint8_t mode, uint8_t count);

// $FD:E64F-$FD:E7C0 (darkening), $FD:E8D7-$FD:EA5C (lightening)
/* The slot's run of colors, as loaded, into the working palette, lightened or
 * darkened by `fraction` / 256 in the channels its fade mode changes. */
static void fade_colors(GameState *g, unsigned slot, uint8_t mode, uint8_t fraction, bool lighten)
{
    const FadeChannels *m = lighten ? &LIGHTENING[mode] : &DARKENING[mode];
    uint8_t first = first_color(g, slot), count = color_count(g, slot);
    for (uint8_t i = 0; i < count; i++) {
        unsigned at = (unsigned)(first + i);
        uint16_t color = faded(palette_base(g, at), m, fraction, lighten);
        if (m->keeps_working)
            color = (uint16_t)((palette_working(g, at) & m->keeps_working) | (color & ~m->keeps_working));
        set_palette_working(g, at, color);
    }

    charge_fade(g, lighten, mode, count);
}

static void faded_by(GameState *g, unsigned slot, uint8_t fraction, bool lighten)
{
    uint8_t mode = fade_mode(g, slot);
    TIMED_AS(g, fade_loop_routine(lighten, mode), fade_colors(g, slot, mode, fraction, lighten));
}

// $FD:E8AC (after white), $FD:E624 (after black)
/* A fade is over, back where it started: its colors as loaded. */
static void restore_colors(GameState *g, unsigned slot)
{
    stop(g, slot);
    uint8_t first = first_color(g, slot), count = color_count(g, slot);
    for (uint8_t i = 0; i < count; i++)
        set_palette_working(g, (unsigned)(first + i), palette_base(g, (unsigned)(first + i)));
}

// $FD:E82C (to white), $FD:E5A4 (to black)
/* A step of a fade: its level moves by the step toward the target, and
 * reaching the target (going up, getting there; going down, passing it) ends
 * the fade there. A fade that would run past the level where its colors are
 * as loaded -- above the top darkening, below the bottom lightening --
 * restores them and ends instead. A slot whose period is negative holds its
 * level at the period. */
static void fade_step(GameState *g, unsigned slot, bool lighten)
{
    enum { HELD_LEVEL = 0x80, TOP = 0xFF };
    uint8_t level = palette_animations_period(g, slot);
    if (!(level & HELD_LEVEL)) {
        uint8_t target = palette_animations_fade_target(g, slot), step = palette_animations_step(g, slot);
        uint8_t now = palette_animations_fade_level(g, slot);
        bool rising = now < target;
        int next = rising ? now + step : now - step;
        bool past_the_end = next > TOP || next < 0;
        if (past_the_end && (rising ? !lighten : lighten)) {
            TIMED_AS(g, lighten ? 0xFDE8AC : 0xFDE624, restore_colors(g, slot));
            return;
        }
        bool arrived = past_the_end || (rising ? next >= target : next < target);
        if (arrived)
            stop(g, slot);
        level = arrived ? target : (uint8_t)next;
    }
    set_palette_animations_fade_level(g, slot, level);
    faded_by(g, slot, level, lighten);
}

// $FD:E807 (white), $FD:E57C (black)
/* A step of a pulse: its level runs on by the step, as a byte, so it swings
 * from positive to negative and back, and the colors lighten or darken by how
 * far it is from the middle times the target. Darkening, the fraction is what
 * is taken away, so a pulse at the middle shows black. */
static void pulse_step(GameState *g, unsigned slot, bool lighten)
{
    uint8_t level = (uint8_t)(palette_animations_fade_level(g, slot) + palette_animations_step(g, slot));
    set_palette_animations_fade_level(g, slot, level);
    uint8_t swing = (uint8_t)(((int8_t)level < 0 ? (uint8_t)-level : level) * 2);
    uint8_t fraction = (uint8_t)(swing * palette_animations_fade_target(g, slot) >> 8);
    faded_by(g, slot, lighten ? fraction : (uint8_t)-fraction, lighten);
}

/* ---- The frame's work ----------------------------------------------------------------------- */

/* What a slot does when its period comes round, as the original's handler for
 * its type does it. */
static void animate_slot(GameState *g, unsigned slot, AnimationType type)
{
    switch (type) {
    case NO_ANIMATION: break;
    case CYCLE_FRAMES: TIMED_AS(g, 0xFDE437, show_frame(g, slot, type)); break;
    case ROTATE_DOWN: TIMED_AS(g, 0xFDE532, rotate_colors(g, slot, type)); break;
    case ROTATE_UP: TIMED_AS(g, 0xFDE4E8, rotate_colors(g, slot, type)); break;
    case FADE_TO_WHITE: TIMED_AS(g, 0xFDE82C, fade_step(g, slot, true)); break;
    case FADE_TO_BLACK: TIMED_AS(g, 0xFDE5A4, fade_step(g, slot, false)); break;
    case PULSE_WHITE: TIMED_AS(g, 0xFDE807, pulse_step(g, slot, true)); break;
    case PULSE_BLACK: TIMED_AS(g, 0xFDE57C, pulse_step(g, slot, false)); break;
    default: TIMED_AS(g, 0xFDE485, show_frame(g, slot, type)); break;
    }
}

// $FD:E39C
/* A frame of every slot: each counts a frame toward its period, and one that
 * gets there starts counting again, steps on to its next frame if it plays
 * frames, and animates. */
static void run_animations(GameState *g)
{
    for (unsigned slot = 0; slot < SLOTS; slot++) {
        uint8_t delay = (uint8_t)(palette_animations_delay(g, slot) + 1);
        set_palette_animations_delay(g, slot, delay);
        if (delay != palette_animations_period(g, slot))
            continue;
        set_palette_animations_delay(g, slot, 0);
        AnimationType type = type_of(g, slot);
        if (steps_through_frames(type))
            step_frame(g, slot);
        animate_slot(g, slot, type);
    }
}

// $FD:E275
void palette_stop_animations(GameState *g)
{
    for (unsigned slot = 0; slot < SLOTS; slot++)
        stop(g, slot);
}

/* A fade a mode starts in a slot of its own. The frame counter starts at 0,
 * so the first step comes a period on. */
static void start_fade(GameState *g, unsigned slot, AnimationType type, PaletteFade fade)
{
    set_palette_animations_kind(g, slot, (uint8_t)(type << TYPE_SHIFT));
    set_palette_animations_first_color(g, slot, fade.first_color);
    set_palette_animations_color_count(g, slot, fade.color_count);
    set_palette_animations_step(g, slot, fade.step);
    set_palette_animations_delay(g, slot, 0);
    set_palette_animations_period(g, slot, fade.period);
    set_palette_animations_fade_mode(g, slot, (uint8_t)fade.tint);
    set_palette_animations_fade_level(g, slot, fade.from_level);
    set_palette_animations_fade_target(g, slot, fade.to_level);
}

bool palette_animation_playing(const GameState *g, unsigned slot) { return palette_animations_kind(g, slot) != NO_ANIMATION; }

/* The scene program's start of the telepod ending's fade ($7E:9FA9). */
void palette_start_fade_to_white(GameState *g, unsigned slot, PaletteFade fade) { start_fade(g, slot, FADE_TO_WHITE, fade); }

/* The scene program's captions fading in and out ($7E:9B4F). */
void palette_start_fade_to_black(GameState *g, unsigned slot, PaletteFade fade) { start_fade(g, slot, FADE_TO_BLACK, fade); }

/* ---- A location's animations ---------------------------------------------------------------- */

/* The ROM's sets of animations, a stream of slots: each a kind, then for an
 * animation the first color, the count and the period; for the frame types
 * the frames' offset and bank; and for a frame table the table itself, a
 * frame number a frame, which the slot then points at where it lies. A kind
 * of type 0 is a slot left empty, and takes one byte. */
typedef struct {
    const GameState *g;
    uint16_t at;
} SetStream;

static uint8_t next_byte(SetStream *in) { return palette_animation_set_byte(in->g, in->at++); }

static void load_slot(GameState *g, unsigned slot, SetStream *in)
{
    uint8_t kind = next_byte(in);
    AnimationType type = (AnimationType)(kind >> TYPE_SHIFT);
    set_palette_animations_kind(g, slot, type == NO_ANIMATION ? NO_ANIMATION : kind);
    if (type == NO_ANIMATION)
        return;
    set_palette_animations_first_color(g, slot, next_byte(in));
    set_palette_animations_color_count(g, slot, next_byte(in));
    set_palette_animations_step(g, slot, 0);
    set_palette_animations_delay(g, slot, 0);
    set_palette_animations_period(g, slot, next_byte(in));
    if (!steps_through_frames(type))
        return;
    uint16_t frames = next_byte(in);
    frames |= (uint16_t)(next_byte(in) << 8);
    set_palette_animations_frames(g, slot, frames);
    set_palette_animations_frames_bank(g, slot, next_byte(in));
    if (type != FRAMES_FROM_TABLE)
        return;
    set_palette_animations_frame_table(g, slot, palette_animation_set_address(in->at));
    uint8_t table_frames = (uint8_t)(kind & FRAMES_LESS_ONE);
    in->at = (uint16_t)(in->at + table_frames + 1);
}

// $FD:E292
/* Start the current location's palette animations in the first six slots,
 * from the set its record names, and stop the others. */
void palette_load_location_animations(GameState *g)
{
    uint8_t set = location_palette_animation_set(g, location_record_offset(g));
    SetStream in = { g, palette_animation_set_start(g, set) };
    for (unsigned slot = 0; slot < LOCATION_SLOTS; slot++)
        load_slot(g, slot, &in);
    for (unsigned slot = LOCATION_SLOTS; slot < SLOTS; slot++)
        stop(g, slot);
}

/* ---- Calibrated fade work -------------------------------------------------------------- */

/* The calibrated routine and access widths for each fade mode's work. */
typedef struct {
    uint32_t routine;
    uint16_t scratch_byte;   /* 0: the word; LOW_BYTE or HIGH_BYTE: that byte, into the cell's low byte */
    bool keeps_green;
} FadeLoop;

static const FadeLoop DARKENING_LOOPS[FADE_MODES] = {
    { 0xFDE64F, 0, false }, { 0xFDE7C0, 0, false }, { 0xFDE73A, 0, false },
    { 0xFDE6A1, 0, false },       { 0xFDE779, 0, false }, { 0xFDE6D1, 0, false },
    { 0xFDE708, 0, false },
};
static const FadeLoop LIGHTENING_LOOPS[FADE_MODES] = {
    { 0xFDE8D7, 0, true },     { 0xFDE962, LOW_BYTE, false }, { 0xFDE98F, 0, false },
    { 0xFDEA5C, 0, true },            { 0xFDE935, HIGH_BYTE, false }, { 0xFDE9CA, 0, false },
    { 0xFDEA11, 0, true },
};

static const FadeLoop *fade_loop(bool lighten, uint8_t mode)
{
    return lighten ? &LIGHTENING_LOOPS[mode] : &DARKENING_LOOPS[mode];
}

static uint32_t fade_loop_routine(bool lighten, uint8_t mode) { return fade_loop(lighten, mode)->routine; }

/* The original spends a color's time on its scratch -- each channel stored
 * there and read back, and what it leaves of the color -- and on the
 * multiplier, a channel's operand and its product. Keep the calibrated charge
 * after removing those scratch writes, so the estimate follows the count. */
static void charge_fade(GameState *g, bool lighten, uint8_t mode, uint8_t count)
{
    enum { ACCESSES_PER_CHANNEL = 4, WORD_ACCESSES = 2, BYTE_ACCESSES = 1 };
    const FadeChannels *m = lighten ? &LIGHTENING[mode] : &DARKENING[mode];
    const FadeLoop *loop = fade_loop(lighten, mode);
    uint32_t per_color = loop->scratch_byte ? BYTE_ACCESSES : WORD_ACCESSES;
    if (loop->keeps_green)
        per_color += BYTE_ACCESSES;
    for (unsigned c = 0; c < CHANNEL_COUNT; c++)
        if (m->scales & CHANNELS[c].bits)
            per_color += ACCESSES_PER_CHANNEL;
    charge_as(g, fade_loop_routine(lighten, mode), per_color * count);
}

static bool every_slot_fits(const GameState *g)
{
    for (unsigned slot = 0; slot < SLOTS; slot++)
        if (!slot_fits(g, slot, type_of(g, slot)))
            return false;
    return true;
}

/* Reject malformed animation slots without charging validation reads to
 * the animation work. A zero count would overrun the palette. */
bool palette_animated(GameState *g)
{
    vars_peek(true);
    bool fits = every_slot_fits(g);
    vars_peek(false);
    if (!fits) {
        program_breaks_a_contract(g, 0xFDE39C, "palette animations in slots the game never makes");
        return false;
    }
    palette_animate_a_frame(g);
    return true;
}

// $FD:E39C
/* Advance every active palette animation by one frame. */
void palette_animate_a_frame(GameState *g)
{
    run_animations(g);
}

// $C0:712E
/* Upload the working palette for this frame. */
void palette_send_working(Platform *pf)
{
    vid_send_palette(pf, PF_AT(0xC07130), 0, PF_GAME(0x7E2200), PALETTE_COLORS);
}
