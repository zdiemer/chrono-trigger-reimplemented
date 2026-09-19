/* Scenes: arithmetic helpers. */
#include "scene/scene.h"

#include "port/machine.h"
#include "ram.h"

/* The hardware multiplier: 8 x 8 bits, the product in RDMPY. The scene reaches
 * it through the data bank, as the original does. */
enum { WRMPYA = 0x4202, WRMPYB = 0x4203, RDMPY = 0x4216 };
/* And the divider: the dividend in WRDIV divided by WRDIVB, the quotient in
 * RDDIV. */
enum { WRDIVB = 0x4206, RDDIV = 0x4214 };

enum {
    MULTIPLICAND = RAM_SCENE_MATH_SCRATCH_A - RAM_SCENE_MATH_SCRATCH + RAM_SCENE_MATH_SCRATCH_DP,
    MULTIPLIER = RAM_SCENE_MATH_SCRATCH_B - RAM_SCENE_MATH_SCRATCH + RAM_SCENE_MATH_SCRATCH_DP,
    PRODUCT = RAM_SCENE_MATH_SCRATCH_C - RAM_SCENE_MATH_SCRATCH + RAM_SCENE_MATH_SCRATCH_DP,
};

static uint16_t cell16(Snes *s, uint8_t dp) { return m_read16(s, m_dp(s, dp)); }
static void set_cell16(Snes *s, uint8_t dp, uint16_t v) { m_write16(s, m_dp(s, dp), v); }

static uint16_t multiply(Snes *s, uint8_t b)
{
    m_write8(s, m_abs(s, WRMPYB), b);
    return m_read16(s, m_abs(s, RDMPY));
}

// $C3:094C
/* The signed 16-bit multiplicand at $F0 times the signed 8-bit multiplier at
 * $F2, into the 32-bit product at $F4-$F7: the magnitudes through the hardware
 * multiplier, a byte of the multiplicand at a time, and the product negated
 * when the signs differ. The middle word adds the second partial product to
 * what is at $F5-$F6, which includes $F6 as it was before, as the original
 * does. Returns the first partial product, which the original leaves in X. */
static uint16_t multiply_signed(Snes *s)
{
    uint16_t a = cell16(s, MULTIPLICAND);
    uint8_t b = m_read8(s, m_dp(s, MULTIPLIER));
    bool negative = (m_read8(s, m_dp(s, (uint8_t)(MULTIPLICAND + 1))) ^ b) & 0x80;
    uint8_t b_size = (b & 0x80) ? (uint8_t)-b : b;
    uint16_t a_size = (a & 0x8000) ? (uint16_t)-a : a;
    m_write8(s, m_abs(s, WRMPYA), b_size);
    uint16_t low = multiply(s, (uint8_t)a_size);
    m_write8(s, m_dp(s, (uint8_t)(PRODUCT + 3)), 0);
    uint16_t high = multiply(s, (uint8_t)(a_size >> 8));
    set_cell16(s, PRODUCT, low);
    set_cell16(s, (uint8_t)(PRODUCT + 1), (uint16_t)(high + cell16(s, (uint8_t)(PRODUCT + 1))));
    if (negative) {
        uint16_t product_low = cell16(s, PRODUCT);
        uint16_t negated_low = (uint16_t)(~product_low + 1);
        set_cell16(s, PRODUCT, negated_low);
        uint16_t borrow = negated_low == 0 ? 1 : 0;
        set_cell16(s, (uint8_t)(PRODUCT + 2), (uint16_t)(~cell16(s, (uint8_t)(PRODUCT + 2)) + borrow));
    }
    return low;
}

/* Entered with 16-bit index registers; saves P and restores it. */
void scene_multiply_signed(Snes *s)
{
    s->cpu.x = multiply_signed(s);
    m_rtl(s);
}

/* The divisors $C3:0E29 tries run through the 8-bit X: after all 256 the loop
 * is back where it was after the first, and it never ends. */
enum { DIVISORS = 0x100 };

/* One pass of the loop: the quotient of the division started last, and the
 * next divisor started. */
static uint16_t next_divisor(Snes *s, uint8_t divisor)
{
    uint16_t quotient = m_read16(s, RDDIV);
    m_write8(s, WRDIVB, divisor);
    return quotient;
}

// $C3:0E29
/* Count the divisor up from X until the quotient of the dividend falls below
 * the limit at $F4 (through the data bank): each pass reads the quotient of
 * the division started before (the caller starts the first) and starts the
 * next one. Returns true with X the divisor it stopped on and A the quotient;
 * false, after the first pass, when no divisor gets there and the original
 * loops forever. */
static bool find_divisor(Snes *s, uint8_t *x, uint16_t *quotient)
{
    uint16_t limit = m_read16(s, m_abs(s, RAM_ABS(RAM_SCENE_MATH_SCRATCH_C)));
    uint8_t first = (uint8_t)(*x + 1);
    uint16_t first_quotient = next_divisor(s, first);
    *x = first;
    *quotient = first_quotient;
    for (uint16_t i = 0; i < DIVISORS && *quotient >= limit; i++) {
        *x = (uint8_t)(*x + 1);
        *quotient = next_divisor(s, *x);
    }
    if (*quotient < limit)
        return true;
    /* as after the first pass */
    m_write8(s, WRDIVB, first);
    *x = first;
    *quotient = first_quotient;
    return false;
}

bool scene_find_divisor_on_divider_page(Snes *s) { return s->cpu.d == M_DP_CPU; }

/* Entered with 16-bit A and 8-bit index registers; the carry is clear where it
 * returns (the loop's BCS not taken). */
void scene_find_divisor(Snes *s)
{
    uint8_t x = (uint8_t)s->cpu.x;
    uint16_t quotient;
    bool found = find_divisor(s, &x, &quotient);
    s->cpu.x = x;
    s->cpu.a = quotient;
    if (!found) {
        m_set_flag(s, CPU_FLAG_C, true);
        m_stop_at(s, 0xC30E29, DC_N | DC_Z);
        return;
    }
    m_set_flag(s, CPU_FLAG_C, false);
    m_rtl(s);
}
