/* Battle: the arithmetic the battle's code shares.
 *
 * The original multiplies and divides small numbers with the CPU's multiplier
 * and divider, through subroutines that take their numbers in direct-page
 * cells and leave the result there, keeping copies of the operands for the
 * interrupt handlers, which do arithmetic of their own and put the operands
 * back (battle/nmi.c). A port computes in C; these return the result, and
 * leave what the original's routine leaves -- the cells, the copies, the
 * multiplier's and divider's state -- for the original's code and shadow mode
 * (scaffolding, game/original_effects.h). A port that ran the routine's work
 * inline in a caller's call to it leaves the same, so the caller's port
 * matches the original's caller. */
#ifndef CT_GAME_BATTLE_ARITH_H
#define CT_GAME_BATTLE_ARITH_H

#include <stdint.h>

#include "state.h"

/* $C1:0089: a byte times a byte. */
uint16_t battle_multiply(GameState *g, uint8_t a, uint8_t b);

/* $C1:00A7: a byte times a word, a 24-bit product. */
uint32_t battle_multiply_long(GameState *g, uint8_t a, uint16_t b);

/* $C1:00D7: a word divided by a byte, with the remainder. A divisor of 0 gives
 * what the divider gives for it: a quotient of $FFFF and the dividend back. */
typedef struct {
    uint16_t quotient, remainder;
} BattleQuotient;
BattleQuotient battle_divide_by(GameState *g, uint16_t dividend, uint8_t divisor);

#endif
