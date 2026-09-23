/* Scenes: arithmetic helpers of bank C3, on the math scratch cells of the
 * caller's page (scene/scratch.c) and the CPU's multiplier and divider
 * (game/cpu_io.h). */
#include "scene/scene.h"

#include "cpu_io.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

static bool negative16(uint16_t v) { return v & 0x8000; }

// $C3:094C
/* The signed 16-bit multiplicand times the signed 8-bit multiplier (the second
 * operand's low byte), into the 32-bit product: the magnitudes
 * through the multiplier a byte of the multiplicand at a time, and the product
 * negated when the signs differ. The middle word adds the second partial
 * product to the product's bytes 1-2 as they stand, and byte 2 is still what the
 * caller left there, so it is added in too, as the original does. Returns the
 * first partial product, which the original leaves in X. */
static uint16_t multiply_signed(GameState *g, SceneMathPage page)
{
    uint16_t a = scene_scratch16(g, page, SCRATCH_A);
    uint8_t b = scene_scratch8(g, page, SCRATCH_B);
    uint8_t leftover = scene_scratch8(g, page, SCRATCH_PRODUCT + 2);
    bool negative = ((a >> 8) ^ b) & 0x80;
    uint16_t a_size = negative16(a) ? (uint16_t)-a : a;
    hw_set_multiplicand(g, (b & 0x80) ? (uint8_t)-b : b);
    uint16_t low = hw_multiply_by(g, (uint8_t)a_size);
    uint16_t high = hw_multiply_by(g, (uint8_t)(a_size >> 8));
    uint16_t middle = (uint16_t)(((low >> 8) | leftover << 8) + high);
    uint32_t product = (uint32_t)(low & 0xFF) | (uint32_t)middle << 8;
    set_scene_scratch32(g, page, SCRATCH_PRODUCT, negative ? (uint32_t)-product : product);
    return low;
}

/* Entered with 16-bit index registers; P comes back as it was. */
void scene_multiply_signed(Snes *s)
{
    SceneMathPage page = scene_math_scene();
    scene_math_page_of(m_direct_page(s), &page);   /* the guard has checked it is one */
    s->cpu.x = multiply_signed(state_of(s), page);
    m_rtl(s);
}

/* The divisors $C3:0E29 tries run through the 8-bit X: after all 256 the loop
 * is back where it was after the first, and it never ends. */
enum { DIVISORS = 0x100 };

/* One pass of the loop: the quotient of the division started last, and the
 * next divisor started. */
static uint16_t next_divisor(GameState *g, uint8_t divisor)
{
    uint16_t quotient = hw_quotient(g);
    hw_divide_by(g, divisor);
    return quotient;
}

typedef struct {
    bool found;
    uint8_t divisor;
    uint16_t quotient;
} Divisor;

// $C3:0E29
/* Count the divisor up from `divisor` until the quotient of the dividend falls
 * below the limit (the scene's own product cell, reached through the data
 * bank): each pass reads the quotient of the
 * division started before (the caller starts the first) and starts the next
 * one. Not found, the original loops forever; the port stops after the first
 * pass, which is where it would be after 256. */
static Divisor find_divisor(GameState *g, uint8_t divisor)
{
    uint16_t limit = scene_scratch16(g, scene_math_scene(), SCRATCH_PRODUCT);
    Divisor first = { false, (uint8_t)(divisor + 1), 0 };
    first.quotient = next_divisor(g, first.divisor);
    Divisor d = first;
    for (uint16_t i = 0; i < DIVISORS && d.quotient >= limit; i++) {
        d.divisor = (uint8_t)(d.divisor + 1);
        d.quotient = next_divisor(g, d.divisor);
    }
    if (d.quotient < limit) {
        d.found = true;
        return d;
    }
    hw_divide_by(g, first.divisor);   /* as after the first pass */
    return first;
}

/* The original reaches the divider through the direct page. */
bool scene_find_divisor_on_divider_page(Snes *s)
{
    return m_direct_page(s) == M_DP_CPU && m_bank_reaches_low_ram(s);
}

/* Entered with 16-bit A and 8-bit index registers: X the divisor before the
 * first, and back X the divisor and A the quotient. The carry is clear where
 * it returns (the loop's BCS not taken). */
void scene_find_divisor(Snes *s)
{
    Divisor d = find_divisor(state_of(s), (uint8_t)s->cpu.x);
    s->cpu.x = d.divisor;
    s->cpu.a = d.quotient;
    m_set_flag(s, CPU_FLAG_C, !d.found);
    if (!d.found) {
        m_stop_at(s, 0xC30E29, DC_N | DC_Z);
        return;
    }
    m_rtl(s);
}
