/* Object work contributes to the curtain's random edge. The scene stir reads
 * this word under its frame-counter alias. Keep the live value without adding
 * a new host timing charge: the machine's old estimate included these writes,
 * while the native program estimated the drawing/setup work alone. */
#ifndef CT_GAME_MENU_EFFECT_NOISE_H
#define CT_GAME_MENU_EFFECT_NOISE_H

#include "charge.h"
#include "vars.h"

static inline void menu_note_effect_noise(GameState *g, uint16_t value)
{
    uint32_t mark = charge_uncounted_begin(g);
    set_menu_effect_noise(g, value);
    charge_uncounted_end(g, mark);
}

#endif
