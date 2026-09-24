/* Palette animation, run every frame in every game mode ($FD:E39C).
 *
 * Twelve slots (palette_animations) each change a run of colors of the
 * working palette (palette_working), most of them from the palette as loaded
 * (palette_base). A slot runs every `period` frames; its type is the high
 * nibble of its kind:
 *
 *   1  frames: copy the colors from frame `step` of an animation in the ROM
 *      (core/palette_data.h), stepping through the low nibble + 1 frames;
 *   8  the same, the frame number read from a table at frame_table + step;
 *      9-15 run the same handler without stepping;
 *   2  rotate the colors first..first + count (count + 1 of them), in both
 *      palettes, toward the first;
 *   3  rotate them toward the last;
 *   4  fade toward white: the level moves by `step` toward `target`, and each
 *      of the channels the fade mode picks moves toward full by level / 256;
 *   5  fade toward black: the channels scale by level / 256;
 *   6  pulse toward white: the level runs on by `step`, wrapping, and its
 *      size times `target` makes the fraction;
 *   7  pulse toward black, the same fraction negated.
 *
 * A fade that reaches its target ends (type 0); fading past the end of its
 * range restores the colors and ends. The fractions go through the hardware
 * multiplier, which the fades and pulses leave as they last set it.
 *
 * Each routine is a C function of what it does; the entries, at the end of
 * the file, move the original's registers in and out. The animations run with
 * the direct page $0500, whose cells $0C-$1A are their scratch, and the data
 * bank $00, through which they reach the slots and the multiplier. */
#include "core/palette.h"

#include "core/palette_data.h"
#include "platform/video.h"
#include "port/machine.h"
#include "ram.h"

/* ---- Slots ------------------------------------------------------------------------------
 *
 * A slot is addressed by the original's Y, its offset from the first (slot *
 * 12), and reached through the data bank $00: an offset past the slots runs
 * on into bank 0 as the original's does. */
static uint32_t slot_field(uint16_t slot, uint32_t field) { return RAM_ABS(field) + (uint32_t)slot; }
static uint8_t slot8(Snes *s, uint16_t slot, uint32_t field) { return m_read8(s, slot_field(slot, field)); }
static void set_slot8(Snes *s, uint16_t slot, uint32_t field, uint8_t v) { m_write8(s, slot_field(slot, field), v); }

enum {
    KIND_TYPE = 0xF0,
    KIND_STEPS = 0x0F,          /* the number of frames less one, for types 1 and 8 */
    TYPE_FRAMES = 1, TYPE_ROTATE_DOWN, TYPE_ROTATE_UP, TYPE_FADE_TO_WHITE, TYPE_FADE_TO_BLACK,
    TYPE_PULSE_WHITE, TYPE_PULSE_BLACK, TYPE_FRAMES_FROM_TABLE,
};

/* The palettes as the animations reach them: a color by its byte offset
 * (color * 2) from the start, 16-bit, the way the original indexes them from a
 * long base, so an offset past the palette runs on into what follows. */
static uint32_t base_color(uint16_t at) { return RAM_PALETTE_BASE + at; }
static uint32_t working_color(uint16_t at) { return RAM_PALETTE_WORKING + at; }

/* Scratch cells of the page $0500 that the original leaves written: a color's
 * channels and the old green as a fade builds it, or a rotation's carried base
 * and working colors, its end and its length; and a long pointer. */
#define SCRATCH_COLOR RAM_PALETTE_ANIMATION_SCRATCH
#define SCRATCH_GREEN (RAM_PALETTE_ANIMATION_SCRATCH + 2)
#define SCRATCH_END (RAM_PALETTE_ANIMATION_SCRATCH + 4)
#define SCRATCH_LENGTH (RAM_PALETTE_ANIMATION_SCRATCH + 6)
#define SCRATCH_POINTER RAM_PALETTE_ANIMATION_POINTER

/* The multiplier, through which the fades take a fraction of a channel: the
 * fraction (/ 256) is set once, then each channel's product's high byte read. */
static void set_fraction(Snes *s, uint8_t f) { m_write8(s, 0x004202, f); }
static uint8_t scale(Snes *s, uint8_t v)
{
    m_write8(s, 0x004203, v);
    return m_read8(s, 0x004217);
}
static uint16_t product(Snes *s, uint8_t v)
{
    m_write8(s, 0x004203, v);
    return m_read16(s, 0x004216);
}

/* ---- Copying frames in (types 1 and 8) -------------------------------------------------- */

/* Copy `count` colors (0: 65536, as the original's counter runs) from the
 * frames at `at` into the working palette from color `first`, through the
 * long pointer the original keeps in its scratch, which it reads again for
 * every color: a count of 0 runs over the whole bank, the pointer included.
 * Returns where the frames were left, which the original leaves in X. */
static uint16_t copy_frame(Snes *s, uint8_t first, uint8_t count, uint16_t at)
{
    m_write16(s, SCRATCH_POINTER, (uint16_t)(RAM_ABS(RAM_PALETTE_WORKING) + first * 2));
    m_write8(s, SCRATCH_POINTER + 2, (uint8_t)(RAM_PALETTE_WORKING >> 16));
    uint16_t n = count;
    do {
        uint32_t to = (uint32_t)m_read8(s, SCRATCH_POINTER + 2) << 16 | m_read16(s, SCRATCH_POINTER);
        m_write16(s, to, palette_frame_color(s, at));
        m_write16(s, SCRATCH_POINTER, (uint16_t)(m_read16(s, SCRATCH_POINTER) + 2));
        at = (uint16_t)(at + 2);
        n--;
    } while (n != 0);
    return at;
}

/* Where frame `frame` of the slot's animation is: its offset plus the frame
 * times the colors' size, in the multiplier (the size taken 8-bit, as the
 * original's ASL of the count leaves it). */
static uint16_t frame_at(Snes *s, uint16_t slot, uint8_t frame)
{
    set_fraction(s, frame);
    uint16_t offset = m_read16(s, slot_field(slot, RAM_PALETTE_ANIMATIONS_FRAMES));
    return (uint16_t)(offset + product(s, (uint8_t)(slot8(s, slot, RAM_PALETTE_ANIMATIONS_COLOR_COUNT) * 2)));
}

// $FD:E437
/* Type 1: the frame is the step. */
static uint16_t animate_frames(Snes *s, uint16_t slot)
{
    uint16_t at = frame_at(s, slot, slot8(s, slot, RAM_PALETTE_ANIMATIONS_STEP));
    return copy_frame(s, slot8(s, slot, RAM_PALETTE_ANIMATIONS_FIRST_COLOR),
                      slot8(s, slot, RAM_PALETTE_ANIMATIONS_COLOR_COUNT), at);
}

// $FD:E485
/* Type 8: the frame is read from the slot's frame table, indexed by the
 * step. */
static uint16_t animate_frames_from_table(Snes *s, uint16_t slot)
{
    uint16_t pointer = (uint16_t)(slot8(s, slot, RAM_PALETTE_ANIMATIONS_STEP) +
                                  m_read16(s, slot_field(slot, RAM_PALETTE_ANIMATIONS_FRAME_TABLE)));
    uint8_t bank = slot8(s, slot, RAM_PALETTE_ANIMATIONS_FRAME_TABLE + 2);
    m_write16(s, SCRATCH_POINTER, pointer);
    m_write8(s, SCRATCH_POINTER + 2, bank);
    uint8_t frame = m_read8(s, (uint32_t)bank << 16 | pointer);
    uint16_t at = frame_at(s, slot, frame);
    return copy_frame(s, slot8(s, slot, RAM_PALETTE_ANIMATIONS_FIRST_COLOR),
                      slot8(s, slot, RAM_PALETTE_ANIMATIONS_COLOR_COUNT), at);
}

/* ---- Rotating (types 2 and 3) ----------------------------------------------------------- */

/* Rotate colors first..first + count of both palettes by one; the colors move
 * down (toward the first) or up. The loop runs at least once, so a count of 0
 * runs through all 32768 colors' worth of offsets, as the original's does.
 * Returns where it stopped, which the original leaves in X. */
static uint16_t rotate(Snes *s, uint16_t slot, bool up)
{
    uint8_t length = (uint8_t)(slot8(s, slot, RAM_PALETTE_ANIMATIONS_COLOR_COUNT) * 2);
    m_write16(s, SCRATCH_LENGTH, length);
    uint16_t first = (uint16_t)(slot8(s, slot, RAM_PALETTE_ANIMATIONS_FIRST_COLOR) * 2);
    uint16_t last = (uint16_t)(first + length);
    uint16_t from = up ? last : first, end = up ? first : last;
    m_write16(s, SCRATCH_END, end);
    uint16_t carried_base = m_read16(s, base_color(from)), carried_working = m_read16(s, working_color(from));
    m_write16(s, SCRATCH_COLOR, carried_base);
    m_write16(s, SCRATCH_GREEN, carried_working);
    uint16_t at = from;
    do {
        /* The neighbour is read at the color's address -2 or +2, so at an
         * offset of 0 or $FFFE it is outside the 16-bit run of offsets. */
        int8_t by = up ? -2 : 2;
        m_write16(s, base_color(at), m_read16(s, base_color(at) + (uint32_t)(int32_t)by));
        m_write16(s, working_color(at), m_read16(s, working_color(at) + (uint32_t)(int32_t)by));
        at = (uint16_t)(at + by);
    } while (at != end);
    m_write16(s, base_color(at), carried_base);
    m_write16(s, working_color(at), carried_working);
    return at;
}

/* ---- Fading (types 4-7) ------------------------------------------------------------------ */

enum { RED = 0x001F, GREEN = 0x03E0, BLUE = 0x7C00 };
enum { WRITE_WORD, WRITE_LOW, WRITE_HIGH };

/* Which channels a fade changes, by its fade mode, and what the original's
 * code for that mode leaves besides: the byte of the color it writes (the
 * lighten modes for red alone and blue alone write one), the new color's bits
 * it keeps in a scratch cell (a byte for those two), whether it keeps the old
 * green there too, and the channels it keeps from the old color, which two of
 * the lighten modes' masks get wrong (red and green clears blue, green and
 * blue clears red). */
typedef struct {
    uint16_t channels;
    uint16_t kept;
    uint8_t write;
    uint16_t scratch;
    bool green_scratch;
    uint32_t routine;   /* the mode's loop in the original, whose time the port's is (native_time_as) */
} FadeMode;

enum { FADE_MODES = 7 };

static const FadeMode darken_modes[FADE_MODES] = {
    { RED | GREEN | BLUE, 0, WRITE_WORD, RED | BLUE, false, 0xFDE64F },
    { GREEN | BLUE, RED, WRITE_WORD, RED | BLUE, false, 0xFDE7C0 },
    { RED | BLUE, GREEN, WRITE_WORD, RED | BLUE, false, 0xFDE73A },
    { BLUE, RED | GREEN, WRITE_WORD, BLUE, false, 0xFDE6A1 },
    { RED | GREEN, BLUE, WRITE_WORD, RED | BLUE, false, 0xFDE779 },
    { GREEN, RED | BLUE, WRITE_WORD, RED | BLUE, false, 0xFDE6D1 },
    { RED, GREEN | BLUE, WRITE_WORD, RED, false, 0xFDE708 },          /* and modes past 6 */
};
static const FadeMode lighten_modes[FADE_MODES] = {
    { RED | GREEN | BLUE, 0, WRITE_WORD, RED | BLUE, true, 0xFDE8D7 },
    { RED, GREEN | BLUE, WRITE_LOW, RED, false, 0xFDE962 },
    { GREEN, RED | BLUE, WRITE_WORD, GREEN, false, 0xFDE98F },
    { RED | GREEN, 0, WRITE_WORD, RED, true, 0xFDEA5C },
    { BLUE, GREEN, WRITE_HIGH, BLUE, false, 0xFDE935 },
    { RED | BLUE, GREEN, WRITE_WORD, RED | BLUE, false, 0xFDE9CA },
    { GREEN | BLUE, 0, WRITE_WORD, BLUE, true, 0xFDEA11 },            /* and modes past 6 */
};

/* One channel, 8-bit (blue as the color's high byte, green shifted up to the
 * top of a byte), scaled by the fraction, or moved toward full (`full`) by it. */
static uint8_t fade_channel(Snes *s, uint8_t v, uint8_t full, bool lighten)
{
    return lighten ? (uint8_t)(v + scale(s, v ^ full)) : scale(s, v);
}

/* Fade `count` colors (0: 65536) from byte offset `at` of the palette as
 * loaded into the working palette, by the fraction in the multiplier. The
 * channels go through the multiplier blue, red, green. Returns where it
 * stopped, which the original leaves in X. */
static uint16_t fade_colors(Snes *s, uint16_t at, uint8_t count, const FadeMode *m, bool lighten)
{
    uint16_t n = count;
    do {
        uint16_t old = m_read16(s, base_color(at));
        uint8_t blue = (uint8_t)(old >> 8) & (BLUE >> 8), red = old & RED;
        uint8_t green = (uint8_t)((old & GREEN) >> 2);
        uint16_t color = old & m->kept;
        if (m->channels & BLUE)
            color |= (uint16_t)(fade_channel(s, blue, BLUE >> 8, lighten) << 8) & BLUE;
        if (m->channels & RED)
            color |= fade_channel(s, red, RED, lighten) & RED;
        if (m->channels & GREEN)
            color |= (uint16_t)((fade_channel(s, green, GREEN >> 2, lighten) & (GREEN >> 2)) << 2);
        if (m->write == WRITE_LOW) {
            m_write8(s, working_color(at), (uint8_t)color);
            m_write8(s, SCRATCH_COLOR, (uint8_t)(color & m->scratch));
        } else if (m->write == WRITE_HIGH) {
            m_write8(s, working_color(at) + 1, (uint8_t)(color >> 8));
            m_write8(s, SCRATCH_COLOR, (uint8_t)((color & m->scratch) >> 8));
        } else {
            m_write16(s, working_color(at), color);
            m_write16(s, SCRATCH_COLOR, color & m->scratch);
        }
        if (m->green_scratch)
            m_write8(s, SCRATCH_GREEN, green);
        at = (uint16_t)(at + 2);
        n--;
    } while (n != 0);
    return at;
}

static const FadeMode *fade_mode(Snes *s, uint16_t slot, bool lighten)
{
    uint8_t mode = slot8(s, slot, RAM_PALETTE_ANIMATIONS_FADE_MODE);
    if (mode >= FADE_MODES)
        mode = FADE_MODES - 1;
    return lighten ? &lighten_modes[mode] : &darken_modes[mode];
}

/* The slot's colors by its fade mode, with the fraction already set: the
 * mode's own loop in the original, which the handlers jump to through a
 * table. */
static uint16_t fade_slot_colors(Snes *s, uint16_t slot, bool lighten)
{
    uint16_t at = (uint16_t)(slot8(s, slot, RAM_PALETTE_ANIMATIONS_FIRST_COLOR) * 2);
    const FadeMode *m = fade_mode(s, slot, lighten);
    int was = native_time_as(s, m->routine);
    at = fade_colors(s, at, slot8(s, slot, RAM_PALETTE_ANIMATIONS_COLOR_COUNT), m, lighten);
    native_time_end(s, was);
    return at;
}

// $FD:E8AC (toward white), $FD:E624 (toward black)
/* End the animation with its colors as loaded. */
static uint16_t restore_colors(Snes *s, uint16_t slot)
{
    set_slot8(s, slot, RAM_PALETTE_ANIMATIONS_KIND, 0);
    uint16_t at = (uint16_t)(slot8(s, slot, RAM_PALETTE_ANIMATIONS_FIRST_COLOR) * 2);
    uint16_t n = slot8(s, slot, RAM_PALETTE_ANIMATIONS_COLOR_COUNT);
    do {
        m_write16(s, working_color(at), m_read16(s, base_color(at)));
        at = (uint16_t)(at + 2);
        n--;
    } while (n != 0);
    return at;
}

/* The restore is a path of the fade's handler that the original branches to,
 * registered as an entry of its own so that its time, a copy of the colors,
 * is estimated apart from the handler's (native_time_as). */
static uint16_t restore_path(Snes *s, uint16_t slot, bool lighten)
{
    uint16_t at;
    NATIVE_TIME_AS(s, lighten ? 0xFDE8AC : 0xFDE624, at = restore_colors(s, slot));
    return at;
}

// $FD:E82C (toward white), $FD:E5A4 (toward black)
/* Types 4 and 5: move the level by the step toward the target, ending the
 * animation at the target when it gets there; a negative period is the level
 * itself. Fading past the end of the range -- above 255 toward black, below 0
 * toward white -- restores the colors and ends instead. */
static uint16_t fade(Snes *s, uint16_t slot, bool lighten)
{
    uint8_t level = slot8(s, slot, RAM_PALETTE_ANIMATIONS_PERIOD);
    if (!(level & 0x80)) {
        uint8_t target = slot8(s, slot, RAM_PALETTE_ANIMATIONS_FADE_TARGET);
        uint8_t step = slot8(s, slot, RAM_PALETTE_ANIMATIONS_STEP);
        level = slot8(s, slot, RAM_PALETTE_ANIMATIONS_FADE_LEVEL);
        bool arrived;
        if (level < target) {
            if (level + step > 0xFF && !lighten)
                return restore_path(s, slot, lighten);
            arrived = level + step > 0xFF || (uint8_t)(level + step) >= target;
            level = (uint8_t)(level + step);
        } else {
            if (level < step && lighten)
                return restore_path(s, slot, lighten);
            arrived = level < step || (uint8_t)(level - step) < target;
            level = (uint8_t)(level - step);
        }
        if (arrived) {
            set_slot8(s, slot, RAM_PALETTE_ANIMATIONS_KIND, 0);
            level = slot8(s, slot, RAM_PALETTE_ANIMATIONS_FADE_TARGET);
        }
    }
    set_slot8(s, slot, RAM_PALETTE_ANIMATIONS_FADE_LEVEL, level);
    set_fraction(s, slot8(s, slot, RAM_PALETTE_ANIMATIONS_FADE_LEVEL));
    return fade_slot_colors(s, slot, lighten);
}

// $FD:E807 (toward white), $FD:E57C (toward black)
/* Types 6 and 7: the level runs on by the step (8-bit, so it swings from
 * positive to negative), and the fraction is its size times 2 times the
 * target, / 256; toward black it is negated. */
static uint16_t pulse(Snes *s, uint16_t slot, bool lighten)
{
    uint8_t level = (uint8_t)(slot8(s, slot, RAM_PALETTE_ANIMATIONS_FADE_LEVEL) +
                              slot8(s, slot, RAM_PALETTE_ANIMATIONS_STEP));
    set_slot8(s, slot, RAM_PALETTE_ANIMATIONS_FADE_LEVEL, level);
    uint8_t size = (level & 0x80) ? (uint8_t)-level : level;
    set_fraction(s, (uint8_t)(size * 2));
    uint8_t fraction = scale(s, slot8(s, slot, RAM_PALETTE_ANIMATIONS_FADE_TARGET));
    set_fraction(s, lighten ? fraction : (uint8_t)-fraction);
    return fade_slot_colors(s, slot, lighten);
}

/* ---- The frame's work ---------------------------------------------------------------- */

static uint16_t run_type(Snes *s, uint16_t slot, uint8_t type)
{
    switch (type) {
    case TYPE_FRAMES: return animate_frames(s, slot);
    case TYPE_ROTATE_DOWN: return rotate(s, slot, false);
    case TYPE_ROTATE_UP: return rotate(s, slot, true);
    case TYPE_FADE_TO_WHITE: return fade(s, slot, true);
    case TYPE_FADE_TO_BLACK: return fade(s, slot, false);
    case TYPE_PULSE_WHITE: return pulse(s, slot, true);
    case TYPE_PULSE_BLACK: return pulse(s, slot, false);
    default: return animate_frames_from_table(s, slot);
    }
}

/* Each type's handler in the original, which the loop calls through a table
 * (types 9-15 run type 8's), and whose time the port's is (native_time_as). */
static const uint32_t type_handlers[16] = {
    0, 0xFDE437, 0xFDE532, 0xFDE4E8, 0xFDE82C, 0xFDE5A4, 0xFDE807, 0xFDE57C,
    0xFDE485, 0xFDE485, 0xFDE485, 0xFDE485, 0xFDE485, 0xFDE485, 0xFDE485, 0xFDE485,
};

/* Run the slot's type; returns what its handler leaves in X, or `x` if it
 * runs none. */
static uint16_t run_slot(Snes *s, uint16_t slot, uint16_t x)
{
    uint8_t type = slot8(s, slot, RAM_PALETTE_ANIMATIONS_KIND) >> 4;
    if (type == 0)
        return x;
    int was = native_time_as(s, type_handlers[type]);
    x = run_type(s, slot, type);
    native_time_end(s, was);
    return x;
}

enum { SLOTS_END = RAM_PALETTE_ANIMATIONS_COUNT * RAM_PALETTE_ANIMATIONS_STRIDE };

// $FD:E39C
/* Every slot whose delay reaches its period: start the delay over, step the
 * frame types (1 and 8) on, wrapping after their frames, and run its type.
 * Returns what the last handler left in X, or `x`. */
static uint16_t animate(Snes *s, uint16_t x)
{
    for (uint16_t slot = 0; slot != SLOTS_END; slot = (uint16_t)(slot + RAM_PALETTE_ANIMATIONS_STRIDE)) {
        uint8_t delay = (uint8_t)(slot8(s, slot, RAM_PALETTE_ANIMATIONS_DELAY) + 1);
        set_slot8(s, slot, RAM_PALETTE_ANIMATIONS_DELAY, delay);
        if (delay != slot8(s, slot, RAM_PALETTE_ANIMATIONS_PERIOD))
            continue;
        set_slot8(s, slot, RAM_PALETTE_ANIMATIONS_DELAY, 0);
        uint8_t type = slot8(s, slot, RAM_PALETTE_ANIMATIONS_KIND) & KIND_TYPE;
        if (type == TYPE_FRAMES << 4 || type == TYPE_FRAMES_FROM_TABLE << 4) {
            uint8_t step = (uint8_t)(slot8(s, slot, RAM_PALETTE_ANIMATIONS_STEP) + 1);
            set_slot8(s, slot, RAM_PALETTE_ANIMATIONS_STEP, step);
            if ((uint8_t)((slot8(s, slot, RAM_PALETTE_ANIMATIONS_KIND) & KIND_STEPS) + 1) ==
                slot8(s, slot, RAM_PALETTE_ANIMATIONS_STEP))
                set_slot8(s, slot, RAM_PALETTE_ANIMATIONS_STEP, 0);
        }
        x = run_slot(s, slot, x);
    }
    return x;
}

// $FD:E275
/* Stop every palette animation (the slots' types to 0). */
static void clear_animations(Snes *s)
{
    for (uint16_t slot = 0; slot != SLOTS_END; slot = (uint16_t)(slot + RAM_PALETTE_ANIMATIONS_STRIDE))
        set_slot8(s, slot, RAM_PALETTE_ANIMATIONS_KIND, 0);
}

/* The location's animations go in the first six slots; the others stop. */
enum { LOCATION_SLOTS = 6 };

// $FD:E292
/* Start the current location's palette animations: the set its record names,
 * a stream of slots in the ROM (core/palette_data.h). Each slot starts with
 * its kind; a kind of type 0 stops the slot, and takes one byte. The others
 * take the first color, the count and the period, starting at step 0 and delay
 * 0; the frame types (1 and 8) also take +6-+8 (the frames' address and a
 * byte past it), and type 8 its frame table, which the slot points at where it
 * lies in the stream, the frame count bytes of it. The original also sets the
 * work RAM port's address to the slots, which nothing uses. */
static void load_animations(Snes *s)
{
    uint8_t set = location_palette_animation_set(s, m_read16(s, RAM_LOCATION_RECORD_OFFSET));
    uint16_t at = palette_animation_set_start(s, set);
    m_write16(s, 0x002181, RAM_ABS(RAM_PALETTE_ANIMATIONS));   /* the work RAM port's address, $7E:0520 */
    m_write8(s, 0x002183, 0x00);
    m_write16(s, SCRATCH_POINTER, 0x000C);   /* which nothing reads */
    for (uint16_t slot = 0; slot != LOCATION_SLOTS * RAM_PALETTE_ANIMATIONS_STRIDE;
         slot = (uint16_t)(slot + RAM_PALETTE_ANIMATIONS_STRIDE)) {
        uint8_t type = palette_animation_set_byte(s, at) & KIND_TYPE;
        if (type == 0) {
            at++;
            set_slot8(s, slot, RAM_PALETTE_ANIMATIONS_KIND, 0);
            continue;
        }
        bool frames = type == TYPE_FRAMES << 4 || type == TYPE_FRAMES_FROM_TABLE << 4;
        static const uint32_t header[] = {
            RAM_PALETTE_ANIMATIONS_KIND, RAM_PALETTE_ANIMATIONS_FIRST_COLOR, RAM_PALETTE_ANIMATIONS_COLOR_COUNT,
        };
        for (uint8_t i = 0; i < 3; i++)
            set_slot8(s, slot, header[i], palette_animation_set_byte(s, at++));
        set_slot8(s, slot, RAM_PALETTE_ANIMATIONS_STEP, 0);
        set_slot8(s, slot, RAM_PALETTE_ANIMATIONS_DELAY, 0);
        set_slot8(s, slot, RAM_PALETTE_ANIMATIONS_PERIOD, palette_animation_set_byte(s, at++));
        if (!frames)
            continue;
        for (uint8_t i = 0; i < 3; i++)
            set_slot8(s, slot, RAM_PALETTE_ANIMATIONS_FRAMES + i, palette_animation_set_byte(s, at++));
        if (type != TYPE_FRAMES_FROM_TABLE << 4)
            continue;
        uint32_t table = palette_animation_set_address(at);
        m_write16(s, slot_field(slot, RAM_PALETTE_ANIMATIONS_FRAME_TABLE), (uint16_t)table);
        set_slot8(s, slot, RAM_PALETTE_ANIMATIONS_FRAME_TABLE + 2, (uint8_t)(table >> 16));
        uint8_t steps = slot8(s, slot, RAM_PALETTE_ANIMATIONS_KIND) & KIND_STEPS;
        m_write8(s, SCRATCH_GREEN, steps);
        m_write8(s, SCRATCH_GREEN + 1, 0);
        at = (uint16_t)(at + steps + 1);
    }
    for (uint16_t slot = LOCATION_SLOTS * RAM_PALETTE_ANIMATIONS_STRIDE; slot != SLOTS_END;
         slot = (uint16_t)(slot + RAM_PALETTE_ANIMATIONS_STRIDE))
        set_slot8(s, slot, RAM_PALETTE_ANIMATIONS_KIND, 0);
}

/* ---- The machine at the entries ---------------------------------------------------------
 *
 * From here to the upload: the translation between the original's registers
 * and the C, which a build for the 65816 replaces with its own calling
 * convention. palette_animate saves its caller's direct page and data bank,
 * sets the data bank from the direct page's low byte and the direct page to
 * $0500, and restores both. The handlers, entries of their own, run with
 * what it set, and A's high byte clear, which its guard checks: they index
 * the palettes by the first color through the whole of A. */

bool palette_animate_page_is_whole(Snes *s) { return (s->cpu.d & 0xFF) == 0; }
bool palette_handler_can_run(Snes *s)
{
    return s->cpu.d == RAM_PAGE_PALETTE_FADE && s->cpu.db == 0x00 && (s->cpu.a >> 8) == 0;
}

void palette_animate(Snes *s)
{
    s->cpu.x = animate(s, RAM_PAGE_PALETTE_FADE);
    /* The loop leaves its last slot offset in A and Y and the flags of its
     * comparison and addition. */
    s->cpu.a = SLOTS_END;
    s->cpu.y = SLOTS_END;
    m_set_flag(s, CPU_FLAG_C, true);
    m_set_flag(s, CPU_FLAG_V, false);
    m_rtl(s);
}

/* Both save the caller's P (and data bank; the loader its direct page) and
 * restore them. The clear leaves Y past the last slot, which its callers read. */
void palette_clear_animations(Snes *s)
{
    clear_animations(s);
    s->cpu.y = SLOTS_END;
    m_set_a8(s, SLOTS_END);
    m_rtl(s);
}

void palette_load_animations(Snes *s)
{
    load_animations(s);
    m_rtl(s);
}

/* A type's handler: returns with the slot offset in Y as it came, X from the
 * handler, and A 0. */
static void handler_exit(Snes *s, uint16_t x)
{
    s->cpu.x = x;
    s->cpu.a = 0;
    m_rts(s);
}
/* The frame types save Y on the stack around the copy, which a count of 0
 * runs over (the whole bank): Y comes back from there. palette_animate
 * doesn't reproduce that; its callers' slots never have such a count. */
static void frames_entry(Snes *s, bool from_table)
{
    uint16_t slot = s->cpu.y;
    m_push16(s, slot);
    uint16_t x = from_table ? animate_frames_from_table(s, slot) : animate_frames(s, slot);
    s->cpu.y = m_pull16(s);
    handler_exit(s, x);
}
void palette_animate_frames(Snes *s) { frames_entry(s, false); }
void palette_animate_frames_from_table(Snes *s) { frames_entry(s, true); }
void palette_rotate_down(Snes *s) { handler_exit(s, rotate(s, s->cpu.y, false)); }
void palette_rotate_up(Snes *s) { handler_exit(s, rotate(s, s->cpu.y, true)); }
void palette_fade_to_white(Snes *s) { handler_exit(s, fade(s, s->cpu.y, true)); }
void palette_fade_to_black(Snes *s) { handler_exit(s, fade(s, s->cpu.y, false)); }
/* The fades' restore paths, entered with the slot in Y and on the stack under
 * the return address, where the handler saved it: they pull it back and
 * return. */
static void restore_entry(Snes *s)
{
    uint16_t x = restore_colors(s, s->cpu.y);
    s->cpu.y = m_pull16(s);
    handler_exit(s, x);
}
void palette_restore_after_white(Snes *s) { restore_entry(s); }
void palette_restore_after_black(Snes *s) { restore_entry(s); }
void palette_pulse_white(Snes *s) { handler_exit(s, pulse(s, s->cpu.y, true)); }
void palette_pulse_black(Snes *s) { handler_exit(s, pulse(s, s->cpu.y, false)); }

/* A fade mode's loop, entered with X the first color's offset and Y the slot:
 * returns with X past the last color and Y 0. */
static void fade_mode_entry(Snes *s, const FadeMode *m, bool lighten)
{
    s->cpu.x = fade_colors(s, s->cpu.x, slot8(s, s->cpu.y, RAM_PALETTE_ANIMATIONS_COLOR_COUNT), m, lighten);
    s->cpu.y = 0;
    m_rts(s);
}
void palette_darken_all(Snes *s) { fade_mode_entry(s, &darken_modes[0], false); }
void palette_darken_green_blue(Snes *s) { fade_mode_entry(s, &darken_modes[1], false); }
void palette_darken_red_blue(Snes *s) { fade_mode_entry(s, &darken_modes[2], false); }
void palette_darken_blue(Snes *s) { fade_mode_entry(s, &darken_modes[3], false); }
void palette_darken_red_green(Snes *s) { fade_mode_entry(s, &darken_modes[4], false); }
void palette_darken_green(Snes *s) { fade_mode_entry(s, &darken_modes[5], false); }
void palette_darken_red(Snes *s) { fade_mode_entry(s, &darken_modes[6], false); }
void palette_lighten_all(Snes *s) { fade_mode_entry(s, &lighten_modes[0], true); }
void palette_lighten_red(Snes *s) { fade_mode_entry(s, &lighten_modes[1], true); }
void palette_lighten_green(Snes *s) { fade_mode_entry(s, &lighten_modes[2], true); }
void palette_lighten_red_green(Snes *s) { fade_mode_entry(s, &lighten_modes[3], true); }
void palette_lighten_blue(Snes *s) { fade_mode_entry(s, &lighten_modes[4], true); }
void palette_lighten_red_blue(Snes *s) { fade_mode_entry(s, &lighten_modes[5], true); }
void palette_lighten_green_blue(Snes *s) { fade_mode_entry(s, &lighten_modes[6], true); }

// $C0:712E
/* Upload the working palette: the 256 colors at $7E:2200 into CGRAM from entry
 * 0. Every mode calls this from its vblank work. */
void palette_upload(Snes *s)
{
    m_lda8(s, 0x00);
    m_lda8(s, 0x00);
    m_lda8(s, 0x22);
    m_ldx16(s, 0x2200);
    m_lda8(s, 0x7E);
    m_ldx16(s, 0x0200);
    vid_send_palette(m_platform(s), PF_AT(0xC07130), 0, PF_GAME(0x7E2200), 0x100);
    m_lda8(s, 0x80);
    m_rts(s);
}

/* The upload writes the registers with absolute addressing, so it needs a data
 * bank that reaches them. */
bool palette_bank_is_io(Snes *s) { return m_io_bank(s->cpu.db); }
