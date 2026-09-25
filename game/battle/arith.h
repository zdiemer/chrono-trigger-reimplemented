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

/* The long arithmetic's leftover high word: the last word-by-word product's
 * high word, or what the last long division left of its dividend's. Game
 * state: the random draw divides with it (battle/random.c). */
uint16_t battle_long_high_word(const GameState *g);

/* The two as the effect code calls them (battle/effect_parts.h), which leaves
 * in the cells what its instructions before each call leave: the carry goes
 * into the multiply's leftovers, and into the top of the high word a division
 * leaves. The division divides the leftover high word and `low`, and leaves
 * the quotient's low word in its cell as well. */
uint32_t battle_multiply_words_carrying(GameState *g, uint16_t a, uint16_t b, bool carry);
BattleLongQuotient battle_divide_carrying(GameState *g, uint16_t low, uint16_t divisor, bool carry);

/* What the cells hold of the last call: its second number (a division's
 * divisor), and the quotient's (or product's) low word, which a few of the
 * effect code's steps read back as they are. */
uint16_t battle_wide_math_second(const GameState *g);
uint16_t battle_wide_math_quotient(const GameState *g);
void battle_set_wide_math_quotient(GameState *g, uint16_t v);

#endif
