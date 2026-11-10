/* Battle conditions, tech searches and window slopes leave a value that the
 * next scene's curtain stirs into its edge through the frame-counter alias.
 * This is shared state. Keep both timing models' existing estimates: the
 * caller already retains the old access charge, and the host counted the
 * search or drawing work without this transfer. */
#ifndef CT_GAME_BATTLE_EFFECT_NOISE_H
#define CT_GAME_BATTLE_EFFECT_NOISE_H

#include "charge.h"
#include "vars.h"

static inline void battle_note_curtain_noise_low(GameState *g, uint8_t value)
{
    uint32_t mark = charge_uncounted_begin(g);
    set_battle_low_scratch(g, 8, value);
    charge_uncounted_end(g, mark);
}

static inline void battle_note_curtain_noise_high(GameState *g, uint8_t value)
{
    uint32_t mark = charge_uncounted_begin(g);
    set_battle_low_scratch(g, 9, value);
    charge_uncounted_end(g, mark);
}

static inline void battle_note_curtain_noise(GameState *g, uint16_t value)
{
    battle_note_curtain_noise_low(g, (uint8_t)value);
    battle_note_curtain_noise_high(g, (uint8_t)(value >> 8));
}

#endif
