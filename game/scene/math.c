/* Scenes: the arithmetic the scene programs share.
 *
 * The scene programs work out their motion in fixed point -- the title's
 * pendulum and its Mode 7 matrix, the race's perspective, the endings' zooms --
 * and pass operands and results in sixteen bytes on the page they are called on
 * (scene_call: the scene's own page, or the page of the sequence channel whose
 * script called the program). They are:
 *
 * - multiply: 16 x 16 bits into a 32-bit product;
 * - multiply_signed: a signed 16-bit value times a signed byte;
 * - multiply_signed16: two signed 16-bit values, as magnitudes through
 *   multiply (the race's camera, the endings' flight);
 * - divide: 32 by 32 bits, into a quotient and a remainder;
 * - divide_8_8: a value's high byte by a 16-bit divisor, as an 8.8 fixed-point
 *   fraction (how far along a distance something is);
 * - find_divisor: the first divisor after a starting one whose quotient falls
 *   under a limit (the race's scale for a distance);
 * - the sine and the cosine of an angle, 1024 steps a turn, from the game's
 *   table of a quarter wave (scene_data.h).
 *
 * Each is C arithmetic on the operands the game gives it. The original's way of
 * computing them leaves marks on some inputs no scene gives it -- a 32-bit
 * divisor of 0 or past $7FFFFFFF, a product whose middle partial sums overflow
 * -- and each entry's guard hands those to the original. The marks it leaves
 * on inputs the game does give are named inputs: the unsigned multiply carries
 * in the top bit of the byte after its product, and the signed one adds in a
 * byte of the product cell, both as the last routine left them. The CPU's
 * multiplier and divider, and the cells the original only works in, are left
 * as it leaves them, for its own code and shadow mode. */
#include "scene/scene.h"

#include "charge.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "scene/scene_data.h"
#include "vars.h"

/* The multiplier as the original leaves it: its last partial product, the high
 * bytes of the two operands. */
static void leave_multiplier(GameState *g, uint8_t multiplicand, uint8_t multiplier)
{
    hw_set_multiplicand(g, multiplicand);
    hw_multiply_by(g, multiplier);
}

/* ---- multiply ------------------------------------------------------------------------------ */

/* The byte after the product -- whatever the last routine to use it left
 * there: the matrix code generator's source base, a divide's quotient or its
 * divisor less one. The original adds its middle partial products through a
 * rotate of that byte, so the byte's top bit is carried into the product, 256
 * more, and the byte is left shifted a place, for the next multiply. The title's
 * pendulum and the race multiply with it set, some 850 times a title screen. */
enum { CARRIED_IN = 0x100 };

static bool carries_into_the_product(const GameState *g, SceneProgramPage page)
{
    return scene_call_after_the_product(g, page) & 0x80;
}

static void shift_the_byte_after(GameState *g, SceneProgramPage page)
{
    set_scene_call_after_the_product(g, page, (uint8_t)(scene_call_after_the_product(g, page) << 1));
}

// $7E:31BF
/* The product of the two operands, which it also returns. */
static uint32_t multiply(GameState *g, SceneProgramPage page)
{
    uint16_t a = scene_call_first(g, page), b = scene_call_second(g, page);
    bool carried = carries_into_the_product(g, page);
    shift_the_byte_after(g, page);
    uint32_t product = (uint32_t)a * b + (carried ? CARRIED_IN : 0);
    set_scene_call_product(g, page, product);
    ORIGINAL_EFFECT(leave_multiplier(g, (uint8_t)(b >> 8), (uint8_t)(a >> 8)));
    return product;
}

/* The original sums the middle partial products in 16 bits, and one that
 * overflows goes into the product a byte too low; no scene's product does, and
 * the entry's guard hands such a product to the original. */
bool scene_multiplies_exactly(uint16_t a, uint16_t b, bool carried)
{
    uint32_t middle = (((uint32_t)(a & 0xFF) * (b >> 8) + (uint32_t)(a >> 8) * (b & 0xFF)) & 0xFFFF) +
                      ((uint32_t)(a & 0xFF) * (b & 0xFF) >> 8) + (carried ? 1 : 0);
    return middle <= 0xFFFF;
}

static bool multiplies_exactly(const GameState *g, SceneProgramPage page)
{
    return scene_multiplies_exactly(scene_call_first(g, page), scene_call_second(g, page),
                                    carries_into_the_product(g, page));
}

/* ---- multiply_signed ------------------------------------------------------------------------ */

static uint16_t magnitude16(uint16_t v) { return (v & 0x8000) ? (uint16_t)-v : v; }
static uint8_t magnitude8(uint8_t v) { return (v & 0x80) ? (uint8_t)-v : v; }

/* The original adds the second partial product into the product cell's second
 * and third bytes as they stand, so the third byte the caller left there is
 * added in: the product's magnitude is 24 bits with that byte on top. */
static uint32_t magnitude_with_leftover(uint32_t magnitude, uint8_t leftover)
{
    return (magnitude + ((uint32_t)leftover << 16)) & 0x00FFFFFF;
}

// $C3:094C
/* The first operand, signed, times the second's low byte, signed: the
 * magnitudes multiplied, and the product negated when the signs differ.
 * Returns the product. */
static uint32_t multiply_signed(GameState *g, SceneProgramPage page)
{
    uint16_t a = scene_call_first(g, page);
    uint8_t b = (uint8_t)scene_call_second(g, page);
    uint8_t leftover = scene_call_product_third_byte(g, page);
    bool negative = ((a >> 8) ^ b) & 0x80;
    uint16_t size_a = magnitude16(a);
    uint8_t size_b = magnitude8(b);
    uint32_t product = magnitude_with_leftover((uint32_t)size_a * size_b, leftover);
    if (negative)
        product = (uint32_t)-product;
    set_scene_call_product(g, page, product);
    ORIGINAL_EFFECT(leave_multiplier(g, size_b, (uint8_t)(size_a >> 8)));
    return product;
}

/* ---- multiply_signed16 --------------------------------------------------------------------- */

// $7E:33D6
/* A signed 16-bit value times another, as the race's camera turns its
 * distance: the magnitudes multiplied (multiply, above: its carried byte and
 * all, and its time) and the product negated when the signs differ. The
 * operands' cells are left with the magnitudes, as the multiply takes them.
 * Returns the product. */
uint32_t scene_multiply_signed16(GameState *g, SceneProgramPage page, uint16_t a, uint16_t b)
{
    set_scene_call_first(g, page, magnitude16(a));
    set_scene_call_second(g, page, magnitude16(b));
    uint32_t product;
    TIMED_AS(g, 0x7E31BF, product = multiply(g, page));
    if ((a ^ b) & 0x8000) {
        product = (uint32_t)-product;
        set_scene_call_product(g, page, product);
    }
    return product;
}

/* ---- The sine --------------------------------------------------------------------------------- */

// $7E:3465
int16_t scene_sine(const GameState *g, uint16_t angle)
{
    uint16_t step = angle & (SCENE_FULL_TURN - 1);
    int16_t magnitude = scene_sine_magnitude(g, step);
    return step < SCENE_HALF_TURN ? (int16_t)-magnitude : magnitude;
}

// $7E:3461
int16_t scene_cosine(const GameState *g, uint16_t angle) { return scene_sine(g, (uint16_t)(angle + SCENE_QUARTER_TURN)); }

bool scene_sine_second_half(uint16_t angle) { return (angle & (SCENE_FULL_TURN - 1)) >= SCENE_HALF_TURN; }

/* ---- The arithmetic as C calls -------------------------------------------------------------- */

/* For the ports that do the program's arithmetic where it calls it: each sets
 * the operands' cells as the caller does, works on the page's cells as the
 * routine does (its quirks and its leftovers with it) and is timed as it. */

/* A product of two 16-bit operands whose middle partial sums fit
 * (multiplies_exactly), as every caller here gives it. */
uint32_t scene_multiply16(GameState *g, SceneProgramPage page, uint16_t a, uint16_t b)
{
    set_scene_call_first(g, page, a);
    set_scene_call_second(g, page, b);
    uint32_t product;
    TIMED_AS(g, 0x7E31BF, product = multiply(g, page));
    return product;
}

/* A signed `value` (a sine, mostly) scaled by `by` 256ths: its magnitude
 * scaled and the sign put back, so that a negative value rounds toward 0. */
int16_t scene_scale_by_256ths(GameState *g, SceneProgramPage page, int16_t value, uint16_t by)
{
    uint16_t size = value < 0 ? (uint16_t)-value : (uint16_t)value;
    uint16_t product = (uint16_t)(scene_multiply16(g, page, size, by) >> 8);
    return value < 0 ? (int16_t)-product : (int16_t)product;
}

/* A signed 16-bit value times the signed low byte of `b` (whose whole word the
 * caller leaves in the second operand's cell); the product's low word. */
int16_t scene_multiply_by_signed_byte(GameState *g, SceneProgramPage page, uint16_t a, uint16_t b)
{
    set_scene_call_first(g, page, a);
    set_scene_call_second(g, page, b);
    uint32_t product;
    TIMED_AS(g, 0xC3094C, product = multiply_signed(g, page));
    return (int16_t)product;
}

/* ---- divide ----------------------------------------------------------------------------------- */

// $7E:339F
/* The dividend by the divisor, into the quotient and the remainder; the
 * quotient returned. */
static uint32_t divide(GameState *g, SceneProgramPage page)
{
    uint32_t dividend = scene_call_dividend(g, page), divisor = scene_call_divisor(g, page);
    set_scene_call_quotient(g, page, dividend / divisor);
    set_scene_call_remainder(g, page, dividend % divisor);
    return dividend / divisor;
}

/* The original shifts the dividend out of its cell as it goes, bringing the
 * caller's carry in at the bottom first, so the cell ends with that carry in its
 * top bit. */
enum { TOP_BIT_32 = 0x80000000u };

static void leave_dividend_shifted_out(GameState *g, SceneProgramPage page, bool carry)
{
    set_scene_call_dividend(g, page, carry ? TOP_BIT_32 : 0);
}

uint32_t scene_divide32(GameState *g, SceneProgramPage page, uint32_t dividend, uint32_t divisor, bool carry)
{
    set_scene_call_dividend(g, page, dividend);
    set_scene_call_divisor(g, page, divisor);
    uint32_t quotient;
    TIMED_AS(g, 0x7E339F, quotient = divide(g, page));
    ORIGINAL_EFFECT(leave_dividend_shifted_out(g, page, carry));
    return quotient;
}

/* ---- divide_8_8 ------------------------------------------------------------------------------ */

enum { WHOLE_PART = 0xFF00 };

// $7E:32E2
/* How many times the divisor goes into the dividend's high byte, as an 8.8
 * fixed-point fraction (the dividend's low byte is dropped). A divisor of 0
 * counts as 65536, so the fraction is 0: the endings divide by it (637 times
 * over the library). The divisor less one is left after the product, where the
 * next multiply carries in its top bit. */
static void divide_8_8(GameState *g, SceneProgramPage page, uint16_t dividend)
{
    uint16_t divisor = scene_call_second(g, page);
    set_scene_call_fraction(g, page, divisor ? (uint16_t)((dividend & WHOLE_PART) / divisor) : 0);
    set_scene_call_divisor_less_one(g, page, (uint16_t)(divisor - 1));
}

/* The 8.8 fraction of `dividend`'s high byte over `divisor`. The original also
 * leaves the dividend in the first operand's cell, shifted, which every caller
 * here writes over before anything reads it, so this doesn't. */
uint16_t scene_divide_8_8_by(GameState *g, SceneProgramPage page, uint16_t dividend, uint16_t divisor)
{
    set_scene_call_second(g, page, divisor);
    TIMED_AS(g, 0x7E32E2, divide_8_8(g, page, dividend));
    return scene_call_fraction(g, page);
}

/* The original keeps the dividend in the first operand's cell and shifts its
 * high byte out as it goes, bringing the caller's carry in first. */
static void leave_dividend_high_byte_shifted_out(GameState *g, SceneProgramPage page, uint16_t dividend, bool carry)
{
    set_scene_call_first(g, page, (uint16_t)(dividend << 8 | (carry ? 0x80 : 0)));
}

/* ---- find_divisor --------------------------------------------------------------------------- */

static uint16_t quotient_of(uint16_t dividend, uint8_t divisor) { return scene_divider_quotient(dividend, divisor); }

/* The 8-bit divisors wrap after 256: a search that goes round finds nothing. */
enum { DIVISORS = 0x100 };

typedef struct {
    bool found;
    uint8_t divisor;    /* the one found, or where the search started */
    uint16_t quotient;  /* its quotient */
    unsigned tried;     /* how many divisors it tried */
} FoundDivisor;

// $C3:0E29
/* The first divisor, from `first`, whose quotient of `dividend` falls under
 * `limit`. The caller has divided by `first` already, and its quotient is
 * `first_quotient`. */
static FoundDivisor find_divisor(uint16_t dividend, uint8_t first, uint16_t first_quotient, uint16_t limit)
{
    uint16_t quotient = first_quotient;
    for (unsigned n = 0; n < DIVISORS; n++) {
        uint8_t divisor = (uint8_t)(first + n);
        if (n)
            quotient = quotient_of(dividend, divisor);
        if (quotient < limit)
            return (FoundDivisor){ true, divisor, quotient, n + 1 };
    }
    return (FoundDivisor){ false, first, first_quotient, 1 };
}

/* The original tries a divisor a pass, through the CPU's divider: each pass
 * starts the next division and reads the last one's quotient. Its time is
 * charged a pass at a time, and the divider left dividing by the divisor after
 * the one found, as the original leaves it. */
enum { FIND_DIVISOR = 0xC30E29, ACCESSES_PER_PASS = 3 };

static void leave_divider_after(GameState *g, const FoundDivisor *f)
{
    charge_as(g, FIND_DIVISOR, f->tried * ACCESSES_PER_PASS);
    hw_divide_by(g, (uint8_t)(f->divisor + 1));
}

/* The first divisor from `from`, which the caller has started dividing
 * `dividend` by, whose quotient falls under `limit`; the caller makes sure there
 * is one (a search with none loops forever in the original). */
uint8_t scene_find_divisor_from(GameState *g, uint16_t dividend, uint8_t from, uint16_t limit)
{
    FoundDivisor f = find_divisor(dividend, from, quotient_of(dividend, from), limit);
    ORIGINAL_EFFECT(leave_divider_after(g, &f));
    return f.divisor;
}

/* ---- Guards: the helpers run from the program in RAM ----------------------------------------- */

bool scene_multiply_present(Snes *s)
{
    if (!scene_on_program_page(s) || !m_code_matches(s, 0x7E31BF, 74, 0x479627EBF1311CAFull))
        return false;
    vars_peek(true);
    bool exact = multiplies_exactly(state_of(s), scene_program_page_at_entry(s));
    vars_peek(false);
    return exact;
}

/* The signed multiply's magnitudes go through the multiply, whose guard's
 * test of its middle partial sums they must pass too. */
bool scene_multiply_signed16_present(Snes *s)
{
    if (!scene_on_program_page(s) || !m_code_matches(s, 0x7E33D6, 0x37, 0xCB3F0FAD1C9F8EA9ull) ||
        !m_code_matches(s, 0x7E31BF, 74, 0x479627EBF1311CAFull))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    SceneProgramPage page = scene_program_page_at_entry(s);
    bool exact = scene_multiplies_exactly(magnitude16(scene_call_first(g, page)), magnitude16(scene_call_second(g, page)),
                                          carries_into_the_product(g, page));
    vars_peek(false);
    return exact;
}

/* The sine reads its table through a long address, so any page and data bank
 * will do. */
bool scene_sine_present(Snes *s) { return m_code_matches(s, 0x7E3461, 0x19, 0xA2486F18B991C163ull); }

bool scene_divide_8_8_present(Snes *s)
{
    return scene_on_program_page(s) && m_code_matches(s, 0x7E32E2, 189, 0xD04B8C1274AF93C8ull);
}

/* The 32-bit divide takes the divisors the scenes divide by, never 0 and under
 * $80000000; past those the original's long division comes out otherwise. */
bool scene_divide_present(Snes *s)
{
    if (!scene_on_program_page(s) || !m_code_matches(s, 0x7E339F, 55, 0x03E628A5184CD799ull))
        return false;
    vars_peek(true);
    uint32_t divisor = scene_call_divisor(state_of(s), scene_program_page_at_entry(s));
    vars_peek(false);
    return divisor != 0 && !(divisor & TOP_BIT_32);
}

/* The original reaches the divider through the direct page. */
bool scene_find_divisor_on_divider_page(Snes *s)
{
    return m_direct_page(s) == M_DP_CPU && m_bank_reaches_low_ram(s);
}

/* ---- Entries --------------------------------------------------------------------------------- */

/* Entered with 16-bit A and X; P, the data bank, X and Y come back as they
 * were, and nothing else it leaves is read. */
void scene_multiply(Snes *s)
{
    GameState *g = state_of(s);
    SceneProgramPage page = scene_program_page_at_entry(s);
    multiply(g, page);
    m_rts(s);
}

/* Entered with 16-bit index registers; P comes back as it was. X is read after
 * it: the original leaves the first partial product there, the magnitudes' low
 * bytes multiplied. */
void scene_multiply_signed(Snes *s)
{
    GameState *g = state_of(s);
    SceneProgramPage page = scene_program_page_at_entry(s);
    uint16_t a = magnitude16(scene_call_first(g, page));
    uint8_t b = magnitude8((uint8_t)scene_call_second(g, page));
    multiply_signed(g, page);
    s->cpu.x = (uint16_t)((a & 0xFF) * b);
    m_rtl(s);
}

/* Entered with 16-bit index registers; P comes back as it was (the original
 * saves it round its work), and nothing else it leaves is read. */
void scene_multiply_signed16_entry(Snes *s)
{
    GameState *g = state_of(s);
    SceneProgramPage page = scene_program_page_at_entry(s);
    scene_multiply_signed16(g, page, scene_call_first(g, page), scene_call_second(g, page));
    m_rts(s);
}

/* The angle comes in A, and the value goes back there. The callers read the
 * flags it leaves: whether the value is negative or 0, and the carry, set for
 * an angle in the turn's second half, where the value is the table's as it
 * is. X is left the angle's step in the turn. */
static void leave_the_sine(Snes *s, uint16_t angle)
{
    int16_t value = scene_sine(state_of(s), angle);
    s->cpu.a = (uint16_t)value;
    s->cpu.x = angle & (SCENE_FULL_TURN - 1);
    m_set_flag(s, CPU_FLAG_N, value < 0);
    m_set_flag(s, CPU_FLAG_Z, value == 0);
    m_set_flag(s, CPU_FLAG_C, scene_sine_second_half(angle));
}

void scene_sine_entry(Snes *s)
{
    leave_the_sine(s, s->cpu.a);
    m_rts(s);
}

bool scene_quarter_turn_on_overflows(uint16_t angle) { return (int16_t)angle > INT16_MAX - SCENE_QUARTER_TURN; }

/* The cosine is the sine a quarter turn on; the original adds the quarter
 * turn, which leaves the overflow of that signed add. */
void scene_cosine_entry(Snes *s)
{
    uint16_t angle = s->cpu.a;
    m_set_flag(s, CPU_FLAG_V, scene_quarter_turn_on_overflows(angle));
    leave_the_sine(s, (uint16_t)(angle + SCENE_QUARTER_TURN));
    m_rts(s);
}

/* Entered with 16-bit A and X and the carry, which the dividend's cell ends
 * with; only Y is read after it, and it doesn't touch Y. */
void scene_divide(Snes *s)
{
    GameState *g = state_of(s);
    SceneProgramPage page = scene_program_page_at_entry(s);
    (void)divide(g, page);
    ORIGINAL_EFFECT(leave_dividend_shifted_out(g, page, m_flag(s, CPU_FLAG_C)));
    m_rts(s);
}

/* Entered with 16-bit A, the dividend, and the carry. Back, the carry is read:
 * the last of the fraction's rotates leaves it, and it is always clear (sixteen
 * rotates of a cell cleared first shift out one of its zeros). */
void scene_divide_8_8(Snes *s)
{
    GameState *g = state_of(s);
    SceneProgramPage page = scene_program_page_at_entry(s);
    uint16_t dividend = s->cpu.a;
    divide_8_8(g, page, dividend);
    ORIGINAL_EFFECT(leave_dividend_high_byte_shifted_out(g, page, dividend, m_flag(s, CPU_FLAG_C)));
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* Entered with 16-bit A and 8-bit index registers, X the divisor the caller
 * started a division by, of the dividend it left in the divider, whose
 * quotient the divider holds; the limit is
 * on the scene's own page, through the data bank. Back, X is the divisor after
 * the one found and A its quotient, with the carry clear (the loop's compare).
 * A search that finds nothing loops forever in the original; the port stops
 * after the first pass, where the search is after all 256. */
void scene_find_divisor(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t caller_tried = (uint8_t)s->cpu.x;
    uint16_t limit = scene_call_quotient_limit(g, scene_program_own());
    FoundDivisor f = find_divisor(hw_dividend(g), caller_tried, hw_quotient(g), limit);
    ORIGINAL_EFFECT(leave_divider_after(g, &f));
    s->cpu.x = (uint8_t)(f.divisor + 1);
    s->cpu.a = f.quotient;
    m_set_flag(s, CPU_FLAG_C, !f.found);
    if (!f.found) {
        m_stop_at(s, 0xC30E29, DC_N | DC_Z);
        return;
    }
    m_rtl(s);
}
