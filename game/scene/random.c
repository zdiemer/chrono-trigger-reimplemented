/* Scenes: the program's random numbers.
 *
 * The scene program has two kinds. The race and the animated sprites take the
 * next of the game's table of random bytes (scene_next_random_byte, a step
 * through $C0:FE00). The star fields take stirred numbers
 * (scene_draw_random): a seed (scene_random_seed) stirred by where the beam
 * is, the frame counter, the scene's call cells and what the caller hands it.
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

// $C3:02DD
SceneDraw scene_draw_random(GameState *g, Platform *pf, PfOrigin o, SceneDraw last, uint16_t salt, TmEntropyWidth width)
{
    DrawWidth w = draw_width(width);
    uint16_t seed_kept = (uint16_t)(scene_random_seed(g) & ~w.number);
    SceneProgramPage scene = scene_program_own();

    /* The last draw, turned over by a bit, into the seed. */
    uint32_t turned = ((last << 1) | (last >> w.bits)) & (w.spill | w.number);
    uint32_t sum = stir_in(w, turned, scene_random_seed(g));
    uint16_t seed = (uint16_t)(sum & w.number);
    set_scene_random_seed(g, (uint16_t)(seed_kept | seed));

    /* The salt, the seed again and the frame. */
    sum = stir_in(w, (sum & w.spill) | (salt & w.number), seed);
    sum = stir_in(w, sum, scene_frame_counter(g));

    /* The beam. Its first value decides the bit the counters' sum starts
     * with: whether the stir so far is at least as large. On the SNES it is
     * what the read that latches the counters gives -- the bus's last byte,
     * and for a word a byte of sprite memory -- not a counter, so that bit
     * depends on the stir more than on the beam; the original's stir is so. */
    TmEntropy beam = tm_entropy(pf, o, width);
    sum = (sum & w.number) | ((sum & w.number) >= beam.value[0] ? w.spill : 0);
    sum = stir_in(w, sum, beam.value[1]);
    sum = stir_out(w, sum, beam.value[2]);

    /* The seed, the scene's call cells, and the beam again. */
    sum = stir_in(w, sum, seed);
    sum = stir_in(w, sum, scene_call_first(g, scene));
    sum ^= scene_call_second(g, scene) & w.number;
    sum = stir_in(w, sum, (uint16_t)scene_call_product(g, scene));
    uint32_t before = sum & w.number;
    sum = stir_out(w, sum, beam.value[3]);

    set_scene_random_seed(g, (uint16_t)(seed_kept | (sum & w.number)));
    ORIGINAL_EFFECT(last_draw_overflowed = sign_lost(w, before, beam.value[3], sum));
    return sum;
}
