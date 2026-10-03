/* The digits two of the menus' screens draw their numbers with, built in
 * work RAM from four-colour digits in the ROM before the screen sends them
 * to VRAM.
 *
 * - The number tiles ($FF:F548): small numbers drawn two digits to a tile,
 *   one tile for each of 0-99 and one for each digit alone, and for numbers
 *   past 99 a tile that starts them -- the hundreds digit and the first
 *   columns of the tens -- before the number's own tile, which then shows the
 *   tens digit's last columns. The digits are five pixels wide, so two only
 *   share a tile by the tens digit losing its first two columns.
 * - The items screen's amounts ($FF:F7D9): the ten digits, with a third
 *   bitplane that is the ink of the other two, which puts them in colours 5-7
 *   of their palette.
 *
 * The tiles are sixteen-colour ones (32 bytes: two pairs of bitplanes, a row
 * a byte of each plane); the four-colour digits fill the first pair, and what
 * the tiles' last pair held before is left as it was. */
#include "menu/digits.h"

#include "menu/bridge.h"
#include "menu/menu_data.h"
#include "original_effects.h"
#include "port/machine.h"
#include "ram.h"
#include "vars.h"

/* A four-colour digit is sixteen bytes, its rows' two bitplanes byte by
 * byte; a sixteen-colour tile has those first, then the other two planes'. */
enum { ROWS = 8, PLANES_BYTES = MENU_DIGIT_BYTES };

/* ---- The number tiles ------------------------------------------------------------------------- */

/* The tens digit goes five pixels left of where the font has the digit,
 * which loses its first two columns; a tile starting a number past 99 has
 * its hundreds digit two pixels left, and after it those two columns, three
 * pixels right. */
enum { TENS_SHIFT = 5, HUNDREDS_SHIFT = 2, TENS_START_SHIFT = 3, TENS_START_COLUMNS = 0x03 };

/* The numbers past 99 it starts: none (a blank), 1xx and 2xx. */
enum { HUNDREDS = 3, NUMBERS = 100 };

static void put_digit_planes(GameState *g, unsigned tile, unsigned digit)
{
    for (unsigned at = 0; at < PLANES_BYTES; at++)
        set_menu_number_tiles_byte(g, tile, at, menu_small_digit_byte(g, digit, at));
}

/* Every block of ten tiles starts as the first: the digits alone, over
 * what the first block's last two planes held. The last block stays so. */
static void lay_out_digits(GameState *g)
{
    for (unsigned digit = 0; digit < MENU_DIGITS; digit++)
        put_digit_planes(g, digit, digit);
    for (unsigned tile = MENU_DIGITS; tile < MENU_NUMBER_TILES_COUNT; tile++)
        for (unsigned at = 0; at < MENU_NUMBER_TILES_BYTES; at++)
            set_menu_number_tiles_byte(g, tile, at, menu_number_tiles_byte(g, tile % MENU_DIGITS, at));
}

static void put_tens(GameState *g, unsigned number)
{
    unsigned tens = number / MENU_DIGITS, units = number % MENU_DIGITS;
    for (unsigned at = 0; at < PLANES_BYTES; at++)
        set_menu_number_tiles_byte(g, number, at,
                                   (uint8_t)(menu_small_digit_byte(g, units, at) | menu_small_digit_byte(g, tens, at) << TENS_SHIFT));
}

/* The hundreds digit: none, or a digit of the font. */
static uint8_t hundreds_byte(const GameState *g, unsigned hundreds, unsigned at)
{
    return hundreds == 0 ? 0 : menu_small_digit_byte(g, hundreds, at);
}

static void put_number_start(GameState *g, unsigned hundreds, unsigned tens)
{
    unsigned tile = hundreds * MENU_DIGITS + tens;
    for (unsigned at = 0; at < PLANES_BYTES; at++) {
        uint8_t tens_start = (uint8_t)(menu_small_digit_byte(g, tens, at) >> TENS_START_SHIFT & TENS_START_COLUMNS);
        set_menu_hundreds_tiles_byte(g, tile, at, (uint8_t)(hundreds_byte(g, hundreds, at) << HUNDREDS_SHIFT | tens_start));
    }
}

/* The first start tile, of no hundreds and a tens digit of 0, is blank. */
static void blank_first_start(GameState *g)
{
    for (unsigned at = 0; at < PLANES_BYTES; at++)
        set_menu_hundreds_tiles_byte(g, 0, at, 0);
}

/* What the original's drawing leaves in its cells: the hundreds digits it
 * reads from, the digit it got to past the last start tile's, the counts run
 * out, and the last font byte as far as it shifted it. */
enum { PIXELS_SHIFTED = 5 };

static void leave_number_tiles_built(GameState *g)
{
    for (unsigned hundreds = 0; hundreds < HUNDREDS; hundreds++)
        for (unsigned at = 0; at < MENU_DIGIT_BYTES; at++)
            set_menu_hundreds_digits_byte(g, hundreds * MENU_DIGIT_BYTES + at, hundreds_byte(g, hundreds, at));
    set_menu_scratch_number_tiles_digit(g, HUNDREDS * MENU_DIGIT_BYTES);
    set_menu_scratch_number_tiles_bytes_left(g, 0);
    set_menu_scratch_number_tiles_pixels(g, (uint8_t)(menu_small_digit_byte(g, MENU_DIGITS - 1, PLANES_BYTES - 1) << PIXELS_SHIFTED));
    set_menu_scratch_number_tiles_hundreds_byte(g, HUNDREDS * MENU_DIGIT_BYTES);
    set_menu_scratch_number_tiles_blocks_left(g, 0);
}

// $FF:F548
void menu_build_number_tiles(GameState *g)
{
    lay_out_digits(g);
    for (unsigned number = 0; number < NUMBERS; number++)
        put_tens(g, number);
    for (unsigned hundreds = 0; hundreds < HUNDREDS; hundreds++)
        for (unsigned tens = 0; tens < MENU_DIGITS; tens++)
            put_number_start(g, hundreds, tens);
    blank_first_start(g);
    ORIGINAL_EFFECT(leave_number_tiles_built(g));
}

/* ---- The amounts' digits ---------------------------------------------------------------------- */

/* A tile's third bitplane is right after the first two. */
enum { THIRD_PLANE = PLANES_BYTES, PLANE_PAIR = 2 };

static void build_amount_digit(GameState *g, unsigned digit)
{
    for (unsigned at = 0; at < PLANES_BYTES; at++)
        set_menu_amount_digit_tiles_byte(g, digit, at, menu_amount_digit_byte(g, digit, at));
    for (unsigned row = 0; row < ROWS; row++) {
        uint8_t ink = (uint8_t)(menu_amount_digit_byte(g, digit, PLANE_PAIR * row) | menu_amount_digit_byte(g, digit, PLANE_PAIR * row + 1));
        set_menu_amount_digit_tiles_byte(g, digit, THIRD_PLANE + PLANE_PAIR * row, ink);
    }
}

/* What the original leaves: where the tile after the last would be, and the
 * row count run out. */
static void leave_amount_digits_built(GameState *g)
{
    set_menu_scratch_amount_digit_at(g, (uint16_t)(RAM_ABS(RAM_MENU_AMOUNT_DIGIT_TILES) + MENU_DIGITS * MENU_AMOUNT_DIGIT_TILES_BYTES));
    set_menu_scratch_amount_digit_rows_left(g, 0);
}

// $FF:F7D9
void menu_build_amount_digits(GameState *g)
{
    for (unsigned digit = 0; digit < MENU_DIGITS; digit++)
        build_amount_digit(g, digit);
    ORIGINAL_EFFECT(leave_amount_digits_built(g));
}

/* ---- The registered entries ------------------------------------------------------------------- */

/* Both move the digits in with a block move, which leaves the data bank on
 * work RAM's; the guards ask for it there already. */
void menu_build_number_tiles_entry(Snes *s)
{
    menu_build_number_tiles(state_of(s));
    m_rtl(s);
}

void menu_build_amount_digits_entry(Snes *s)
{
    menu_build_amount_digits(state_of(s));
    m_rtl(s);
}

/* The number tiles start from the menus' zero word, which is 0 whenever the
 * menus run. */
bool menu_digits_are_buildable(Snes *s) { return menu_on_page_work_ram(s); }

bool menu_number_tiles_are_buildable(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_zero(state_of(s)) == 0;
    vars_peek(false);
    return ok;
}
