/* Scenes: the arithmetic the scene programs share.
 *
 * The scene programs work out their motion in fixed point -- the attract's
 * pendulum and its Mode 7 matrix, the race's perspective, the endings' zooms --
 * and pass operands and results to these helpers in sixteen bytes of scratch on
 * the caller's page (scene_math_scratch: the scene's own page, or the page of
 * the sequence channel whose script called the program). They are:
 *
 * - multiply: 16 x 16 bits into a 32-bit product;
 * - multiply_signed: a signed 16-bit value times a signed byte;
 * - divide: 32 by 32 bits, into a quotient and a remainder;
 * - divide_8_8: a value's high byte by a 16-bit divisor, as an 8.8 fixed-point
 *   fraction (how far along a distance something is);
 * - find_divisor: the first divisor from a starting one whose quotient falls
 *   under a limit (the race's scale for a distance).
 *
 * Each is C arithmetic. What the original's way of computing them leaves in the
 * result is reproduced where a caller can see it, each in a function of its own
 * that says what it is: a carry that reaches the product from the byte after
 * it, a byte the signed multiply takes from the product cell as the caller left
 * it, and the long divisions' 16- and 32-bit remainders, which differ from exact
 * division only for divisors no scene uses. The CPU's multiplier and divider
 * are left as the original leaves them, for its own code and shadow mode
 * (game/cpu_io.h). */
#include "scene/scene.h"

#include "cpu_io.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- multiply ------------------------------------------------------------------------------ */

/* The original adds its two middle partial products and carries their sum's
 * overflow into the top byte with a rotate over the cell after the product,
 * which shifts that byte left and brings its top bit into the product. */
enum { CARRIED_INTO_PRODUCT = 0x100 };

static uint32_t carry_from_the_byte_after(GameState *g, SceneMathPage page)
{
    uint8_t after = scene_math_scratch_quotient_first_byte(g, page);
    set_scene_math_scratch_quotient_first_byte(g, page, (uint8_t)(after << 1));
    return (after & 0x80) ? CARRIED_INTO_PRODUCT : 0;
}

/* The multiplier as the original leaves it: its last partial product, the
 * high bytes of the two operands. */
static void leave_multiplier(GameState *g, uint8_t multiplicand, uint8_t multiplier)
{
    hw_set_multiplicand(g, multiplicand);
    hw_multiply_by(g, multiplier);
}

// $7E:31BF
/* The product of the two operands. */
static void multiply(GameState *g, SceneMathPage page)
{
    uint16_t a = scene_math_scratch_first(g, page), b = scene_math_scratch_second(g, page);
    uint32_t product = (uint32_t)a * b + carry_from_the_byte_after(g, page);
    set_scene_math_scratch_product(g, page, product);
    ORIGINAL_EFFECT(leave_multiplier(g, (uint8_t)(b >> 8), (uint8_t)(a >> 8)));
}

/* ---- multiply_signed ------------------------------------------------------------------------ */

static uint16_t magnitude16(uint16_t v) { return (v & 0x8000) ? (uint16_t)-v : v; }
static uint8_t magnitude8(uint8_t v) { return (v & 0x80) ? (uint8_t)-v : v; }

/* The 24 bits of the product's magnitude the original computes, and the byte
 * above them it clears: it adds the second partial product into the product
 * cell's second and third bytes as they stand, so the third byte the caller
 * left there is added in too. */
static uint32_t magnitude_with_leftover(uint32_t magnitude, uint8_t leftover)
{
    return (magnitude + ((uint32_t)leftover << 16)) & 0x00FFFFFF;
}

// $C3:094C
/* The first operand, signed, times the second's low byte, signed: the
 * magnitudes through the multiplier, and the product negated when the signs
 * differ. */
static void multiply_signed(GameState *g, SceneMathPage page)
{
    uint16_t a = scene_math_scratch_first(g, page);
    uint8_t b = (uint8_t)scene_math_scratch_second(g, page);
    uint8_t leftover = (uint8_t)(scene_math_scratch_product(g, page) >> 16);
    bool negative = ((a >> 8) ^ b) & 0x80;
    uint16_t size_a = magnitude16(a);
    uint8_t size_b = magnitude8(b);
    uint32_t product = magnitude_with_leftover((uint32_t)size_a * size_b, leftover);
    set_scene_math_scratch_product(g, page, negative ? (uint32_t)-product : product);
    ORIGINAL_EFFECT(leave_multiplier(g, size_b, (uint8_t)(size_a >> 8)));
}

/* ---- divide ----------------------------------------------------------------------------------- */

typedef struct {
    uint32_t quotient, remainder;
} Division;

/* A divisor with its top bit set, or 0. The original divides bit by bit into a
 * 32-bit remainder, which then loses the bit it shifts out; and a divisor of 0
 * subtracts nothing, so every bit of the quotient is set and the remainder is
 * the dividend. No scene divides by either -- its divisors are distances --
 * but a caller could, so this is the original's long division as it runs. */
static Division long_division_in_32_bits(uint32_t dividend, uint32_t divisor)
{
    Division d = { 0, 0 };
    for (int bit = 31; bit >= 0; bit--) {
        d.remainder = d.remainder << 1 | (dividend >> bit & 1);   /* its top bit lost */
        d.quotient <<= 1;
        if (d.remainder >= divisor) {
            d.remainder -= divisor;
            d.quotient |= 1;
        }
    }
    return d;
}

enum { TOP_BIT_32 = 0x80000000u };

// $7E:339F
/* The dividend by the divisor, into the quotient and the remainder. */
static void divide(GameState *g, SceneMathPage page)
{
    uint32_t dividend = scene_math_scratch_dividend(g, page), divisor = scene_math_scratch_divisor(g, page);
    Division d = (divisor == 0 || (divisor & TOP_BIT_32)) ? long_division_in_32_bits(dividend, divisor)
                                                          : (Division){ dividend / divisor, dividend % divisor };
    set_scene_math_scratch_quotient(g, page, d.quotient);
    set_scene_math_scratch_remainder(g, page, d.remainder);
}

/* The original shifts the dividend out of its cell as it goes, bringing the
 * caller's carry in at the bottom first, so the cell ends with that carry in its
 * top bit. */
static void leave_dividend_shifted_out(GameState *g, SceneMathPage page, bool carry)
{
    set_scene_math_scratch_dividend(g, page, carry ? TOP_BIT_32 : 0);
}

/* ---- divide_8_8 ------------------------------------------------------------------------------ */

/* A divisor past $8000, or 0. The original's remainder is 16 bits, and the bit
 * it shifts out of the top counts as one more (it subtracts the divisor less
 * one, borrowing that bit); a divisor of 0 counts as $10000. No scene divides by
 * either, but a caller could, so this is the original's long division as it runs,
 * with the dividend's high byte and then eight zeros shifted in. */
static uint16_t fraction_in_16_bits(uint16_t dividend, uint16_t divisor)
{
    uint32_t by = divisor ? divisor : 0x10000u;
    uint16_t remainder = 0, fraction = 0;
    for (int bit = 15; bit >= 0; bit--) {
        uint32_t shifted = (uint16_t)(remainder << 1 | (bit >= 8 ? dividend >> bit & 1 : 0));
        if (remainder & 0x8000)
            shifted++;
        fraction = (uint16_t)(fraction << 1);
        if (shifted >= by) {
            shifted -= by;
            fraction |= 1;
        }
        remainder = (uint16_t)shifted;
    }
    return fraction;
}

enum { WHOLE_PART = 0xFF00, LARGEST_EXACT_DIVISOR = 0x8000 };

// $7E:32E2
/* How many times the divisor goes into the dividend's high byte, as an 8.8
 * fixed-point fraction (the dividend's low byte is dropped). The divisor less
 * one is left in the quotient cell, whose first byte the next multiply reads. */
static void divide_8_8(GameState *g, SceneMathPage page, uint16_t dividend)
{
    uint16_t divisor = scene_math_scratch_second(g, page);
    set_scene_math_scratch_divisor_less_one(g, page, (uint16_t)(divisor - 1));
    uint16_t fraction = (divisor != 0 && divisor <= LARGEST_EXACT_DIVISOR)
                            ? (uint16_t)((dividend & WHOLE_PART) / divisor)
                            : fraction_in_16_bits(dividend, divisor);
    set_scene_math_scratch_fraction(g, page, fraction);
}

/* The original keeps the dividend in the first operand's cell and shifts its
 * high byte out as it goes, bringing the caller's carry in first. */
static void leave_dividend_high_byte_shifted_out(GameState *g, SceneMathPage page, uint16_t dividend, bool carry)
{
    set_scene_math_scratch_first(g, page, (uint16_t)(dividend << 8 | (carry ? 0x80 : 0)));
}

/* ---- find_divisor --------------------------------------------------------------------------- */

/* A divisor through the CPU's 16/8 divider, where 0 gives $FFFF. */
static uint16_t quotient_by(uint16_t dividend, uint8_t divisor) { return divisor ? dividend / divisor : 0xFFFF; }

typedef struct {
    bool found;
    uint8_t after;      /* the divisor after the one found */
    uint16_t quotient;  /* the quotient of the one found */
} FoundDivisor;

/* The 8-bit divisors wrap after 256, and the search is then back where it
 * started: a quotient that never falls under the limit never ends it. */
enum { DIVISORS = 0x100 };

// $C3:0E29
/* Try divisors from the one after `tried` (the caller's own division, already
 * made) until a quotient of the divider's dividend falls under the limit (on
 * the scene's own page, reached through the data bank). The divider is left
 * dividing by the divisor after the one found, as the original leaves it. */
static FoundDivisor find_divisor(GameState *g, uint8_t tried)
{
    uint16_t limit = scene_math_scratch_quotient_limit(g, scene_math_scene());
    uint16_t dividend = hw_dividend(g);
    uint16_t quotient = hw_quotient(g);
    FoundDivisor f = { false, (uint8_t)(tried + 1), quotient };
    for (unsigned n = 0; n < DIVISORS; n++) {
        uint8_t next = (uint8_t)(tried + 1);
        if (quotient < limit) {
            f = (FoundDivisor){ true, next, quotient };
            break;
        }
        quotient = quotient_by(dividend, next);
        tried = next;
    }
    ORIGINAL_EFFECT(hw_divide_by(g, f.after));
    return f;
}

/* ---- Entries --------------------------------------------------------------------------------- */

/* Entered with 16-bit A and X; P, the data bank, X and Y come back as they
 * were, and nothing else it leaves is read. */
void scene_multiply(Snes *s)
{
    multiply(state_of(s), scene_math_page_at_entry(s));
    m_rts(s);
}

/* Entered with 16-bit index registers; P comes back as it was. X is read after
 * it: the original leaves the first partial product there, the magnitudes'
 * low bytes multiplied. */
void scene_multiply_signed(Snes *s)
{
    GameState *g = state_of(s);
    SceneMathPage page = scene_math_page_at_entry(s);
    uint16_t a = magnitude16(scene_math_scratch_first(g, page));
    uint8_t b = magnitude8((uint8_t)scene_math_scratch_second(g, page));
    multiply_signed(g, page);
    s->cpu.x = (uint16_t)((a & 0xFF) * b);
    m_rtl(s);
}

/* Entered with 16-bit A and X and the carry, which the dividend's cell ends
 * with; only Y is read after it, and it doesn't touch Y. */
void scene_divide(Snes *s)
{
    GameState *g = state_of(s);
    SceneMathPage page = scene_math_page_at_entry(s);
    divide(g, page);
    ORIGINAL_EFFECT(leave_dividend_shifted_out(g, page, m_flag(s, CPU_FLAG_C)));
    m_rts(s);
}

/* Entered with 16-bit A, the dividend, and the carry. Back, the carry is read:
 * the last of the fraction's rotates leaves it, and it is always clear (sixteen
 * rotates of a cell cleared first shift out one of its zeros). */
void scene_divide_8_8(Snes *s)
{
    GameState *g = state_of(s);
    SceneMathPage page = scene_math_page_at_entry(s);
    uint16_t dividend = s->cpu.a;
    divide_8_8(g, page, dividend);
    ORIGINAL_EFFECT(leave_dividend_high_byte_shifted_out(g, page, dividend, m_flag(s, CPU_FLAG_C)));
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* The original reaches the divider through the direct page. */
bool scene_find_divisor_on_divider_page(Snes *s)
{
    return m_direct_page(s) == M_DP_CPU && m_bank_reaches_low_ram(s);
}

/* Entered with 16-bit A and 8-bit index registers, X the divisor the caller
 * tried; back, X is the divisor after the one found and A its quotient, with
 * the carry clear (the loop's compare). One that is never found loops forever
 * in the original; the port stops after the first pass, where the search is
 * after all 256. */
void scene_find_divisor(Snes *s)
{
    FoundDivisor f = find_divisor(state_of(s), (uint8_t)s->cpu.x);
    s->cpu.x = f.after;
    s->cpu.a = f.quotient;
    m_set_flag(s, CPU_FLAG_C, !f.found);
    if (!f.found) {
        m_stop_at(s, 0xC30E29, DC_N | DC_Z);
        return;
    }
    m_rtl(s);
}

/* ---- Guards: the helpers run from the program in RAM ----------------------------------------- */

bool scene_multiply_present(Snes *s)
{
    return scene_on_math_page(s) && m_code_matches(s, 0x7E31BF, 74, 0x479627EBF1311CAFull);
}

bool scene_divide_8_8_present(Snes *s)
{
    return scene_on_math_page(s) && m_code_matches(s, 0x7E32E2, 189, 0xD04B8C1274AF93C8ull);
}

bool scene_divide_present(Snes *s)
{
    return scene_on_math_page(s) && m_code_matches(s, 0x7E339F, 55, 0x03E628A5184CD799ull);
}
