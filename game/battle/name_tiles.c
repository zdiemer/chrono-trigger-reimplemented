/* Battle: a name as the two rows of tiles the windows draw it in
 * (battle/choosing.h).
 *
 * The windows show text two tiles high. Most characters are one tile, in the
 * lower row, with a blank above; the font's two-part characters are drawn as
 * a base tile with one of two marks above it. The battle makes the rows for
 * the names it shows once, as it starts (setup.c), and for the techs and
 * items each time their lists are laid out (menu_lists.c).
 *
 * The original makes them in its name buffer (battle_name_buffer): the text
 * goes in, it copies the text out (battle_name_converted) and writes the two
 * rows over it. Here the rows are computed from the text; what the buffer is
 * left holding is scaffolding, except at the registered entry, whose
 * original callers read the rows there. */
#include "battle/choosing.h"

#include <string.h>

#include "battle/bridge.h"
#include "battle/entries.h"
#include "vars.h"

/* The two-part characters: those from the first run take the first mark, and
 * move up the font by a quarter of it; those of the second run take the
 * second mark. */
enum {
    FIRST_RUN = 0x40, SECOND_RUN = 0x69, PAST_THE_RUNS = 0x73,
    FIRST_RUN_SHIFT = 0x40, SECOND_RUN_SHIFT = 0x17,
    FIRST_MARK = 0x71, SECOND_MARK = 0x72,
};

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

/* The buffer's first row is the text as it is there, past what the caller
 * put in as well: the original copies all sixteen bytes out. */
void battle_leave_name_buffer(GameState *g, const uint8_t text[NAME_TILES], const NameTiles *tiles)
{
    for (unsigned i = 0; i < NAME_TILES; i++) {
        set_battle_name_converted(g, i, text[i]);
        set_battle_name_buffer(g, i, tiles->glyphs[i]);
        set_battle_name_buffer(g, NAME_TILES + i, tiles->marks[i]);
    }
}

/* ---- The entry ---------------------------------------------------------------------------------- */

/* The name in the buffer, its rows put back there for the original's
 * callers. It leaves X at the name's end and A, its high byte clear, holding
 * what it last loaded: the end's zero, or for a name of all sixteen the last
 * mark it wrote. */
void battle_convert_name(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t text[NAME_TILES];
    unsigned length = 0;
    for (unsigned i = 0; i < NAME_TILES; i++)
        text[i] = battle_name_buffer(g, i);
    while (length < NAME_TILES && text[length] != NAME_END)
        length++;
    NameTiles tiles = battle_name_tiles(text);
    battle_leave_name_buffer(g, text, &tiles);
    s->cpu.x = (uint16_t)length;
    s->cpu.a = length < NAME_TILES ? NAME_END : tiles.marks[NAME_TILES - 1];
    m_rts(s);
}
