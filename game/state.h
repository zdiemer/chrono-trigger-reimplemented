/* The game's state, as a port sees it.
 *
 * A port's logic takes a `GameState *` and reaches the game's variables through
 * game/vars.h, which is generated from re/ram.toml. What the handle points at is
 * the backing's business and opaque here on purpose: the game's records live
 * in generated C structs behind the accessors (docs/STATE.md), and nothing in
 * the game's logic changes with that storage. */
#ifndef CT_GAME_STATE_H
#define CT_GAME_STATE_H

typedef struct GameState GameState;

#endif
