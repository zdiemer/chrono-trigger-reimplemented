/* Battle: the flag the window's routines leave their callers, as the
 * original's leave it.
 *
 * It follows the path the routine took, so it is worked out here from the
 * state before the call (noted first, under vars_peek) and after it. The
 * menu window hands it on to its calls, and nothing reads it now. */
#ifndef CT_GAME_BATTLE_WINDOW_LEAVES_H
#define CT_GAME_BATTLE_WINDOW_LEAVES_H

#include <stdbool.h>

#include "battle/window.h"
#include "state.h"

/* The window's redraw ($C1:0299): from the state before it. */
bool window_redraw_carry(const GameState *g);

/* The party status ($C1:05A7): noted before, and read after. */
typedef struct {
    bool numbers;       /* whether it redraws a member's numbers */
    bool went_round;    /* whether counting the member on went round */
} PartyStatusNote;

PartyStatusNote note_party_status(const GameState *g);
/* The flag it leaves. */
bool party_status_carry(const GameState *g, PartyStatusNote note);

#endif
