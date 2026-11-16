/* A number's decimal digits (decimal.h). */
#include "core/decimal.h"

static const uint32_t powers_of_ten[DECIMAL_MOST_DIGITS + 1] = {
    1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000,
};

/* How many times `power` goes into *left, taken off it. */
static uint32_t times_taken(uint32_t *left, uint32_t power)
{
    uint32_t times = 0;
    while (*left >= power) {
        *left -= power;
        times++;
    }
    return times;
}

void decimal_digits(uint32_t value, uint8_t *digits, unsigned count)
{
    uint32_t left = value;
    times_taken(&left, powers_of_ten[count]);
    for (unsigned place = count; place-- > 0;)
        digits[place] = (uint8_t)times_taken(&left, powers_of_ten[place]);
}
