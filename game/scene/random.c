/* Scenes: the program's random numbers.
 *
 * The scene program has two kinds. The race and the captions take the next of
 * the game's table of random bytes (scene_next_random_byte, a step through
 * $C0:FE00). The star fields take stirred numbers (scene_draw_random): each
 * draw stirs into a seed (scene_random_seed) the number and the carry the
 * caller hands it, a salt, the frame counter, the scene's call cells and four
 * values of noise from the timing service, and keeps what comes out as the
 * new seed. A run of draws hands each one's number and carry on to the next,
 * so the carry is part of what a caller keeps between draws. A draw is a byte
 * or a word: a byte draw stirs the low bytes alone and leaves the seed's high
 * byte as it was.
 *
 * The stir is the original's step for step, its carries included: it is the
 * definition of the numbers the stars get, and given the same noise it draws
 * the same ones. On the SNES backend the noise is where the beam is, read as
 * the original reads it, and shadow mode hands the port the beam position the
 * original latched, so it compares everything the stars do with their
 * numbers. A host's noise comes from a generator its driver seeds, so a replay
 * draws the same stars. */
#include "scene/scene.h"

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

/* The stir's arithmetic, on bytes or words: sums and differences that carry
 * from one step to the next, as the stir chains them. */
typedef struct {
    uint16_t top;   /* the largest value of the width: $FF or $FFFF */
    bool carry;
} Stir;

static uint16_t add(Stir *st, uint16_t a, uint16_t b)
{
    uint32_t sum = (uint32_t)(a & st->top) + (b & st->top) + st->carry;
    st->carry = sum > st->top;
    return (uint16_t)(sum & st->top);
}

/* A difference borrows where the carry is clear, and leaves it set where
 * nothing was borrowed. */
static uint16_t subtract(Stir *st, uint16_t a, uint16_t b)
{
    uint32_t taken = (uint32_t)(b & st->top) + !st->carry;
    st->carry = (uint32_t)(a & st->top) >= taken;
    return (uint16_t)((a - taken) & st->top);
}

// $C3:02DD
SceneRandom scene_draw_random(GameState *g, Platform *pf, PfOrigin o, SceneRandom from, uint16_t salt,
                              TmEntropyWidth width)
{
    Stir st = { width == TM_ENTROPY_WORDS ? 0xFFFF : 0x00FF, from.carry };
    uint16_t high = (uint16_t)(scene_random_seed(g) & ~st.top);
    SceneProgramPage scene = scene_program_own();

    /* The number handed in, doubled with the carry in its low bit, into the
     * seed; the number's top bit is the next carry. */
    uint16_t number = from.value & st.top;
    uint16_t doubled = (uint16_t)(((number << 1) | st.carry) & st.top);
    st.carry = (number & (uint16_t)(st.top ^ (st.top >> 1))) != 0;
    uint16_t seed = add(&st, scene_random_seed(g), doubled);
    set_scene_random_seed(g, (uint16_t)(high | seed));

    uint16_t v = add(&st, salt, seed);
    v = add(&st, v, scene_frame_counter(g));
    TmEntropy noise = tm_entropy(pf, o, width);
    st.carry = v >= noise.value[0];
    v = add(&st, v, noise.value[1]);
    v = subtract(&st, v, noise.value[2]);
    v = add(&st, v, seed);
    v = add(&st, v, scene_call_first(g, scene));
    v ^= scene_call_second(g, scene) & st.top;
    v = add(&st, v, (uint16_t)scene_call_product(g, scene));
    uint16_t last = v;
    v = subtract(&st, v, noise.value[3]);
    set_scene_random_seed(g, (uint16_t)(high | v));
    uint16_t sign = (uint16_t)(st.top ^ (st.top >> 1));
    bool overflow = ((last ^ noise.value[3]) & (last ^ v) & sign) != 0;
    return (SceneRandom){ (uint16_t)((from.value & ~st.top) | v), st.carry, overflow };
}
