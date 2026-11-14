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

enum { OVERWORLD_MAGUS = 6, OVERWORLD_MAGUS_ANIMATIONS_ON = 12 };

/* The idle animation each of the party plays once after standing long
 * enough, the leader's first. */
enum { OVERWORLD_FIRST_IDLE_ANIMATION = 0xA2 };

/* The places' names, the strings the sign sets (a table of strings in bank
 * C6, as the typesetter reads one: menu_text). */
static inline uint32_t overworld_place_names(void) { return asset_address(ASSET_OVERWORLD_PLACE_NAMES); }

/* The sign's sprite script as the ROM keeps it ($C2:57C2), which the sign
 * copies to work RAM to show: a command showing the sprite group after it,
 * held, and the group: its count of sprites, then six of them side by side
 * over the tiles the place's name is set in. */
enum { OVERWORLD_SIGN_SPRITES_BYTES = 0x1D };

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
    REACH_OF_THE_PARTY,
    REACH_OF_THE_EPOCH,
    REACH_OF_THE_DACTYL,
    REACH_OF_THE_EPOCH_IN_FLIGHT,
    REACH_OF_THE_DACTYL_FROM_THE_AIR,
    REACH_OF_THE_BLACK_OMEN,
} ReachRecord;
enum { REACHES_A_TABLE = 3, REACH_BYTES = 8 };

/* The second sprite each vehicle is drawn with, the Epoch and the dactyls
 * alike: standing, and in flight. */
enum { VEHICLE_SECOND_SPRITE_STANDING = 0x4E, VEHICLE_SECOND_SPRITE_IN_FLIGHT = 0x1F };

/* The Epoch's sprite scripts: standing with its wings or without them,
 * taking off and landing; and in flight, the way it faces, with its wings
 * ($C2:48C9, a byte a facing) or without ($C2:48C5). */
enum {
    EPOCH_STANDS_WITH_ITS_WINGS = 0x18,
    EPOCH_STANDS_WITHOUT_WINGS = 0x2D,
    EPOCH_TAKES_OFF = 0x19,
    EPOCH_LANDS = 0x1A,
};

/* The scripts the Epoch's scenes run (menu/script.h): arriving through time,
 * leaving through time, taking off in the scene at the story's point $D3,
 * and entering the Black Omen; and the one the vehicles' controller starts
 * instead of boarding at the story's point $D2. */
enum {
    EPOCH_TAKES_OFF_IN_A_SCENE = 0x000,
    EPOCH_LEAVES_THROUGH_TIME = 0x012,
    EPOCH_ARRIVES_THROUGH_TIME = 0x0F1,
    EPOCH_ENTERS_THE_BLACK_OMEN = 0x1F4,
};

/* Where an Epoch scene's script starts, by where it is in the scenes'
 * scripts, and the boarding scene's own. */
static inline uint32_t epoch_scene_script(uint16_t at) { return asset_address(ASSET_EPOCH_SCENE_SCRIPTS) + at; }
static inline uint32_t epoch_boarded_in_a_scene(void) { return asset_address(ASSET_EPOCH_BOARDED_IN_A_SCENE); }

/* Where a script that fades the map out or in starts, `at` bytes into the
 * fades' scripts ($C2:2EC1). */
static inline uint32_t overworld_fade_script(uint16_t at) { return asset_address(ASSET_OVERWORLD_FADE_SCRIPTS) + at; }

/* ---- The dactyls ------------------------------------------------------------------------ */

/* The dactyls' sprite scripts: the leader's standing, and taking off and
 * landing alike; the second's and third's taking off and landing alike, the
 * second member's first. */
enum {
    DACTYL_STANDS = 0x24,
    DACTYL_TAKES_OFF = 0x29,
    SECOND_S_DACTYL_TAKES_OFF = 0x6F,
    THIRD_S_DACTYL_TAKES_OFF = 0x74,
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
enum { OVERWORLD_FIRST_MAP = 0x1F0, OVERWORLD_WORLD_MAPS = 8 };

/* The tables of streams the records name (MD2 resources in bank C6, 24-bit
 * pointers to streams the decompressor takes): the sprites' graphics, the
 * map's graphics, palettes, the map's tile groups, its maps, the top layer's
 * tilemaps, the ground's kinds, the regions, the lists of places and the
 * scenes' scripts. A stream's number is tripled in a byte, as the original
 * indexes them. */
typedef enum {
    OVERWORLD_SPRITE_GRAPHICS = ASSET_OVERWORLD_SPRITE_GRAPHICS,
    OVERWORLD_MAP_GRAPHICS = ASSET_OVERWORLD_MAP_GRAPHICS,
    OVERWORLD_PALETTES = ASSET_OVERWORLD_PALETTES,
    OVERWORLD_TILE_GROUPS = ASSET_OVERWORLD_TILE_GROUPS,
    OVERWORLD_MAPS = ASSET_OVERWORLD_MAPS,
    OVERWORLD_TOP_LAYER_TILEMAPS = ASSET_OVERWORLD_TOP_LAYER_TILEMAPS,
    OVERWORLD_GROUND_KINDS = ASSET_OVERWORLD_GROUND_KINDS,
    OVERWORLD_REGION_MAPS = ASSET_OVERWORLD_REGION_MAPS,
    OVERWORLD_PLACE_LISTS = ASSET_OVERWORLD_PLACE_LISTS,
    OVERWORLD_SCENE_SCRIPTS = ASSET_SCENE_SCRIPT_PTRS,
} OverworldStreamTable;

/* The walkers' animations, as sprite scripts by number (menu/sprites.h): for
 * each way a walker faces, two walking ones ($C2:397F, two bytes a facing,
 * which a walker's gait picks between) and a standing one ($C2:39AA, a byte a
 * facing). Magus walks and stands with his own, a fixed count further on. */
static inline uint8_t overworld_walking_animation(const GameState *g, uint8_t facing, uint8_t gait)
{
    return asset_read8(g, ASSET_OVERWORLD_WALKING_ANIMATION, (uint8_t)(facing * 2u + gait));
}

static inline uint8_t overworld_standing_animation(const GameState *g, uint8_t facing)
{
    return asset_read8(g, ASSET_OVERWORLD_STANDING_ANIMATION, facing);
}

static inline uint8_t overworld_sign_sprites_byte(const GameState *g, unsigned at)
{
    return asset_read8(g, ASSET_OVERWORLD_SIGN_SPRITES_BYTE, 0x12u + at);
}

static inline Reach overworld_reach(const GameState *g, ReachRecord r)
{
    AssetId reaches = (unsigned)r < REACHES_A_TABLE ? ASSET_OVERWORLD_STANDING_REACHES : ASSET_OVERWORLD_FLYING_REACHES;
    uint32_t at = ((unsigned)r % REACHES_A_TABLE) * REACH_BYTES;
    return (Reach){ asset_read16(g, reaches, at), asset_read16(g, reaches, at + 2), asset_read16(g, reaches, at + 4),
                    asset_read16(g, reaches, at + 6) };
}

static inline uint8_t overworld_epoch_flying_animation(const GameState *g, bool wings, uint8_t facing)
{
    enum { WINGS = 4 };   /* the wings' frames after the hull's */
    return asset_read8(g, ASSET_OVERWORLD_EPOCH_FLYING_ANIMATION, (wings ? WINGS : 0) + facing);
}

/* How fast the Epoch flies the way it faces, in whole pixels a frame across
 * and down ($C2:4899, two words a facing). */
static inline int16_t overworld_epoch_pace(const GameState *g, uint8_t facing, MenuAxis axis)
{
    return (int16_t)asset_read16(g, ASSET_OVERWORLD_EPOCH_PACE, facing * 4u + axis * 2u);
}

/* The leader's dactyl flies the way it faces two pixels a frame, across and
 * down ($C2:5037, two words a facing), showing its flying animation that way
 * ($C2:5047, a byte a facing). */
static inline int16_t overworld_dactyl_pace(const GameState *g, uint8_t facing, MenuAxis axis)
{
    return (int16_t)asset_read16(g, ASSET_OVERWORLD_DACTYL_PACE, facing * 4u + axis * 2u);
}

static inline uint8_t overworld_dactyl_flying_animation(const GameState *g, uint8_t facing)
{
    return asset_read8(g, ASSET_OVERWORLD_DACTYL_PACE, 0x10u + facing);
}

/* Where the leader's dactyl's second sprite is drawn, below its spot (above
 * where negative), standing and in flight ($C2:508F, a word of each and a
 * word of its sprite script). */
static inline int16_t overworld_dactyl_second_sprite_below(const GameState *g, bool flying)
{
    return (int16_t)asset_read16(g, ASSET_OVERWORLD_DACTYL_SECOND_SPRITE_BELOW, (flying ? 4u : 0u));
}

static inline uint8_t overworld_dactyl_second_sprite(const GameState *g, bool flying)
{
    return asset_read8(g, ASSET_OVERWORLD_DACTYL_SECOND_SPRITE_BELOW, 0x2u + (flying ? 4u : 0u));
}

/* The second and third members' dactyls fly in a V behind the leader's: each
 * its place across and down from the leader's spot by the way the leader
 * flies ($C2:543C, two words a place, the two followers' places for each
 * facing in turn), and its flying animation the way it faces ($C2:548C, four
 * a follower). */
static inline int16_t overworld_formation_place(const GameState *g, uint8_t leader_facing, unsigned follower, MenuAxis axis)
{
    return (int16_t)asset_read16(g, ASSET_OVERWORLD_FORMATION_PLACE, (leader_facing * 2u + follower) * 4u + axis * 2u);
}

static inline uint8_t overworld_following_dactyl_flying_animation(const GameState *g, unsigned follower, uint8_t facing)
{
    return asset_read8(g, ASSET_OVERWORLD_FOLLOWING_DACTYL_FLYING_ANIMATION, follower * 4u + facing);
}

static inline uint32_t overworld_map_record_of(const GameState *g, uint16_t location)
{
    uint16_t at = (uint16_t)(((location & 0x1FFu) - OVERWORLD_FIRST_MAP) * 2u);
    return asset_place_address(ASSET_OVERWORLD_MAP_RECORD_OF, asset_read16(g, ASSET_OVERWORLD_MAP_RECORD_OF, at));
}

static inline uint8_t overworld_map_record_byte(const GameState *g, uint32_t record, uint8_t field)
{
    return rom_read8(g, (record + field) & 0xFFFFFFu);
}

static inline uint32_t overworld_stream(const GameState *g, OverworldStreamTable table, uint8_t number)
{
    uint32_t at = (uint8_t)(number * 3u);
    return asset_read16(g, (AssetId)table, at) | (uint32_t)asset_read8(g, (AssetId)table, at + 2) << 16;
}

/* The colours of the last sprite palette, which every world map shows
 * ($C6:F620, sixteen colours), and the byte after them (the first of the
 * scenes' velocities, menu_data.h), which the original's copy of them runs
 * on to. */
static inline uint8_t overworld_last_sprite_colours_byte(const GameState *g, unsigned at)
{
    return asset_read8(g, ASSET_OVERWORLD_LAST_SPRITE_COLORS, at);
}

#endif
