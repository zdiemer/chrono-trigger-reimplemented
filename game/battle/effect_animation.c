/* Battle: the effects' animations (battle/effects.h).
 *
 * An effect shows its animation a frame at a time: each frame is a sprite
 * set, drawn at an offset from the effect's anchor, for a number of frames of
 * time. Once a frame the battle counts every animating effect's frame time
 * down; an effect whose time has run out moves on to its next frame, which
 * the original's code at $CC:F2A9 reads from the effect's animation (its
 * sprite set, offset and time), or, at the animation's end, starts it over or
 * stops it. That step is still the original's (docs/TODO.md). */
#include "battle/effects.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "original_effects.h"
#include "vars.h"

static bool effect_animating(const GameState *g, unsigned effect) { return battle_effect_animating(g, effect) != 0; }

/* Count each animating effect's frame time down a frame, to 0. */
static void count_frame_times_down(GameState *g)
{
    for (unsigned effect = 0; effect < BATTLE_EFFECTS; effect++) {
        uint8_t left = battle_effect_frame_time(g, effect);
        if (effect_animating(g, effect) && left != 0)
            set_battle_effect_frame_time(g, effect, (uint8_t)(left - 1));
    }
}

static bool frame_is_up(const GameState *g, unsigned effect)
{
    return effect_animating(g, effect) && battle_effect_frame_time(g, effect) == 0;
}

/* ---- The entry ------------------------------------------------------------------------- */

enum { NEXT_FRAME = 0xCCF2A9 };

/* The effect the next frame is taken for, which the original's step reads
 * from the battle's first work cells (a word). */
static void set_animating_effect(GameState *g, uint16_t effect)
{
    set_battle_work(g, 0, (uint8_t)effect);
    set_battle_work(g, 1, (uint8_t)(effect >> 8));
}

/* Take the effect's next frame: the original's step, which reads the effect
 * from its cell. */
static bool take_next_frame(Snes *s, unsigned effect)
{
    set_animating_effect(state_of(s), (uint16_t)effect);
    return battle_call(s, 0xCCF29D, NEXT_FRAME, DC_A | DC_X | DC_Y | DC_FLAGS);
}

// $CC:F278
/* Step the effects' animations a frame: count their frame times down, then
 * move each effect whose time is up on to its next frame. */
void battle_animate_effects(Snes *s)
{
    GameState *g = state_of(s);
    count_frame_times_down(g);
    for (unsigned effect = 0; effect < BATTLE_EFFECTS; effect++)
        if (frame_is_up(g, effect) && !take_next_frame(s, effect))
            return;
    ORIGINAL_EFFECT(set_animating_effect(g, BATTLE_EFFECTS));
    m_rtl(s);
}
