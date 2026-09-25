/* Battle: the battle's random numbers (battle/random.h). */
#include "battle/random.h"

#include "battle/overflow.h"

#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/rules_data.h"
#include "original_effects.h"
#include "vars.h"

/* The two positions in the table. */
typedef enum { OWN_POSITION, OTHER_POSITION } Position;

static uint8_t position(const GameState *g, Position p)
{
    return p == OWN_POSITION ? battle_random_position(g) : battle_random_other_position(g);
}

static void step_position(GameState *g, Position p)
{
    if (p == OWN_POSITION)
        set_battle_random_position(g, (uint8_t)(battle_random_position(g) + 1));
    else
        set_battle_random_other_position(g, (uint8_t)(battle_random_other_position(g) + 1));
}

/* Asking with this lowest number draws nothing. */
enum { NO_DRAW = 0xFF, WHOLE_TABLE = 0xFF };

/* The table's number over the range's width, as the original divides it: a
 * long division whose dividend's high word is the long arithmetic's leftover
 * (battle_long_high_word): whatever the last long division or product left.
 * The original never clears it, so it moves the number that comes up; a table
 * number of 0 gives 0 whatever it is. */
static uint8_t over_range(GameState *g, uint8_t number, uint8_t width)
{
    uint32_t dividend = (uint32_t)battle_long_high_word(g) << 16 | number;
    return (uint8_t)battle_divide_long(g, dividend, width).remainder;
}

// $C1:AF22, $C1:AF79
static uint8_t draw(GameState *g, Position p, uint8_t lowest, uint8_t limit)
{
    ORIGINAL_EFFECT(set_battle_random_lowest(g, lowest));
    if (lowest == NO_DRAW)
        return limit;
    ORIGINAL_EFFECT(set_battle_random_limit(g, limit));
    if (battle_is_demo(g))
        return lowest;
    if (limit == 0 || limit == lowest)
        return limit;
    uint8_t width = (uint8_t)(limit - lowest), number = battle_random_number(g, position(g, p));
    ORIGINAL_EFFECT(battle_leave_overflow(battle_add_overflows8(limit, (uint8_t)~lowest, true)));
    if (width == WHOLE_TABLE)
        return number;
    uint8_t within = over_range(g, number, width);
    ORIGINAL_EFFECT(battle_leave_add_overflow8(within, lowest));
    uint8_t drawn = (uint8_t)(lowest + within);
    step_position(g, p);
    return drawn;
}

uint8_t battle_random(GameState *g, uint8_t lowest, uint8_t limit) { return draw(g, OWN_POSITION, lowest, limit); }

uint8_t battle_random_other(GameState *g, uint8_t lowest, uint8_t limit)
{
    return draw(g, OTHER_POSITION, lowest, limit);
}

/* ---- The entries ------------------------------------------------------------------------ */

/* The lowest number comes in X's low byte and the limit in A's; the number
 * goes back in A's low byte. The original works with 8-bit index registers
 * inside, which clears their high bytes, and saves and puts back the long
 * division's quotient cell around its division, leaving that word in X. The
 * carry is its last compare's, or the add of the lowest number's. */
static bool draw_carry(const GameState *g, uint8_t lowest, uint8_t limit, uint8_t drawn)
{
    if (lowest == NO_DRAW)
        return true;
    if (battle_is_demo(g))
        return false;
    if (limit == 0 || limit == lowest || (uint8_t)(limit - lowest) == WHOLE_TABLE)
        return true;
    return (uint8_t)(drawn - lowest) + lowest > 0xFF;
}

enum { QUOTIENT_CELL = 4 };

static void draw_entry(Snes *s, Position p)
{
    GameState *g = state_of(s);
    uint8_t lowest = (uint8_t)s->cpu.x, limit = (uint8_t)s->cpu.a;
    uint8_t drawn = draw(g, p, lowest, limit);
    m_set_a8(s, drawn);
    s->cpu.x = (uint16_t)(battle_wide_math(g, QUOTIENT_CELL) | battle_wide_math(g, QUOTIENT_CELL + 1) << 8);
    s->cpu.y &= 0x00FF;
    m_set_flag(s, CPU_FLAG_C, draw_carry(g, lowest, limit, drawn));
    m_rts(s);
}

void battle_draw_random(Snes *s) { draw_entry(s, OWN_POSITION); }
void battle_draw_random_other(Snes *s) { draw_entry(s, OTHER_POSITION); }
