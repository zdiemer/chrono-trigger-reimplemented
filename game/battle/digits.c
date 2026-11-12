/* Battle: numbers as the windows show them, in digit tiles.
 *
 * HP, MP and tech costs show as three or two digits, a tile each. The
 * original's callers hand the number over in battle_number and read the
 * tiles back from battle_digits, where these leave them; the window's and the
 * menu's ports take the tiles as values. A number's zeros in front are
 * blanked where the window shows it (a cost shows its zero tens instead, and
 * moves its ones left, menu_window.c). */
#include "charge.h"
#include "battle/window.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/window_data.h"
#include "vars.h"

static void keep_digits(GameState *g, Digits d)
{
    set_battle_digits(g, BATTLE_DIGIT_HUNDREDS, d.hundreds);
    set_battle_digits(g, BATTLE_DIGIT_TENS, d.tens);
    set_battle_digits(g, BATTLE_DIGIT_ONES, d.ones);
}

// $C1:011F, $C1:0174
/* A number of three digits, or of two, the hundreds left blank (a digit that
 * outgrows its place shows whatever tile follows the nine's). */
Digits battle_digits_of(GameState *g, uint16_t n, unsigned places)
{
    Digits d = { DIGIT_BLANK, 0, battle_digit_tile(g, n % 10) };
    if (places == 3) {
        d.hundreds = battle_digit_tile(g, n / 100);
        d.tens = battle_digit_tile(g, n % 100 / 10);
    } else {
        d.tens = battle_digit_tile(g, n / 10);
    }
    set_battle_digits(g, BATTLE_DIGIT_THOUSANDS, DIGIT_BLANK);
    keep_digits(g, d);
    return d;
}

// $C1:104E
Digits battle_digits_blanked(GameState *g, Digits d)
{
    if (digit_is_zero(d.hundreds))
        d.hundreds = DIGIT_BLANK;
    if (digit_is_zero(d.tens) && d.hundreds == DIGIT_BLANK)
        d.tens = DIGIT_BLANK;
    keep_digits(g, d);
    return d;
}
