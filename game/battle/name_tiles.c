/* Battle: a name as the two rows of tiles the windows draw it in
 * (battle/choosing.h).
 *
 * The windows show text two tiles high. Most characters are one tile, in the
 * lower row, with a blank above; the font's two-part characters are drawn as
 * a base tile with one of two marks above it. The battle makes the rows for
 * the names it shows once, as it starts (setup.c), and for the techs and
 * items each time their lists are laid out (menu_lists.c).
 *
 * Each conversion returns the two rows directly from the supplied text. */
#include "battle/choosing.h"

#include <string.h>

#include "battle/bridge.h"
#include "battle/entries.h"
#include "vars.h"

/* The two-part characters: those from the first run take the first mark, and
 * move up the font by a quarter of it; those of the second run take the
 * second mark. */
enum { FIRST_RUN = 0x40, SECOND_RUN = 0x69, PAST_THE_RUNS = 0x73, FIRST_RUN_SHIFT = 0x40, SECOND_RUN_SHIFT = 0x17, FIRST_MARK = 0x71, SECOND_MARK = 0x72,  };

// $C1:01A9
NameTiles battle_name_tiles(const uint8_t text[NAME_TILES])
{
    NameTiles tiles;
    memset(&tiles, TILE_BLANK, sizeof tiles);
    for (unsigned i = 0; i < NAME_TILES && text[i] != NAME_END; i++) {
        uint8_t c = text[i];
        if (c >= FIRST_RUN && c < SECOND_RUN) {
            tiles.glyphs[i] = (uint8_t)(c + FIRST_RUN_SHIFT);
            tiles.marks[i] = FIRST_MARK;
        } else if (c >= SECOND_RUN && c < PAST_THE_RUNS) {
            tiles.glyphs[i] = (uint8_t)(c + SECOND_RUN_SHIFT);
            tiles.marks[i] = SECOND_MARK;
        } else {
            tiles.glyphs[i] = c;
        }
    }
    return tiles;
}
