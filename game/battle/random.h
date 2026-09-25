/* Battle: the battle's random numbers.
 *
 * The battle doesn't compute random numbers: it takes them in turn from a
 * table of 256 in the ROM, one draw a number, from a position it keeps
 * between battles (battle_random_kept). A second position, which starts each
 * battle where the first does, serves the roll for a critical hit.
 *
 * A draw asks for a number from `lowest` up to, not including, `limit`: the
 * table's number over the width of that range, the remainder added to
 * `lowest`. The edges are the original's: asking with `lowest` 255 draws
 * nothing and gives `limit` back; a range of nothing, or a limit of 0, gives
 * the limit; a range 255 wide gives the table's number as it is and leaves
 * the position where it was; and in a demo battle every draw is `lowest`, so
 * that the demo plays the same every time. */
#ifndef CT_GAME_BATTLE_RANDOM_H
#define CT_GAME_BATTLE_RANDOM_H

#include <stdint.h>

#include "state.h"

/* $C1:AF22: a number from the battle's own position. */
uint8_t battle_random(GameState *g, uint8_t lowest, uint8_t limit);

/* $C1:AF79: a number from the second position (the critical hits'). */
uint8_t battle_random_other(GameState *g, uint8_t lowest, uint8_t limit);

#endif
