/* Battle: the arithmetic the battle's code shares (battle/arith.h), and the
 * original's two bit-serial routines for numbers wider than the multiplier and
 * divider take, which only the original's code calls. */
#include "battle/arith.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "vars.h"

/* ---- Through the multiplier and the divider ------------------------------------------------ */

/* The cells of battle_long_multiply and battle_division, by the byte. */
enum { LONG_FACTOR = 0, LONG_OTHER = 2, LONG_PRODUCT = 4 };
enum { DIVIDEND = 0, DIVISOR = 2, QUOTIENT = 4, REMAINDER = 6 };

static void leave_word(GameState *g, void (*set)(GameState *, unsigned, uint8_t), unsigned at, uint16_t v)
{
    set(g, at, (uint8_t)v);
    set(g, at + 1, (uint8_t)(v >> 8));
}

/* The operands the multiplier was last given, which the interrupt handlers
 * put back after their own arithmetic. */
static void leave_multiplier(GameState *g, uint8_t a, uint8_t b)
{
    set_battle_multiplier_operands(g, 0, a);
    set_battle_multiplier_operands(g, 1, b);
    ORIGINAL_EFFECT(hw_set_multiplicand(g, a));
    ORIGINAL_EFFECT(hw_multiply_by(g, b));
}

static void leave_multiply(GameState *g, uint8_t a, uint8_t b, uint16_t product)
{
    set_battle_multiply_factors(g, 0, a);
    set_battle_multiply_factors(g, 1, b);
    set_battle_product(g, product);
    leave_multiplier(g, a, b);
}

// $C1:0089
uint16_t battle_multiply(GameState *g, uint8_t a, uint8_t b)
{
    uint16_t product = (uint16_t)(a * b);
    leave_multiply(g, a, b, product);
    return product;
}

/* It multiplies by the word's low byte and then its high byte. */
static void leave_long_multiply(GameState *g, uint8_t a, uint16_t b, uint32_t product)
{
    set_battle_long_multiply(g, LONG_FACTOR, a);
    leave_word(g, set_battle_long_multiply, LONG_OTHER, b);
    leave_word(g, set_battle_long_multiply, LONG_PRODUCT, (uint16_t)product);
    set_battle_long_multiply(g, LONG_PRODUCT + 2, (uint8_t)(product >> 16));
    leave_multiplier(g, a, (uint8_t)b);
    leave_multiplier(g, a, (uint8_t)(b >> 8));
}

// $C1:00A7
uint32_t battle_multiply_long(GameState *g, uint8_t a, uint16_t b)
{
    uint32_t product = (uint32_t)a * b;
    leave_long_multiply(g, a, b, product);
    return product;
}

/* The divider's operands, kept for the interrupt handlers while the division
 * runs (battle_divider_busy, which is clear again once it is done). */
static void leave_divide(GameState *g, uint16_t dividend, uint8_t divisor, BattleQuotient q)
{
    leave_word(g, set_battle_division, DIVIDEND, dividend);
    set_battle_division(g, DIVISOR, divisor);
    leave_word(g, set_battle_division, QUOTIENT, q.quotient);
    leave_word(g, set_battle_division, REMAINDER, q.remainder);
    set_battle_divider_operands(g, 0, (uint8_t)dividend);
    set_battle_divider_operands(g, 1, (uint8_t)(dividend >> 8));
    set_battle_divider_operands(g, 2, divisor);
    ORIGINAL_EFFECT(hw_set_dividend(g, dividend));
    ORIGINAL_EFFECT(hw_divide_by(g, divisor));
    set_battle_divider_busy(g, 0);
}

// $C1:00D7
BattleQuotient battle_divide_by(GameState *g, uint16_t dividend, uint8_t divisor)
{
    BattleQuotient q = divisor ? (BattleQuotient){ (uint16_t)(dividend / divisor), (uint16_t)(dividend % divisor) }
                               : (BattleQuotient){ 0xFFFF, dividend };
    leave_divide(g, dividend, divisor, q);
    return q;
}

/* ---- Wider numbers, for the original's code ----------------------------------------------- */

/* The cells of battle_wide_math, by the byte. */
enum { FIRST = 0, SECOND = 2, RESULT_LOW = 4, RESULT_HIGH = 6, WIDE_REMAINDER = 10 };

static uint16_t wide_word(const GameState *g, unsigned at)
{
    return (uint16_t)(battle_wide_math(g, at) | battle_wide_math(g, at + 1) << 8);
}
static void set_wide_word(GameState *g, unsigned at, uint16_t v) { leave_word(g, set_battle_wide_math, at, v); }

// $C1:C90B
/* A word times a word, a 32-bit product. */
static uint32_t multiply_words(uint16_t a, uint16_t b) { return (uint32_t)a * b; }

/* The long arithmetic's leftover high word ($2E): a word-by-word product's
 * high word, or what a long division left of its dividend's (below). The
 * random draw (battle/random.c) divides with it as its dividend's high word,
 * so it is game state, kept by every producer, not scaffolding. */
static void keep_high_word_left(GameState *g, uint16_t v) { set_wide_word(g, RESULT_HIGH, v); }

uint16_t battle_long_high_word(const GameState *g) { return wide_word(g, RESULT_HIGH); }

/* It shifts its second number out bit by bit, and the carry the caller had in
 * as its first bit, which is all of it that is left: the cell is clear but for
 * that bit, whose value nothing reads (battle_leave_multiply_carry). */
static void leave_word_product(GameState *g, uint16_t a, uint32_t product)
{
    set_wide_word(g, FIRST, a);
    set_battle_wide_math(g, SECOND + 1, 0);
    ORIGINAL_EFFECT(set_battle_wide_math(g, SECOND, 0));
    set_wide_word(g, RESULT_LOW, (uint16_t)product);
}

void battle_leave_multiply_carry(GameState *g, bool carry) { set_battle_wide_math(g, SECOND, carry); }

uint32_t battle_multiply_words(GameState *g, uint16_t a, uint16_t b)
{
    uint32_t product = multiply_words(a, b);
    keep_high_word_left(g, (uint16_t)(product >> 16));
    leave_word_product(g, a, product);
    return product;
}

uint16_t battle_wide_math_quotient(const GameState *g) { return wide_word(g, RESULT_LOW); }
uint16_t battle_wide_math_first(const GameState *g) { return wide_word(g, FIRST); }
void battle_set_wide_math_quotient(GameState *g, uint16_t v) { set_wide_word(g, RESULT_LOW, v); }

// $C1:C92A
/* A long divided by a word, a 32-bit quotient and the remainder. Nothing is
 * divided when the dividend's low word is 0, whatever its high word, nor by
 * 0: the quotient and the remainder are 0 then. */
typedef struct {
    uint32_t quotient;
    uint16_t remainder;
} WideQuotient;

/* The division shifts the quotient in behind the dividend: its bits past the
 * dividend's low word are what it leaves in the first number's cell. */
enum { QUOTIENT_BITS_LEFT_BEHIND = 17 };

static WideQuotient divide_long(uint32_t dividend, uint16_t divisor)
{
    if ((uint16_t)dividend == 0 || divisor == 0)
        return (WideQuotient){ 0, 0 };
    return (WideQuotient){ dividend / divisor, (uint16_t)(dividend % divisor) };
}

static bool divides(uint32_t dividend, uint16_t divisor) { return (uint16_t)dividend != 0 && divisor != 0; }

/* What a long division leaves of its dividend's high word: all of it where it
 * didn't divide; where it did, the dividend is shifted out through it and the
 * caller's carry comes in at its top. */
static uint16_t division_high_word_left(uint32_t dividend, uint16_t divisor, bool carry_in)
{
    if (!divides(dividend, divisor))
        return (uint16_t)(dividend >> 16);
    return carry_in ? 0x8000 : 0;
}

/* What $C1:C92A leaves in its other cells, besides the quotient: the numbers
 * as it was handed them and the remainder, and where it divided, the dividend
 * shifted out through the first number's cell with the quotient's top bits
 * behind it. */
static void leave_long_division(GameState *g, uint32_t dividend, uint16_t divisor, WideQuotient q)
{
    set_wide_word(g, SECOND, divisor);
    set_wide_word(g, WIDE_REMAINDER, q.remainder);
    set_wide_word(g, FIRST, divides(dividend, divisor) ? (uint16_t)(q.quotient >> QUOTIENT_BITS_LEFT_BEHIND) : (uint16_t)dividend);
}

static BattleLongQuotient divide_leftover(GameState *g, uint16_t low, uint16_t divisor, bool top_bit)
{
    uint32_t dividend = (uint32_t)battle_long_high_word(g) << 16 | low;
    WideQuotient q = divide_long(dividend, divisor);
    set_wide_word(g, RESULT_LOW, (uint16_t)q.quotient);
    set_wide_word(g, WIDE_REMAINDER, q.remainder);
    keep_high_word_left(g, division_high_word_left(dividend, divisor, top_bit));
    leave_long_division(g, dividend, divisor, q);
    return (BattleLongQuotient){ q.quotient, q.remainder };
}

BattleLongQuotient battle_divide_leftover(GameState *g, uint16_t low, uint16_t divisor)
{
    return divide_leftover(g, low, divisor, false);
}

BattleLongQuotient battle_divide_leftover_marked(GameState *g, uint16_t low, uint16_t divisor)
{
    return divide_leftover(g, low, divisor, true);
}

BattleLongQuotient battle_divide_long(GameState *g, uint32_t dividend, uint16_t divisor)
{
    WideQuotient q = divide_long(dividend, divisor);
    keep_high_word_left(g, division_high_word_left(dividend, divisor, false));
    leave_long_division(g, dividend, divisor, q);
    return (BattleLongQuotient){ q.quotient, q.remainder };
}

/* ---- The entries ------------------------------------------------------------------------ */

/* Each takes its numbers from its cells, as the original's callers hand them,
 * and leaves A as the original's last TDC leaves it: the battle's page. */
void battle_multiply_8x8(Snes *s)
{
    GameState *g = state_of(s);
    battle_multiply(g, battle_multiply_factors(g, 0), battle_multiply_factors(g, 1));
    s->cpu.a = BATTLE_PAGE;
    m_rts(s);
}

/* It leaves the product by the word's low byte in X. */
void battle_multiply_8x16(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t a = battle_long_multiply(g, LONG_FACTOR);
    uint16_t b = (uint16_t)(battle_long_multiply(g, LONG_OTHER) | battle_long_multiply(g, LONG_OTHER + 1) << 8);
    battle_multiply_long(g, a, b);
    s->cpu.x = (uint16_t)(a * (uint8_t)b);
    s->cpu.a = BATTLE_PAGE;
    m_rts(s);
}

void battle_multiply_16x16(Snes *s)
{
    GameState *g = state_of(s);
    battle_multiply_words(g, wide_word(g, FIRST), wide_word(g, SECOND));
    /* the carry its caller came in with, which the caller's next division can
     * take as its divisor */
    battle_leave_multiply_carry(g, m_flag(s, CPU_FLAG_C));
    s->cpu.a = BATTLE_PAGE;
    m_set_flag(s, CPU_FLAG_C, false);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

/* It shifts the dividend out through its cells, the quotient's top fifteen
 * bits coming in behind it (the next one is lost) and the caller's carry in at
 * the top; its flags come back as they were. */
void battle_divide(Snes *s)
{
    GameState *g = state_of(s);
    /* the caller's carry picks the high word the division leaves */
    if (m_flag(s, CPU_FLAG_C))
        battle_divide_leftover_marked(g, wide_word(g, FIRST), wide_word(g, SECOND));
    else
        battle_divide_leftover(g, wide_word(g, FIRST), wide_word(g, SECOND));
    s->cpu.a = BATTLE_PAGE;
    m_rts(s);
}

/* The bit-serial division loses the remainder's top bit when the divisor has
 * its own set: the game divides by less. */
enum { LARGEST_DIVISOR = 0x8000 };

bool battle_divide_is_exact(Snes *s)
{
    vars_peek(true);
    uint16_t divisor = wide_word(state_of(s), SECOND);
    vars_peek(false);
    return battle_page(s) && divisor <= LARGEST_DIVISOR;
}
