/* Battle: what an action's animation is made of, as the ROM keeps it.
 *
 * Every action the battle shows -- a party member's attack with its weapon,
 * an enemy's attack, a tech of either side, an item -- has an animation: a
 * set of animation scripts, one for each channel it runs (battle/scripts.c),
 * two graphics sets for the effects' tiles, the compressed sprite tiles its
 * effects draw with, a palette animation, the animation records its effects
 * take their frames from, and for some a battlefield the background changes
 * to. Each kind of action keeps its animations in a table of its own, and the
 * parts are numbers into tables shared by the kinds that use them; every
 * read battle/action_animation.c makes of them goes through here (the asset
 * seam, docs/PLAN.md Phase 3), at the index the original uses. */
#ifndef CT_GAME_BATTLE_ACTION_ANIMATION_DATA_H
#define CT_GAME_BATTLE_ACTION_ANIMATION_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "rom.h"
#include "state.h"

/* The animation's background where it has none to change to. */
enum { ANIMATION_NO_BACKGROUND = 0xFF };

typedef struct {
    uint8_t scripts;          /* the script set */
    uint8_t graphics;         /* the first graphics set for the effects' tiles */
    uint8_t second_graphics;  /* the second */
    uint8_t tiles;            /* the compressed sprite tiles */
    uint8_t palette;          /* the palette animation */
    uint8_t records;          /* the animation records */
    uint8_t background;       /* the battlefield it changes to, or ANIMATION_NO_BACKGROUND */
} ActionAnimation;

/* Who keeps the animation's parts: each of these has its own entries in the
 * script sets', the palettes' and the records' tables, and the party's
 * attacks their own tiles too. */
typedef enum {
    ANIMATIONS_OF_PARTY_ATTACKS,
    ANIMATIONS_OF_ENEMY_ATTACKS,
    ANIMATIONS_OF_PARTY_TECHS,
    ANIMATIONS_OF_ENEMY_TECHS,
    ANIMATIONS_OF_ITEMS,
} AnimationKeeper;

/* ---- The animations, by kind of action ----------------------------------------------------- */

/* A party member's attack is shown as its weapon's, one of three views of it:
 * from where the member stands, up close (the member walks to its target),
 * and a critical hit. The weapon gives the parts but the script set, a view
 * five bytes of its record ($CD:4015); the character gives the script set
 * ($CD:4000, a byte a character for each view). */
typedef enum { ATTACK_FROM_AFAR = 0, ATTACK_UP_CLOSE = 1, ATTACK_CRITICAL = 2 } AttackView;

enum { WEAPON_VIEW_BYTES = 5, WEAPON_VIEWS = 3, CHARACTER_SCRIPTS_PER_VIEW = 7 };

/* The other kinds' records start with the script set, and the techs' and the
 * items' end with the background. An enemy has two attacks ($CD:4926, the
 * first, and $CD:4F26), six bytes each by the enemy's number. */
enum { ENEMY_ATTACK_BYTES = 6, ANIMATION_BYTES = 7 };


/* A script set: which of the sixteen channels run a script, for the set's
 * two halves (a word each, high byte first, the first channel the top bit),
 * then where each running channel's script starts, a word each in the set's
 * bank, the first half's channels' first. The original reads the halves
 * from the set's place in long addressing, a set at the bank's end running on
 * into the next bank. */
enum { SCRIPT_SET_CHANNELS = 16, SCRIPT_SET_HEADER_BYTES = 4 };

/* ---- The sixteen one-sprite effects ------------------------------------------------------- */

/* The effect sprite sets 32-47 hold one sprite each, from the effects'
 * one-sprite shapes ($D1:50A2, six bytes each): its tile and attributes. */
enum { ONE_SPRITE_SHAPE_BYTES = 6 };

/* ---- The developers' switches -------------------------------------------------------------- */

/* Three bytes of the ROM ($CF:FFFD-$CF:FFFF), all 0 in the released game, by
 * which the developers forced every action's animation: as a critical hit, as
 * its second script set, or as neither. */
typedef enum {
    ANIMATIONS_AS_THEY_ARE,
    ANIMATIONS_ALL_CRITICAL,
    ANIMATIONS_ALL_SECOND,
    ANIMATIONS_ALL_PLAIN,
} AnimationSwitch;

/* ---- A party member's frames ---------------------------------------------------------------- */

/* The frame a character shows falling to the ground ($D1:5F42), by facing. */
enum { BATTLE_FACINGS = 4 };

static inline ActionAnimation battle_weapon_animation(const GameState *g, uint8_t weapon, AttackView view,
                                                      uint8_t character)
{
    uint32_t at = (uint16_t)(weapon * WEAPON_VIEWS * WEAPON_VIEW_BYTES) + view * WEAPON_VIEW_BYTES;
    return (ActionAnimation){
        .scripts = asset_read8(g, ASSET_BATTLE_WEAPON_ANIMATION, view * CHARACTER_SCRIPTS_PER_VIEW + character),
        .graphics = asset_read8(g, ASSET_BATTLE_WEAPON_ANIMATION_4015, at),
        .second_graphics = asset_read8(g, ASSET_BATTLE_WEAPON_ANIMATION_4015, at + 1),
        .tiles = asset_read8(g, ASSET_BATTLE_WEAPON_ANIMATION_4015, at + 2),
        .palette = asset_read8(g, ASSET_BATTLE_WEAPON_ANIMATION_4015, at + 3),
        .records = asset_read8(g, ASSET_BATTLE_WEAPON_ANIMATION_4015, at + 4),
        .background = ANIMATION_NO_BACKGROUND,
    };
}

static inline ActionAnimation animation_record(const GameState *g, AssetId table, uint32_t at, bool background)
{
    return (ActionAnimation){
        .scripts = asset_read8(g, table, at),
        .graphics = asset_read8(g, table, at + 1),
        .second_graphics = asset_read8(g, table, at + 2),
        .tiles = asset_read8(g, table, at + 3),
        .palette = asset_read8(g, table, at + 4),
        .records = asset_read8(g, table, at + 5),
        .background = background ? asset_read8(g, table, at + 6) : ANIMATION_NO_BACKGROUND,
    };
}

/* An enemy's attack's ($CD:4926, or its second $CD:4F26), by the enemy. */
static inline ActionAnimation battle_enemy_attack_animation(const GameState *g, uint16_t enemy, bool second)
{
    return animation_record(g, second ? ASSET_ENEMY_ATTACK_SECOND_ANIMATIONS : ASSET_ENEMY_ATTACK_ANIMATIONS,
                            (uint16_t)(enemy * ENEMY_ATTACK_BYTES), false);
}

/* A tech's, the party's ($CD:45A6) or an enemy's ($CD:5526), by its number;
 * an item's ($CD:5C26), by its number past the items' first. */
static inline ActionAnimation battle_tech_animation(const GameState *g, bool enemy, uint8_t tech)
{
    return animation_record(g, enemy ? ASSET_ENEMY_TECH_ANIMATIONS : ASSET_PARTY_TECH_ANIMATIONS,
                            (uint16_t)(tech * ANIMATION_BYTES), true);
}

static inline ActionAnimation battle_item_animation(const GameState *g, uint8_t item)
{
    return animation_record(g, ASSET_ITEM_ANIMATIONS, (uint16_t)(item * ANIMATION_BYTES), true);
}

/* ---- The parts ----------------------------------------------------------------------------- */

/* A part's number picks a word from its keeper's table, `first` words in. */
static inline uint16_t keeper_word(const GameState *g, AssetId table, uint16_t first, uint8_t number)
{
    return asset_read16(g, table, (uint16_t)(first + number * 2));
}

/* Where the script set starts, and the bank its scripts are in: the party's
 * and the items' with the animations in bank $CE, the enemies' with those in
 * bank $CD. */
static inline uint8_t battle_script_set_bank(AnimationKeeper keeper)
{
    AssetId scripts = keeper == ANIMATIONS_OF_ENEMY_ATTACKS || keeper == ANIMATIONS_OF_ENEMY_TECHS
                          ? ASSET_BATTLE_ANIMATIONS_CD : ASSET_BATTLE_ANIMATIONS_CE;
    return (uint8_t)(asset_address(scripts) >> 16);
}

static inline uint16_t battle_script_set_at(const GameState *g, AnimationKeeper keeper, uint8_t set)
{
    static const AssetId tables[] = {
        [ANIMATIONS_OF_PARTY_ATTACKS] = ASSET_PARTY_ATTACK_SCRIPT_SETS, [ANIMATIONS_OF_ENEMY_ATTACKS] = ASSET_ENEMY_ATTACK_SCRIPT_SETS,
        [ANIMATIONS_OF_PARTY_TECHS] = ASSET_PARTY_TECH_SCRIPT_SETS,     [ANIMATIONS_OF_ENEMY_TECHS] = ASSET_ENEMY_TECH_SCRIPT_SETS,
        [ANIMATIONS_OF_ITEMS] = ASSET_ITEM_SCRIPT_SETS,
    };
    return keeper_word(g, tables[keeper], 0, set);
}

/* Where the compressed sprite tiles are in bank $D1: the party's attacks' own
 * table, everyone else's after it. */
enum { OTHERS_TILES = 0x60 };

static inline uint16_t battle_animation_tiles_place(const GameState *g, AnimationKeeper keeper, uint8_t tiles)
{
    return keeper_word(g, ASSET_BATTLE_ANIMATION_TILES_PLACE, keeper == ANIMATIONS_OF_PARTY_ATTACKS ? 0 : OTHERS_TILES, tiles);
}

/* The long pointer to a stream of those tiles, by its place. */
static inline uint32_t battle_animation_tiles_stream(uint16_t place)
{
    return asset_place_address(ASSET_BATTLE_ANIMATION_TILES_PLACE, place);
}

/* Where the palette animation's frames are (battle_palette_anim_frames): the
 * enemies share a table. */
static inline uint16_t battle_animation_palette_frames(const GameState *g, AnimationKeeper keeper, uint8_t palette)
{
    static const AssetId tables[] = {
        [ANIMATIONS_OF_PARTY_ATTACKS] = ASSET_PARTY_ATTACK_PALETTE_FRAMES_AT, [ANIMATIONS_OF_ENEMY_ATTACKS] = ASSET_ENEMY_PALETTE_FRAMES_AT,
        [ANIMATIONS_OF_PARTY_TECHS] = ASSET_PARTY_TECH_PALETTE_FRAMES_AT,     [ANIMATIONS_OF_ENEMY_TECHS] = ASSET_ENEMY_PALETTE_FRAMES_AT,
        [ANIMATIONS_OF_ITEMS] = ASSET_ITEM_PALETTE_FRAMES_AT,
    };
    return keeper_word(g, tables[keeper], 0, palette);
}

/* Where the animation records are (battle_effect_records): the enemies share
 * a table, and the party's techs share one with the items. */
static inline uint16_t battle_animation_records_place(const GameState *g, AnimationKeeper keeper, uint8_t records)
{
    static const AssetId tables[] = {
        [ANIMATIONS_OF_PARTY_ATTACKS] = ASSET_PARTY_ATTACK_EFFECT_RECORDS_AT, [ANIMATIONS_OF_ENEMY_ATTACKS] = ASSET_ENEMY_EFFECT_RECORDS_AT,
        [ANIMATIONS_OF_PARTY_TECHS] = ASSET_PARTY_TECH_EFFECT_RECORDS_AT,     [ANIMATIONS_OF_ENEMY_TECHS] = ASSET_ENEMY_EFFECT_RECORDS_AT,
        [ANIMATIONS_OF_ITEMS] = ASSET_PARTY_TECH_EFFECT_RECORDS_AT,
    };
    return keeper_word(g, tables[keeper], 0, records);
}

/* A script set's channels and scripts, at the place its table gives in the
 * bank its scripts are in: read on in long addressing, as the original does,
 * so a set at the bank's end runs on into the next. */
static inline uint16_t battle_script_set_channels(const GameState *g, uint8_t bank, uint16_t set, bool second_half)
{
    uint32_t at = ((uint32_t)bank << 16) + set + (second_half ? 2u : 0u);
    return (uint16_t)(rom_read8(g, at) << 8 | rom_read8(g, at + 1));
}

static inline uint16_t battle_script_set_script(const GameState *g, uint8_t bank, uint16_t at)
{
    return rom_read16(g, (uint32_t)bank << 16 | at);
}

/* ---- What a party member's attack decides by ----------------------------------------------- */

/* Whether the character walks up to its target to attack ($CC:F63C), as all
 * do but those who shoot. */
static inline bool battle_character_walks_up(const GameState *g, uint8_t character)
{
    return asset_read8(g, ASSET_BATTLE_CHARACTER_WALKS_UP, character) != 0;
}

static inline uint8_t battle_one_sprite_tile(const GameState *g, unsigned shape)
{
    return asset_read8(g, ASSET_BATTLE_ONE_SPRITE_TILE, shape * ONE_SPRITE_SHAPE_BYTES);
}

static inline uint8_t battle_one_sprite_attributes(const GameState *g, unsigned shape)
{
    return asset_read8(g, ASSET_BATTLE_ONE_SPRITE_ATTRIBUTES, shape * ONE_SPRITE_SHAPE_BYTES);
}

static inline AnimationSwitch battle_animation_switch(const GameState *g)
{
    if (asset_read8(g, ASSET_BATTLE_ANIMATION_SWITCH, 0x17Au))
        return ANIMATIONS_ALL_CRITICAL;
    if (asset_read8(g, ASSET_BATTLE_ANIMATION_SWITCH, 0x179u))
        return ANIMATIONS_ALL_SECOND;
    if (asset_read8(g, ASSET_BATTLE_ANIMATION_SWITCH, 0x178u))
        return ANIMATIONS_ALL_PLAIN;
    return ANIMATIONS_AS_THEY_ARE;
}

static inline uint8_t battle_character_falling_frame(const GameState *g, uint8_t character, uint8_t facing)
{
    return asset_read8(g, ASSET_BATTLE_CHARACTER_FALLING_FRAME, (uint8_t)(character * BATTLE_FACINGS + facing));
}

#endif
