/* Battle: the fade tables, built once at the start of a battle.
 *
 * The screen effects darken or brighten the battlefield line by line through
 * color math: a per-line table gives, for each band of lines, the value the
 * color-math register takes, all color channels at an intensity of 0-31. The
 * battle builds four such tables at the start, two pairs: in each pair one
 * falls from intensity 8 to nothing down the screen and the other rises from
 * nothing, in steps a third of an intensity apart over 24 lines, or two
 * thirds apart over 12. The screen scripts point the line effect at them.
 *
 * A table entry is four bytes: how many lines it lasts (bit 7 set to write
 * its value on every one of them) and the value, then two bytes left 0. A
 * line count of 0 ends a table. */
#include "battle/screen.h"

#include "original_effects.h"

/* A color-math value: the channels it applies to in the top three bits and the
 * intensity in the low five. The tables step the intensity in 1/256ths of a
 * level, and each entry takes the whole part. */
enum { ALL_CHANNELS = 0xE0, INTENSITY_FRACTION_BITS = 8, INTENSITY_ONE = 1 << INTENSITY_FRACTION_BITS };

static uint8_t color_math_value(uint16_t intensity) { return (uint8_t)(ALL_CHANNELS | intensity >> INTENSITY_FRACTION_BITS); }

/* The four tables in battle_fade_line_data, by the byte their first entry
 * starts at. */
typedef enum { FALL_BY_THIRDS = 0x01, RISE_BY_THIRDS = 0x61, FALL_BY_TWO_THIRDS = 0xC1, RISE_BY_TWO_THIRDS = 0xF1 } FadeTable;

enum { ENTRY_BYTES = 4, ONE_LINE = 1, EVERY_LINE = 0x80, END_OF_TABLE = 0 };

static void set_entry(GameState *g, FadeTable table, unsigned entry, uint8_t lines, uint8_t value)
{
    unsigned at = table + entry * ENTRY_BYTES;
    set_battle_fade_line_data(g, at, lines);
    set_battle_fade_line_data(g, at + 1, value);
}

/* A falling table and a rising one, one line an entry, a step apart each
 * line: the fall starts a step below intensity 9 and the rise a step above
 * nothing. */
typedef struct {
    FadeTable falls, rises;
    unsigned entries;
    uint16_t step;
} FadePair;

enum { FALL_FROM = 9 * INTENSITY_ONE };

static const FadePair pairs[] = {
    { FALL_BY_THIRDS, RISE_BY_THIRDS, 24, INTENSITY_ONE / 3 },
    { FALL_BY_TWO_THIRDS, RISE_BY_TWO_THIRDS, 12, 2 * INTENSITY_ONE / 3 },
};

/* The rising tables' last entry ends them, holding every channel at nothing.
 * The falling ones have no end of their own: each runs on into the rising
 * table after it, which is how the line effect shows a fall and a rise. */
static void end_rising_table(GameState *g, FadeTable table, unsigned entries)
{
    set_entry(g, table, entries - 1, END_OF_TABLE, ALL_CHANNELS);
}

/* The original counts the two intensities in its scratch cells, color-math
 * value and fraction together, and leaves the last pair's there. */
static void leave_intensities(GameState *g, const FadePair *p)
{
    uint16_t fell = (uint16_t)(ALL_CHANNELS << 8 | (uint16_t)(FALL_FROM - (p->entries + 1) * p->step));
    uint16_t rose = (uint16_t)(ALL_CHANNELS << 8 | (uint16_t)((p->entries + 1) * p->step));
    screen_leave_word(g, 0x53, fell);
    screen_leave_word(g, 0x55, rose);
}

// $D1:F83D
/* Build the four fade tables, from nothing. */
static void build_fade_tables(GameState *g)
{
    for (unsigned i = 0; i < BATTLE_FADE_LINE_DATA_COUNT; i++)
        set_battle_fade_line_data(g, i, 0);
    for (unsigned n = 0; n < sizeof pairs / sizeof pairs[0]; n++) {
        const FadePair *p = &pairs[n];
        uint16_t fall = (uint16_t)(FALL_FROM - p->step), rise = p->step;
        for (unsigned e = 0; e < p->entries; e++, fall -= p->step, rise += p->step) {
            set_entry(g, p->falls, e, ONE_LINE, color_math_value(fall));
            set_entry(g, p->rises, e, EVERY_LINE | ONE_LINE, color_math_value(rise));
        }
        end_rising_table(g, p->rises, p->entries);
    }
    ORIGINAL_EFFECT(leave_intensities(g, &pairs[1]));
}

/* ---- The entry ------------------------------------------------------------------------ */

void battle_build_fade_tables(Snes *s)
{
    build_fade_tables(state_of(s));
    m_rtl(s);
}
