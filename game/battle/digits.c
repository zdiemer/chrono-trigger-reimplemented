/* Battle: numbers as the windows show them, in digit tiles.
 *
 * HP, MP and tech costs show as three or two digits, a tile each. The
 * original's callers hand the number over in battle_number and read the
 * tiles back from battle_digits, where these leave them; the window's and the
 * menu's ports take the tiles as values. A number's zeros in front are
 * blanked where the window shows it (a cost shows its zero tens instead, and
 * moves its ones left, menu_window.c). */
#include "battle/window.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/window_data.h"
#include "original_effects.h"
#include "vars.h"


static void keep_digits(GameState *g, Digits d)
{
    set_battle_digits(g, BATTLE_DIGIT_HUNDREDS, d.hundreds);
    set_battle_digits(g, BATTLE_DIGIT_TENS, d.tens);
    set_battle_digits(g, BATTLE_DIGIT_ONES, d.ones);
}

/* The original counts the digits out of the number by subtracting until it
 * goes below zero, and leaves it there: its ones less ten. */
static void leave_number(GameState *g, uint16_t n) { set_battle_number(g, (uint16_t)(n % 10 - 10)); }

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
    ORIGINAL_EFFECT(leave_number(g, n));
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

/* ---- The entries ------------------------------------------------------------------------ */

static Digits kept_digits(const GameState *g)
{
    return (Digits){ battle_digits(g, BATTLE_DIGIT_HUNDREDS), battle_digits(g, BATTLE_DIGIT_TENS), battle_digits(g, BATTLE_DIGIT_ONES) };
}

/* The number in battle_number, with 16-bit A; they come back with 8-bit A,
 * its high byte clear. */
static void digits_entry(Snes *s, unsigned places)
{
    battle_digits_of(state_of(s), battle_number(state_of(s)), places);
    m_sep(s, CPU_FLAG_M);
    s->cpu.a = DIGIT_BLANK;
    m_rts(s);
}

void battle_number_to_digits(Snes *s) { digits_entry(s, 3); }
void battle_number_to_2_digits(Snes *s) { digits_entry(s, 2); }

/* The carry is from the last digit it compared: the tens against the zero,
 * or, where they are a zero, the hundreds against the blank. */
void battle_blank_leading_zeros(Snes *s)
{
    GameState *g = state_of(s);
    Digits before = kept_digits(g);
    Digits d = battle_digits_blanked(g, before);
    m_set_flag(s, CPU_FLAG_C, digit_is_zero(before.tens) ? d.hundreds == DIGIT_BLANK : before.tens >= TILE_DIGIT_ZERO);
    m_rts(s);
}
