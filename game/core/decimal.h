/* A number's decimal digits, for the menus' and the scenes' numbers on the
 * screen. Each digit is counted out by taking its place's power of ten off
 * the number for as long as it goes, rather than divided out: a number drawn
 * is a handful of digits, and a small target's processor (the GBA's) has no
 * divide, so each / and % would be a call. */
#ifndef CT_GAME_CORE_DECIMAL_H
#define CT_GAME_CORE_DECIMAL_H

#include <stdint.h>

enum { DECIMAL_MOST_DIGITS = 9 };

/* The last `count` (at most DECIMAL_MOST_DIGITS) decimal digits of `value`,
 * the units first, into digits[0] to digits[count - 1]: those of the value
 * less its whole multiples of ten to the `count`. */
void decimal_digits(uint32_t value, uint8_t *digits, unsigned count);

#endif
