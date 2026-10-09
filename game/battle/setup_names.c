/* Battle: the names the battle shows, as tiles.
 *
 * The window and the menus draw names as two rows of tiles (name_tiles.c).
 * The battle makes those rows once, as it starts, for the seven characters,
 * the party's three members and the (up to three) kinds of enemy in the
 * fight, and keeps them where the window finds them. A character's or
 * member's name is five characters; an enemy's is eleven. */
#include "battle/battle.h"
#include "battle/setup.h"

#include <string.h>

#include "battle/setup_data.h"
#include "original_effects.h"
#include "vars.h"

enum { SHORT_NAME = 5, ENEMY_NAME = 11, ENEMY_NAME_ROW = 12 };

/* The seven characters' names, copied out of their six-byte records. */
// $CC:E565
void setup_copy_character_names(GameState *g)
{
    for (unsigned character = 0; character < SETUP_CHARACTERS; character++)
        for (unsigned i = 0; i < SHORT_NAME; i++)
            set_battle_character_names_byte(g, character, i, character_names_byte(g, character, i));
}

/* A character of a five-character name, from a byte offset into its table. */
static uint8_t short_name_char(const GameState *g, NameTable table, unsigned at)
{
    return table == CHARACTER_NAMES ? battle_character_names_byte(g, at / SHORT_NAME, at % SHORT_NAME)
                                    : battle_party_names_byte(g, at / SHORT_NAME, at % SHORT_NAME);
}

static void set_name_tile(GameState *g, NameTable table, unsigned at, uint8_t tile)
{
    if (table == CHARACTER_NAMES)
        set_battle_character_name_tiles(g, at, tile);
    else
        set_battle_party_name_tiles(g, at, tile);
}

// $CC:E595
void setup_name_text(GameState *g, NameTable table, uint8_t n, uint8_t text[NAME_TILES])
{
    uint8_t at = setup_name_at(g, n);
    memset(text, NAME_END, NAME_TILES);
    for (unsigned i = 0; i < SHORT_NAME; i++)
        text[i] = short_name_char(g, table, at + i);
    ORIGINAL_EFFECT(setup_leave_word(g, 0x80, n));
}

void setup_keep_name_tiles(GameState *g, NameTable table, uint8_t n, const NameTiles *tiles)
{
    uint8_t at = setup_name_tiles_at(g, n);
    for (unsigned i = 0; i < SHORT_NAME; i++) {
        set_name_tile(g, table, at + i, tiles->glyphs[i]);
        set_name_tile(g, table, at + SHORT_NAME + i, tiles->marks[i]);
    }
}

/* ---- The enemies' kinds ------------------------------------------------------------------- */

enum { NO_KIND = 0x8000 };

bool setup_enemy_kind_present(const GameState *g, uint8_t kind) { return !(battle_enemy_kinds(g, kind) & NO_KIND); }

/* The original counts the kinds in its working cells by twos, and keeps the
 * name's place in the names ahead of the conversion. */
static void leave_enemy_name(GameState *g, uint8_t kind, uint16_t number)
{
    setup_leave_word(g, 0x80, (uint16_t)(2 * kind));
    setup_leave_word(g, 0x82, (uint16_t)(number * 4));
}

// $CC:ED03
void setup_enemy_name_text(GameState *g, uint8_t kind, uint8_t text[NAME_TILES])
{
    uint16_t number = battle_enemy_kinds(g, kind);
    uint16_t at = (uint16_t)(number * ENEMY_NAME);
    memset(text, NAME_END, NAME_TILES);
    for (unsigned i = 0; i < ENEMY_NAME; i++)
        text[i] = setup_enemy_name_char(g, (uint16_t)(at + i));
    ORIGINAL_EFFECT(leave_enemy_name(g, kind, number));
}

/* An enemy's rows are twelve tiles, the name's eleven and the blank after. */
void setup_keep_enemy_name_tiles(GameState *g, uint8_t kind, const NameTiles *tiles)
{
    unsigned at = kind * 2u * ENEMY_NAME_ROW;
    for (unsigned i = 0; i < ENEMY_NAME_ROW; i++) {
        set_battle_enemy_name_tiles(g, at + i, tiles->glyphs[i]);
        set_battle_enemy_name_tiles(g, at + ENEMY_NAME_ROW + i, tiles->marks[i]);
    }
    ORIGINAL_EFFECT(setup_leave(g, 0x82, (uint8_t)(8 * kind)));
}

/* The buffer as the last conversion leaves it. The original writes a name's
 * text, and its end, over the rows the conversion before it left, whose
 * blanks follow it: `written` bytes, a short name's six or an enemy's
 * thirteen (its eleven and two ends). */
enum { SHORT_NAME_WRITTEN = SHORT_NAME + 1, ENEMY_NAME_WRITTEN = ENEMY_NAME + 2 };

void setup_leave_name_buffer(GameState *g, const uint8_t text[NAME_TILES], bool enemy, const NameTiles *tiles)
{
    unsigned written = enemy ? ENEMY_NAME_WRITTEN : SHORT_NAME_WRITTEN;
    uint8_t copied[NAME_TILES];
    for (unsigned i = 0; i < NAME_TILES; i++)
        copied[i] = i < written ? text[i] : TILE_BLANK;
    battle_leave_name_buffer(g, copied, tiles);
}

void setup_enemy_names_converted(GameState *g)
{
    ORIGINAL_EFFECT(setup_leave_word(g, 0x80, 2 * SETUP_ENEMY_KINDS));
}
