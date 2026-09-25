/* Battle: the overflow flag the original's arithmetic leaves (scaffolding).
 *
 * The original's adds and subtractions leave the processor's overflow flag
 * as their last result had it, and an enemy's effect worked out hands it on
 * to the action's showing ($C1:BFA4), which saves it with the flags across its
 * frame waits. Nothing in the battle's code branches on it (the only overflow
 * tests in the ROM are the menus'), so it has no meaning in the game; but the
 * showing runs animation commands through a table ($C1:75D1) whose targets
 * the liveness analysis can't follow, so a don't-care declaration can't drop
 * it, and a fuzz run can't show it dead either (the showing waits on frames
 * past any budget). So it is scaffolding (game/original_effects.h): the
 * arithmetic that sets it notes what it leaves, each note inside
 * ORIGINAL_EFFECT, and the entry that hands it on takes the last one. The
 * logic never reads it. */
#ifndef CT_GAME_BATTLE_OVERFLOW_H
#define CT_GAME_BATTLE_OVERFLOW_H

#include <stdbool.h>
#include <stdint.h>

/* Start noting, and what was noted since (or `before`, where nothing was). */
void battle_forget_overflow(void);
void battle_leave_overflow(bool overflow);
bool battle_overflow_left(bool before);

/* The signed overflow of an 8-bit and a 16-bit add (a subtraction is the add
 * of the complement and the carry). */
static inline bool battle_add_overflows8(uint8_t a, uint8_t b, bool carry)
{
    uint8_t sum = (uint8_t)(a + b + carry);
    return ((a ^ sum) & (b ^ sum) & 0x80) != 0;
}
static inline bool battle_add_overflows16(uint16_t a, uint16_t b, bool carry)
{
    uint16_t sum = (uint16_t)(a + b + carry);
    return ((a ^ sum) & (b ^ sum) & 0x8000) != 0;
}

/* Notes of the common cases. */
static inline void battle_leave_add_overflow8(uint8_t a, uint8_t b) { battle_leave_overflow(battle_add_overflows8(a, b, false)); }
static inline void battle_leave_add_overflow16(uint16_t a, uint16_t b)
{
    battle_leave_overflow(battle_add_overflows16(a, b, false));
}

#endif
