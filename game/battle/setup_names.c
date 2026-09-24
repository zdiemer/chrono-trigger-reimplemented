/* Battle: the names the battle shows, as tiles.
 *
 * The window and the menus draw names as two rows of tiles: a character that
 * draws with a mark above it takes two tiles, one in each row. The battle
 * makes those rows once, as it starts, for the seven characters, the party's
 * three members and the (up to three) kinds of enemy in the fight. Each name
 * goes into the name buffer, the original's converter ($C1:01A9, still the
 * original's: battle/setup.c calls it) turns it into its two rows in the same
 * buffer, and the rows are kept where the window finds them. A character's or
 * member's name is five characters; an enemy's is eleven. */
#include "battle/battle.h"
#include "battle/setup.h"

#include "battle/setup_data.h"
#include "original_effects.h"
#include "vars.h"

enum { SHORT_NAME = 5, ENEMY_NAME = 11, ENEMY_NAME_ROW = 12, SECOND_ROW = 16, NAME_END = 0 };

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
void setup_name_to_convert(GameState *g, NameTable table, uint8_t n)
{
    uint8_t at = setup_name_at(g, n);
    for (unsigned i = 0; i < SHORT_NAME; i++)
        set_battle_name_buffer(g, i, short_name_char(g, table, at + i));
    set_battle_name_buffer(g, SHORT_NAME, NAME_END);
    ORIGINAL_EFFECT(setup_leave_word(g, 0x80, n));
}

void setup_keep_name_tiles(GameState *g, NameTable table, uint8_t n)
{
    uint8_t at = setup_name_tiles_at(g, n);
    for (unsigned i = 0; i < SHORT_NAME; i++) {
        set_name_tile(g, table, at + i, battle_name_buffer(g, i));
        set_name_tile(g, table, at + SHORT_NAME + i, battle_name_buffer(g, SECOND_ROW + i));
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
void setup_enemy_name_to_convert(GameState *g, uint8_t kind)
{
    uint16_t number = battle_enemy_kinds(g, kind);
    uint16_t at = (uint16_t)(number * ENEMY_NAME);
    for (unsigned i = 0; i < ENEMY_NAME; i++)
        set_battle_name_buffer(g, i, setup_enemy_name_char(g, (uint16_t)(at + i)));
    set_battle_name_buffer(g, ENEMY_NAME, NAME_END);
    set_battle_name_buffer(g, ENEMY_NAME + 1, NAME_END);
    ORIGINAL_EFFECT(leave_enemy_name(g, kind, number));
}

void setup_keep_enemy_name_tiles(GameState *g, uint8_t kind)
{
    unsigned at = kind * 2u * ENEMY_NAME_ROW;
    for (unsigned i = 0; i < ENEMY_NAME_ROW; i++) {
        set_battle_enemy_name_tiles(g, at + i, battle_name_buffer(g, i));
        set_battle_enemy_name_tiles(g, at + ENEMY_NAME_ROW + i, battle_name_buffer(g, SECOND_ROW + i));
    }
    ORIGINAL_EFFECT(setup_leave(g, 0x82, (uint8_t)(8 * kind)));
}

void setup_enemy_names_converted(GameState *g)
{
    ORIGINAL_EFFECT(setup_leave_word(g, 0x80, 2 * SETUP_ENEMY_KINDS));
}
