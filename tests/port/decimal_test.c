/* Checks a number's decimal digits (core/decimal.h) against division: the
 * last digits of numbers across 32 bits, a number with more digits than are
 * asked for, and the most digits asked of the largest. Exits non-zero on
 * failure.
 *
 *   decimal_test */
#include <stdio.h>

#include "core/decimal.h"

static int failures;

static void check(uint32_t value, unsigned count)
{
    uint8_t digits[DECIMAL_MOST_DIGITS];
    decimal_digits(value, digits, count);
    uint32_t left = value;
    for (unsigned place = 0; place < count; place++) {
        if (digits[place] != left % 10) {
            printf("FAIL: %lu's digit %u of %u is %u, not %lu\n", (unsigned long)value, place, count, digits[place],
                   (unsigned long)(left % 10));
            failures++;
            return;
        }
        left /= 10;
    }
}

int main(void)
{
    static const uint32_t values[] = { 0, 1, 9, 10, 99, 100, 65535, 99999, 100000, 9999999, 10000000, 12345678,
                                       99999999, 100000000, 123456789, 999999999, 1000000000, 4294967295u };
    for (unsigned i = 0; i < sizeof values / sizeof values[0]; i++)
        for (unsigned count = 1; count <= DECIMAL_MOST_DIGITS; count++)
            check(values[i], count);
    for (uint32_t value = 0, step = 1; value < 4000000000u; value += step, step = step * 3 + 1)
        for (unsigned count = 1; count <= DECIMAL_MOST_DIGITS; count++)
            check(value, count);
    if (failures)
        return 1;
    printf("decimal_test: ok\n");
    return 0;
}
