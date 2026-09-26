/* What the menus' ported routines leave in the registers for the original's
 * sake: scaffolding (original_effects.h), worked out from the state the
 * routine left and called only through ORIGINAL_EFFECT, never by the logic
 * (menu/leaves.c). */
#ifndef CT_GAME_MENU_LEAVES_H
#define CT_GAME_MENU_LEAVES_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

/* What a routine leaves in the index registers; Y only where `y_set`, and
 * as it was otherwise. The entries' wrappers hand them to the machine. */
typedef struct {
    uint16_t x, y;
    bool y_set;
} MenuIndexLeft;

/* The typesetter's start ($C2:57DF): X the arguments' place, Y the string's
 * offset in its table. */
MenuIndexLeft menu_text_started_leaves(const GameState *g);

/* A call of the typesetter ($C2:5823) that began with the text ended or not
 * (`begun`) and reading the string at `from`: X and Y as the original's last
 * steps leave them. */
MenuIndexLeft menu_text_went_on_leaves(const GameState *g, bool begun, uint32_t from);

/* The label setting ($C2:F90C): its last call's X and Y. */
MenuIndexLeft menu_label_set_leaves(const GameState *g);

#endif
