/* The game's state, as a port sees it.
 *
 * A port's logic takes a `GameState *` and reaches the game's variables through
 * game/vars.h, which is generated from re/ram.toml. What the handle points at is
 * the backing's business and opaque here on purpose: emulator builds use work
 * RAM so shadow mode still compares bytes. Native builds move modules into
 * generated C structs behind the same accessors (docs/STATE.md); nothing in
 * the game's logic changes with that storage.
 *
 * `Snes` appears only in a registered entry's wrapper, which turns the machine
 * into a GameState with runtime/port/vars_ram.h's state_of. */
#ifndef CT_GAME_STATE_H
#define CT_GAME_STATE_H

typedef struct GameState GameState;

#endif
