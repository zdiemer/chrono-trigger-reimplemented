/* Battle: the overflow flag the original's arithmetic leaves (battle/overflow.h). */
#include "battle/overflow.h"

static bool noted, overflow_noted;

void battle_forget_overflow(void) { noted = false; }

void battle_leave_overflow(bool overflow)
{
    noted = true;
    overflow_noted = overflow;
}

bool battle_overflow_left(bool before) { return noted ? overflow_noted : before; }
