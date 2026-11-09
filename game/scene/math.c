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
 * -- and the C operations assert those input domains. The marks it leaves
 * on inputs the game does give are named inputs: the unsigned multiply carries
 * in the top bit of the byte after its product, and the signed one adds in a
 * byte of the product cell, both as the last routine left them. The
 * arithmetic cells that later random draws consume remain game state. */
#include "scene/scene.h"

#include "charge.h"
#include "core/program.h"
#include "cpu_io.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "scene/scene_data.h"
#include "vars.h"

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
    if (!scene_multiplies_exactly(a, b, carried)) {
        program_breaks_a_contract(g, 0x7E31BF, "a scene product outside its arithmetic domain");
        return 0;
    }
    shift_the_byte_after(g, page);
    uint32_t product = (uint32_t)a * b + (carried ? CARRIED_IN : 0);
    set_scene_call_product(g, page, product);
    charge_legacy_io_accesses(g, 4);
    return product;
}

/* The original sums the middle partial products in 16 bits, and one that
 * overflows goes into the product a byte too low; no scene's product does, and
 * the product's contract excludes it. */
bool scene_multiplies_exactly(uint16_t a, uint16_t b, bool carried)
{
    uint32_t middle = (((uint32_t)(a & 0xFF) * (b >> 8) + (uint32_t)(a >> 8) * (b & 0xFF)) & 0xFFFF) +
                      ((uint32_t)(a & 0xFF) * (b & 0xFF) >> 8) + (carried ? 1 : 0);
    return middle <= 0xFFFF;
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
    charge_legacy_io_accesses(g, 4);
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

/* ---- The rotating map's figures --------------------------------------------------------------- */

// $7E:5872
/* Quantize a figure's offset to a quarter-turn table, then reflect it into
 * its quadrant. Offsets within one unit of the vertical axis use the pole
 * directly. The slope uses byte magnitudes, as the scene's screen positions do. */
uint16_t scene_offset_angle(GameState *g, int16_t across, int16_t down)
{
    uint16_t x = magnitude16((uint16_t)across), y = magnitude16((uint16_t)down);
    uint16_t dividend = (uint16_t)(y << 8);
    charge_legacy_accesses(g, 2);
    charge_legacy_accesses(g, 2);
    charge_legacy_io_accesses(g, 2);
    if (x <= 1)
        return down < 0 ? SCENE_QUARTER_TURN : 3 * SCENE_QUARTER_TURN;
    uint8_t divisor = (uint8_t)(x - 1);
    uint16_t slope = scene_divider_quotient(dividend, divisor);
    charge_legacy_io_accesses(g, 1);
    unsigned step = 1;
    while (step < 64 && slope >= scene_angle_thresholds(g, step))
        step++;
    uint16_t quarter = (uint16_t)(4 * (step - 1));
    if ((across < 0) != (down < 0))
        return (uint16_t)((down < 0 ? SCENE_HALF_TURN : SCENE_FULL_TURN) - quarter);
    return down < 0 ? quarter : (uint16_t)(SCENE_HALF_TURN + quarter);
}

// $7E:34FB
/* The radius used to orbit a figure: the largest r whose r*(r-1) is less
 * than its squared distance. This is the scene's integer approximation to
 * the square root; zero distance gives zero. Area is at most 65536. */
uint16_t scene_radius_from_area(GameState *g, uint32_t area)
{
    if (area > 65536) {
        program_breaks_a_contract(g, 0x7E34FB, "a scene orbit outside its radius domain");
        return 0;
    }
    unsigned low = 0, high = 257;
    while (low + 1 < high) {
        unsigned middle = (low + high) / 2;
        if (middle * (middle - 1) < area)
            low = middle;
        else
            high = middle;
    }
    charge_legacy_accesses(g, 2);
    return (uint16_t)low;
}

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

uint32_t scene_divide32(GameState *g, SceneProgramPage page, uint32_t dividend, uint32_t divisor, bool carry)
{
    (void)carry;
    if (!divisor || divisor >= 0x80000000u) {
        program_breaks_a_contract(g, 0x7E339F, "a scene divisor outside its arithmetic domain");
        return 0;
    }
    set_scene_call_dividend(g, page, dividend);
    set_scene_call_divisor(g, page, divisor);
    uint32_t quotient;
    TIMED_AS(g, 0x7E339F, quotient = divide(g, page));
    charge_legacy_accesses(g, 4);
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

/* The divide works through the dividend in the first operand's cell,
 * shifting its high byte out, with what the caller's carry brought in coming
 * after it: the cell ends holding the dividend's low byte over that. The
 * scenes' random draws stir the cell in after it (scene/random.c). The ports'
 * own divides bring no carry in. */
enum { CARRY_BEHIND_THE_DIVIDEND = 0x80 };

static void leave_the_dividend(GameState *g, SceneProgramPage page, uint16_t dividend, bool carried_in)
{
    set_scene_call_first(g, page, (uint16_t)(dividend << 8 | (carried_in ? CARRY_BEHIND_THE_DIVIDEND : 0)));
}

/* The 8.8 fraction of `dividend`'s high byte over `divisor`. */
uint16_t scene_divide_8_8_by(GameState *g, SceneProgramPage page, uint16_t dividend, uint16_t divisor)
{
    set_scene_call_second(g, page, divisor);
    TIMED_AS(g, 0x7E32E2, divide_8_8(g, page, dividend));
    leave_the_dividend(g, page, dividend, false);
    return scene_call_fraction(g, page);
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

/* The first divisor from `from`, which the caller has started dividing
 * `dividend` by, whose quotient falls under `limit`; the caller makes sure there
 * is one (a search with none loops forever in the original). */
uint8_t scene_find_divisor_from(GameState *g, uint16_t dividend, uint8_t from, uint16_t limit)
{
    FoundDivisor f = find_divisor(dividend, from, quotient_of(dividend, from), limit);
    if (!f.found) {
        program_breaks_a_contract(g, FIND_DIVISOR, "a scene divisor search without a solution");
        return from;
    }
    charge_as(g, FIND_DIVISOR, f.tried * ACCESSES_PER_PASS);
    charge_legacy_io_accesses(g, 1);
    return f.divisor;
}
