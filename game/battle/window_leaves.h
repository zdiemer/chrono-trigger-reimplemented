/* Battle: what the window's routines leave in the registers for the
 * original's code.
 *
 * Some of the original's callers of the window's routines go on with the
 * carry (and A) as the routine left it, and liveness can't show that they
 * don't read it; the menu window's calls to the original's code, which pass
 * the carry through, carry it on to where it is read. It follows the path the
 * routine took, so it is worked out here, beside the entries, from the state
 * before the call (noted first, under vars_peek) and after it. The game's
 * logic never looks at any of it. */
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
/* The carry, and whether A was left clear (anything drawn). */
bool party_status_carry(const GameState *g, PartyStatusNote note);
bool party_status_clears_a(const GameState *g, PartyStatusNote note);

#endif
