/* Battle: the battle's random numbers (battle/random.h). */
#include "charge.h"
#include "battle/random.h"

#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/rules_data.h"
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
    if (lowest == NO_DRAW)
        return limit;
    /* the limit is kept plainly: a party tech's test of whether it stops after
     * a miss reads a byte of work RAM by the tech's number (battle/
     * party_work.c, stops_after_miss), and for tech $1B that byte is this */
    set_battle_random_limit(g, limit);
    if (battle_is_demo(g))
        return lowest;
    if (limit == 0 || limit == lowest)
        return limit;
    uint8_t width = (uint8_t)(limit - lowest), number = battle_random_number(g, position(g, p));
    if (width == WHOLE_TABLE)
        return number;
    uint8_t drawn = (uint8_t)(lowest + over_range(g, number, width));
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

enum { QUOTIENT_CELL = 4 };
