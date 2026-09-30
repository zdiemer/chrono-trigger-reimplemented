/* Battle: the arithmetic the battle's code shares.
 *
 * The original multiplies and divides small numbers with the CPU's multiplier
 * and divider, through subroutines that take their numbers in direct-page
 * cells and leave the result there, keeping copies of the operands for the
 * interrupt handlers, which do arithmetic of their own and put the operands
 * back (battle/nmi.c). A port computes in C; these return the result, and
 * leave what the original's routine leaves -- the cells, the copies, the
 * multiplier's and divider's state. The cells and the copies are game state,
 * kept plainly: the original's callers multiply and divide what the last
 * multiply or divide left in them, and the interrupt handlers put the copies
 * back; only the hardware's state is scaffolding (game/original_effects.h). A
 * port that ran the routine's work inline in a caller's call to it leaves the
 * same, so the caller's port matches the original's caller. */
#ifndef CT_GAME_BATTLE_ARITH_H
#define CT_GAME_BATTLE_ARITH_H

#include <stdbool.h>
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

/* $C1:C92A: a long divided by a word, with the remainder. Nothing is divided
 * when the dividend's low word is 0, whatever its high word, nor by 0: the
 * quotient and the remainder are 0 then. The numbers are left in the cells
 * the original's callers hand them over in, as its code leaves them, except
 * the quotient's cell, which the random draw (battle/random.h) saves and puts
 * back around its division. */
typedef struct {
    uint32_t quotient;
    uint16_t remainder;
} BattleLongQuotient;
BattleLongQuotient battle_divide_long(GameState *g, uint32_t dividend, uint16_t divisor);

/* $C1:C90B: a word times a word, a 32-bit product, the numbers and the
 * product left in the cells the original's callers use, as its code leaves
 * them with the caller's carry clear. */
uint32_t battle_multiply_words(GameState *g, uint16_t a, uint16_t b);

/* A word-by-word multiply the original makes only to find a place in a
 * table, whose cells the next arithmetic's caller finds as it left them. */
static inline void battle_leave_multiply(GameState *g, uint16_t a, uint16_t b) { (void)battle_multiply_words(g, a, b); }

/* The long arithmetic's leftover high word: the last word-by-word product's
 * high word, or what the last long division left of its dividend's. Game
 * state: the random draw divides with it (battle/random.c). */
uint16_t battle_long_high_word(const GameState *g);

/* $C1:C92A as the effect code calls it (battle/effect_parts.h): the leftover
 * high word and `low` over `divisor`, so that a product just made is divided
 * whole; the quotient's low word is left in its cell as well. Where it
 * divides, the high word it leaves is clear. */
BattleLongQuotient battle_divide_leftover(GameState *g, uint16_t low, uint16_t divisor);

/* The same where the original's caller comes in with its carry set: the high
 * word the division leaves has its top bit set, which the next division then
 * divides with (two callers do: the evasion's level bonus, battle/
 * effect_amounts.c, and Ayla's fists, battle/fight_party.c). */
BattleLongQuotient battle_divide_leftover_marked(GameState *g, uint16_t low, uint16_t divisor);

/* What the cells hold of the last call, which a few of the effect code's
 * steps read back as they are: the quotient's (or the product's) low word,
 * and the first number. */
uint16_t battle_wide_math_quotient(const GameState *g);
/* The high byte of the second number's cell: what a caller that sets only
 * the cell's low byte divides by above it (battle/status_timers.c's poison). */
uint8_t battle_wide_math_second_high(const GameState *g);
uint16_t battle_wide_math_first(const GameState *g);
void battle_set_wide_math_quotient(GameState *g, uint16_t v);
/* A number a caller puts in the first number's cell without multiplying it
 * (the level-up's first-level stat, the tech points' cost), which the next
 * multiply or divide overwrites. */
void battle_set_wide_math_first(GameState *g, uint16_t v);

/* The word-by-word multiply leaves its second number's cell clear but for the
 * carry its caller came in with: scaffolding, for the callers whose carry is
 * set (the multiply itself leaves it 0). */
void battle_leave_multiply_carry(GameState *g, bool carry);

#endif
