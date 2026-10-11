/* What the overworld reads from the ROM: the walkers' animations, the places'
 * names and the sign's sprites.
 *
 * Every such read goes through an accessor here, named after what the data is
 * for, so a build that loads the data from somewhere other than the ROM image
 * changes this file and nothing that uses it. */
#ifndef CT_GAME_OVERWORLD_DATA_H
#define CT_GAME_OVERWORLD_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/object.h"
#include "rom.h"
#include "state.h"

/* The walkers' animations, as sprite scripts by number (menu/sprites.h): for
 * each way a walker faces, two walking ones ($C2:397F, two bytes a facing,
 * which a walker's gait picks between) and a standing one ($C2:39AA, a byte a
 * facing). Magus walks and stands with his own, a fixed count further on. */
static inline uint8_t overworld_walking_animation(const GameState *g, uint8_t facing, uint8_t gait)
{
    return rom_read8(g, 0xC2397Fu + (uint8_t)(facing * 2u + gait));
}
static inline uint8_t overworld_standing_animation(const GameState *g, uint8_t facing)
{
    return rom_read8(g, 0xC239AAu + facing);
}
enum { OVERWORLD_MAGUS = 6, OVERWORLD_MAGUS_ANIMATIONS_ON = 12 };

/* The idle animation each of the party plays once after standing long
 * enough, the leader's first. */
enum { OVERWORLD_FIRST_IDLE_ANIMATION = 0xA2 };

/* The places' names, the strings the sign sets (a table of strings in bank
 * C6, as the typesetter reads one: menu_text). */
enum { OVERWORLD_PLACE_NAMES = 0xC6F400 };

/* The sign's sprite script as the ROM keeps it ($C2:57C2), which the sign
 * copies to work RAM to show: a command showing the sprite group after it,
 * held, and the group: its count of sprites, then six of them side by side
 * over the tiles the place's name is set in. */
enum { OVERWORLD_SIGN_SPRITES_BYTES = 0x1D };
static inline uint8_t overworld_sign_sprites_byte(const GameState *g, unsigned at)
{
    return rom_read8(g, 0xC257C2u + at);
}

/* How far each of the things the vehicles test against one another reaches
 * from its place on the map, left, right, up and down, in pixels: records of
 * four words in bank C2, which the tests of whether two are within reach
 * ($C2:2345) read. The party's leader as it boards, the Epoch and the dactyl
 * where they stand ($C2:324C); the Epoch in flight, the dactyl and the Black
 * Omen as the Epoch flies over them ($C2:4946). */
typedef struct {
    uint16_t left, right, up, down;
} Reach;
typedef enum {
    REACH_OF_THE_PARTY = 0x324C,
    REACH_OF_THE_EPOCH = 0x3254,
    REACH_OF_THE_DACTYL = 0x325C,
    REACH_OF_THE_EPOCH_IN_FLIGHT = 0x4946,
    REACH_OF_THE_DACTYL_FROM_THE_AIR = 0x494E,
    REACH_OF_THE_BLACK_OMEN = 0x4956,
} ReachRecord;
enum { OVERWORLD_REACHES_BANK = 0xC20000 };
static inline Reach overworld_reach(const GameState *g, ReachRecord r)
{
    uint32_t at = OVERWORLD_REACHES_BANK | r;
    return (Reach){ rom_read16(g, at), rom_read16(g, at + 2), rom_read16(g, at + 4), rom_read16(g, at + 6) };
}

/* The Epoch's sprite scripts: standing with its wings or without them,
 * taking off and landing, and the second sprite it is drawn with, standing
 * and in flight; and in flight, the way it faces, with its wings ($C2:48C9,
 * a byte a facing) or without ($C2:48C5). */
enum {
    EPOCH_STANDS_WITH_ITS_WINGS = 0x18,
    EPOCH_STANDS_WITHOUT_WINGS = 0x2D,
    EPOCH_TAKES_OFF = 0x19,
    EPOCH_LANDS = 0x1A,
    EPOCH_SECOND_SPRITE_STANDING = 0x4E,
    EPOCH_SECOND_SPRITE_IN_FLIGHT = 0x1F,
};
static inline uint8_t overworld_epoch_flying_animation(const GameState *g, bool wings, uint8_t facing)
{
    return rom_read8(g, (wings ? 0xC248C9u : 0xC248C5u) + facing);
}

/* How fast the Epoch flies the way it faces, in whole pixels a frame across
 * and down ($C2:4899, two words a facing). */
static inline int16_t overworld_epoch_pace(const GameState *g, uint8_t facing, MenuAxis axis)
{
    return (int16_t)rom_read16(g, 0xC24899u + facing * 4u + axis * 2u);
}

/* The scripts the Epoch's scenes run (menu/script.h): arriving through time,
 * leaving through time, taking off in the scene at the story's point $D3,
 * and entering the Black Omen; and the one the vehicles' controller starts
 * instead of boarding at the story's point $D2. */
enum {
    EPOCH_ARRIVES_THROUGH_TIME = 0xC24B8C,
    EPOCH_LEAVES_THROUGH_TIME = 0xC24AAD,
    EPOCH_TAKES_OFF_IN_A_SCENE = 0xC24A9B,
    EPOCH_ENTERS_THE_BLACK_OMEN = 0xC24C8F,
    EPOCH_BOARDED_IN_A_SCENE = 0xC231A8,
};

/* The Black Omen's name among the places' names (the sign's). */
enum { OVERWORLD_BLACK_OMEN_NAME = 0x4C };

/* ---- The world maps' screens ------------------------------------------------------------- */

/* Each of the eight world maps (the locations from $1F0) has a record of the
 * streams its screen is built from ($C6:FD00, a 16-bit pointer into bank C6 a
 * map), a byte each naming a stream in one of the tables below by its place
 * in the table (setup.c says which byte is which). The original looks a map's
 * record up from the location's low nine bits less $1F0, doubled as a word,
 * so a location before the world maps reaches round to what lies past the
 * table. */
enum { OVERWORLD_FIRST_MAP = 0x1F0, OVERWORLD_WORLD_MAPS = 8, OVERWORLD_RECORDS_BANK = 0xC60000 };
static inline uint32_t overworld_map_record_of(const GameState *g, uint16_t location)
{
    uint16_t at = (uint16_t)(((location & 0x1FFu) - OVERWORLD_FIRST_MAP) * 2u);
    return OVERWORLD_RECORDS_BANK | rom_read16(g, 0xC6FD00u + at);
}
static inline uint8_t overworld_map_record_byte(const GameState *g, uint32_t record, uint8_t field)
{
    return rom_read8(g, (record + field) & 0xFFFFFFu);
}

/* The tables of streams the records name (MD2 resources in bank C6, 24-bit
 * pointers to streams the decompressor takes): the sprites' graphics, the
 * map's graphics, palettes, the map's tile groups, its maps, the top layer's
 * tilemaps, the ground's kinds, the regions, the lists of places and the
 * scenes' scripts. A stream's number is tripled in a byte, as the original
 * indexes them. */
typedef enum {
    OVERWORLD_SPRITE_GRAPHICS = 0xC6FDF0,
    OVERWORLD_MAP_GRAPHICS = 0xC6FE20,
    OVERWORLD_PALETTES = 0xC6FEA0,
    OVERWORLD_TILE_GROUPS = 0xC6FF00,
    OVERWORLD_MAPS = 0xC6FF20,
    OVERWORLD_TOP_LAYER_TILEMAPS = 0xC6FF40,
    OVERWORLD_GROUND_KINDS = 0xC6FF80,
    OVERWORLD_REGION_MAPS = 0xC6FFA0,
    OVERWORLD_PLACE_LISTS = 0xC6FFC0,
    OVERWORLD_SCENE_SCRIPTS = 0xC6FFE0,
} OverworldStreamTable;
static inline uint32_t overworld_stream(const GameState *g, OverworldStreamTable table, uint8_t number)
{
    uint32_t at = (uint32_t)table + (uint8_t)(number * 3u);
    return rom_read16(g, at) | (uint32_t)rom_read8(g, at + 2) << 16;
}

/* The colours of the last sprite palette, which every world map shows
 * ($C6:F620, sixteen colours), and the byte after them (the first of the
 * scenes' velocities, menu_data.h), which the original's copy of them runs
 * on to. */
static inline uint8_t overworld_last_sprite_colours_byte(const GameState *g, unsigned at)
{
    return rom_read8(g, 0xC6F620u + at);
}

#endif
