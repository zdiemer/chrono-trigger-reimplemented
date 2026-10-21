/* Scenes: the program's random numbers.
 *
 * The scene program has two kinds. The race and the animated sprites take the
 * next of the game's table of random bytes (scene_next_random_byte, a step
 * through $C0:FE00). The star fields and the Epoch's dive take stirred numbers
 * (scene_draw_random): a seed (scene_random_seed) stirred by where the beam
 * is, the frame counter, the scene's call cells and what the caller hands it.
 *
 * The call cells are the scene's own page's, which the program's routines
 * take their values in and leave their results in (scene_call), so what the
 * stir takes from them is what the last routines to use them left. Its first
 * cell (scene_call_stirred) is what scenes leave something in on purpose or
 * as they go: the 8.8 divide its dividend (math.c), the scripts' interpreter
 * the routine a script calls (script.c), the palettes' frame the word it keeps
 * aside (screen.c), and the Epoch's dive's lights a warning light's picture
 * and a gauge's place (epoch_dive.c), each through scene_leave_for_the_draws
 * but the divide, which leaves it on whichever page it divides on. The second
 * and the product are the arithmetic's operand and result as it left them.
 *
 * A draw is a byte or a word, and comes with one bit more than its width: the
 * bit its last sum spilled over the top. Each draw starts from the one before
 * (the caller's), turned over by a bit -- the spilled bit coming in at the
 * bottom and the number's top bit going out as the new spill -- so a run of
 * draws feeds on itself. The stir is a ones'-complement sum: what one step
 * spills over the top comes back in at the bottom of the next (stir_in), and a
 * value is taken away by adding its complement. A byte draw stirs the seed's
 * low byte and leaves its high byte as it was.
 *
 * This is the original's mix exactly, so given the same noise it draws the
 * same numbers. On the SNES backend the noise is where the beam is, read as
 * the original reads it, and shadow mode hands the port the beam position the
 * original latched, so it compares everything the stars do with their
 * numbers. A host's noise comes from a generator its driver seeds, so a replay
 * draws the same stars. */
#include "scene/scene.h"

#include "original_effects.h"
#include "platform/timing.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "scene/scene_data.h"
#include "vars.h"

// $7E:3448
uint8_t scene_next_random_byte(GameState *g)
{
    uint8_t step = (uint8_t)(scene_random_step(g) + 1);
    set_scene_random_step(g, step);
    return scene_random_byte(g, step);
}

/* A draw's width: its number's bits, and the bit above them it spills into. */
typedef struct {
    unsigned bits;
    uint32_t spill, number;   /* the spilled bit, and the mask of the number's */
} DrawWidth;

static DrawWidth draw_width(TmEntropyWidth width)
{
    unsigned bits = width == TM_ENTROPY_WORDS ? 16 : 8;
    return (DrawWidth){ bits, 1u << bits, (1u << bits) - 1 };
}

/* One step of the stir: `value` added to the sum so far, with what the sum
 * spilled over the top brought back in at the bottom. */
static uint32_t stir_in(DrawWidth w, uint32_t sum, uint32_t value)
{
    return (sum & w.number) + (sum >> w.bits) + (value & w.number);
}

/* Taking a value away is adding its complement. */
static uint32_t stir_out(DrawWidth w, uint32_t sum, uint32_t value) { return stir_in(w, sum, ~value); }

static bool is_negative(DrawWidth w, uint32_t v) { return (v & (w.spill >> 1)) != 0; }

/* Whether the last draw's final step, taking the beam away, gave a result of
 * the wrong sign. The original leaves it in the processor's flags, where the
 * scatter's callers hand it back to the scripts' interpreter, which may read
 * it (tools/v_readers.py); the stars don't. Scaffolding: recorded on the way,
 * read only by the wrappers that leave it. */
static bool last_draw_overflowed;

bool scene_random_last_overflow(void) { return last_draw_overflowed; }

/* Taking `away` from `from` gave `left` with the wrong sign: the two signs
 * differed and the result's isn't `from`'s. */
static bool sign_lost(DrawWidth w, uint32_t from, uint32_t away, uint32_t left)
{
    return is_negative(w, from) != is_negative(w, away) && is_negative(w, left) != is_negative(w, from);
}

/* A draw up to its noise: the sum so far, and the seed it made. */
typedef struct {
    DrawWidth w;
    uint32_t sum;
    uint16_t seed, seed_kept;
} Stir;

static Stir stir_up_to_the_noise(GameState *g, SceneDraw last, uint16_t salt, TmEntropyWidth width)
{
    Stir st = { .w = draw_width(width) };
    DrawWidth w = st.w;
    st.seed_kept = (uint16_t)(scene_random_seed(g) & ~w.number);

    /* The last draw, turned over by a bit, into the seed. */
    uint32_t turned = ((last << 1) | (last >> w.bits)) & (w.spill | w.number);
    uint32_t sum = stir_in(w, turned, scene_random_seed(g));
    st.seed = (uint16_t)(sum & w.number);
    set_scene_random_seed(g, (uint16_t)(st.seed_kept | st.seed));

    /* The salt, the seed again and the frame. */
    sum = stir_in(w, (sum & w.spill) | (salt & w.number), st.seed);
    st.sum = stir_in(w, sum, scene_frame_counter(g));
    return st;
}

/* The rest of the draw, with its noise. The noise's first value decides the
 * bit the next sums start with: whether the stir so far is at least as
 * large. On the SNES it is what the read that latches the beam's counters
 * gives -- the bus's last byte, and for a word a byte of sprite memory -- not
 * a counter, so that bit depends on the stir more than on the beam; the
 * original's stir is so. */
static SceneDraw stir_in_the_noise(GameState *g, Stir st, TmEntropy noise)
{
    DrawWidth w = st.w;
    SceneProgramPage scene = scene_program_own();
    uint32_t sum = (st.sum & w.number) | ((st.sum & w.number) >= noise.value[0] ? w.spill : 0);
    sum = stir_in(w, sum, noise.value[1]);
    sum = stir_out(w, sum, noise.value[2]);

    /* The seed, the scene's call cells, and the noise again. */
    sum = stir_in(w, sum, st.seed);
    sum = stir_in(w, sum, scene_call_stirred(g, scene));
    sum ^= scene_call_second(g, scene) & w.number;
    sum = stir_in(w, sum, (uint16_t)scene_call_product(g, scene));
    uint32_t before = sum & w.number;
    sum = stir_out(w, sum, noise.value[3]);

    set_scene_random_seed(g, (uint16_t)(st.seed_kept | (sum & w.number)));
    ORIGINAL_EFFECT(last_draw_overflowed = sign_lost(w, before, noise.value[3], sum));
    return sum;
}

/* Something for the draws to stir in, in the scene's first call cell: a word
 * for a word draw, or for a byte draw a byte, which leaves the cell's high
 * byte as it was. */
void scene_leave_for_the_draws(GameState *g, uint16_t value, TmEntropyWidth width)
{
    SceneProgramPage scene = scene_program_own();
    uint16_t kept = width == TM_ENTROPY_WORDS ? 0 : (uint16_t)(scene_call_stirred(g, scene) & 0xFF00);
    set_scene_call_stirred(g, scene, (uint16_t)(kept | (value & draw_width(width).number)));
}

// $C3:02DD
SceneDraw scene_draw_random(GameState *g, Platform *pf, PfOrigin o, SceneDraw last, uint16_t salt, TmEntropyWidth width)
{
    Stir st = stir_up_to_the_noise(g, last, salt, width);
    return stir_in_the_noise(g, st, tm_entropy(pf, o, width));
}

/* The stir reads the beam through the data bank. Called with the data bank on
 * work RAM, its reads land on the palette as loaded instead, the high byte of
 * one color and the next color, which are then the noise: what the program
 * has there in place of the beam's registers, bytes this far into the
 * palette. */
enum { LATCH_BYTE = 0x137, ACROSS_BYTE = 0x13C, DOWN_BYTE = 0x13D };

static uint8_t palette_byte(const GameState *g, uint16_t offset)
{
    return (uint8_t)(palette_base(g, offset / 2u) >> (8 * (offset % 2u)));
}

static uint16_t palette_noise(const GameState *g, uint16_t at, TmEntropyWidth width)
{
    uint16_t low = palette_byte(g, at);
    return width == TM_ENTROPY_WORDS ? (uint16_t)(low | palette_byte(g, (uint16_t)(at + 1)) << 8) : low;
}

SceneDraw scene_draw_random_off_the_palette(GameState *g, SceneDraw last, uint16_t salt, TmEntropyWidth width)
{
    Stir st = stir_up_to_the_noise(g, last, salt, width);
    uint16_t down = palette_noise(g, DOWN_BYTE, width);
    TmEntropy noise = { { palette_noise(g, LATCH_BYTE, width), palette_noise(g, ACROSS_BYTE, width), down, down } };
    return stir_in_the_noise(g, st, noise);
}

/* ---- The draw for the program's own code ----------------------------------------------------- */

/* The program's code hands the draw its number to go on from in A, the bit
 * spilled above it in the carry and its salt in X, a byte or a word as A is
 * wide (its call at $7E:3443, or the stir itself, $C3:02DD), and takes the
 * number drawn back in A with the spilled bit in the carry. A byte draw
 * leaves A's high byte as it was. The flags the stir's last step leaves are
 * worked out from the number drawn; its overflow is the draw's own record. */
static void draw_for_the_program(Snes *s, PfOrigin o)
{
    TmEntropyWidth width = m_flag(s, CPU_FLAG_M) ? TM_ENTROPY_BYTES : TM_ENTROPY_WORDS;
    DrawWidth w = draw_width(width);
    SceneDraw last = scene_draw_from(s->cpu.a, m_flag(s, CPU_FLAG_C), width);
    SceneDraw d = s->cpu.db == SCENE_LOWER_WORK_RAM ? scene_draw_random_off_the_palette(state_of(s), last, s->cpu.x, width)
                                                    : scene_draw_random(state_of(s), m_platform(s), o, last, s->cpu.x, width);
    uint16_t number = (uint16_t)(d & w.number);
    s->cpu.a = width == TM_ENTROPY_WORDS ? number : (uint16_t)((s->cpu.a & 0xFF00) | number);
    m_set_flag(s, CPU_FLAG_C, (d & w.spill) != 0);
    m_set_flag(s, CPU_FLAG_N, is_negative(w, d));
    m_set_flag(s, CPU_FLAG_Z, number == 0);
    ORIGINAL_EFFECT(m_set_flag(s, CPU_FLAG_V, last_draw_overflowed));
}

/* The stir reads its seed, the frame counter and the scene's call cells in
 * low RAM, and the beam's counters, through the data bank: one that reaches
 * the registers, or work RAM's. */
bool scene_draw_random_present(Snes *s) { return m_bank_reaches_low_ram(s); }

bool scene_program_draw_random_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E3443, 0x5, 0x5E005F3C3E12B1C5ull);
}

void scene_draw_random_entry(Snes *s)
{
    draw_for_the_program(s, PF_AT(0xC302DD));
    m_rtl(s);
}

void scene_program_draw_random(Snes *s)
{
    draw_for_the_program(s, PF_AT(0x7E3443));
    m_rts(s);
}
