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

static inline ActionAnimation battle_weapon_animation(const GameState *g, uint8_t weapon, AttackView view,
                                                      uint8_t character)
{
    uint32_t at = 0xCD4015u + (uint16_t)(weapon * WEAPON_VIEWS * WEAPON_VIEW_BYTES) + view * WEAPON_VIEW_BYTES;
    return (ActionAnimation){
        .scripts = rom_read8(g, 0xCD4000u + view * CHARACTER_SCRIPTS_PER_VIEW + character),
        .graphics = rom_read8(g, at),
        .second_graphics = rom_read8(g, at + 1),
        .tiles = rom_read8(g, at + 2),
        .palette = rom_read8(g, at + 3),
        .records = rom_read8(g, at + 4),
        .background = ANIMATION_NO_BACKGROUND,
    };
}

/* The other kinds' records start with the script set, and the techs' and the
 * items' end with the background. An enemy has two attacks ($CD:4926, the
 * first, and $CD:4F26), six bytes each by the enemy's number. */
enum { ENEMY_ATTACK_BYTES = 6, ANIMATION_BYTES = 7 };

static inline ActionAnimation animation_record(const GameState *g, uint32_t at, bool background)
{
    return (ActionAnimation){
        .scripts = rom_read8(g, at),
        .graphics = rom_read8(g, at + 1),
        .second_graphics = rom_read8(g, at + 2),
        .tiles = rom_read8(g, at + 3),
        .palette = rom_read8(g, at + 4),
        .records = rom_read8(g, at + 5),
        .background = background ? rom_read8(g, at + 6) : ANIMATION_NO_BACKGROUND,
    };
}

static inline ActionAnimation battle_enemy_attack_animation(const GameState *g, uint16_t enemy, bool second)
{
    uint32_t table = second ? 0xCD4F26u : 0xCD4926u;
    return animation_record(g, table + (uint16_t)(enemy * ENEMY_ATTACK_BYTES), false);
}

/* A tech's, the party's ($CD:45A6) or an enemy's ($CD:5526), by its number;
 * an item's ($CD:5C26), by its number past the items' first. */
static inline ActionAnimation battle_tech_animation(const GameState *g, bool enemy, uint8_t tech)
{
    uint32_t table = enemy ? 0xCD5526u : 0xCD45A6u;
    return animation_record(g, table + (uint16_t)(tech * ANIMATION_BYTES), true);
}

static inline ActionAnimation battle_item_animation(const GameState *g, uint8_t item)
{
    return animation_record(g, 0xCD5C26u + (uint16_t)(item * ANIMATION_BYTES), true);
}

/* ---- The parts ----------------------------------------------------------------------------- */

/* A part's number picks a word from its keeper's table. */
static inline uint16_t keeper_word(const GameState *g, uint32_t table, uint8_t number)
{
    return rom_read16(g, table + (uint16_t)(number * 2));
}

/* Where the script set starts, and the bank its scripts are in: the party's
 * and the items' in bank $CE, the enemies' in bank $CD. */
static inline uint8_t battle_script_set_bank(AnimationKeeper keeper)
{
    return keeper == ANIMATIONS_OF_ENEMY_ATTACKS || keeper == ANIMATIONS_OF_ENEMY_TECHS ? 0xCD : 0xCE;
}

static inline uint16_t battle_script_set_at(const GameState *g, AnimationKeeper keeper, uint8_t set)
{
    static const uint32_t tables[] = {
        [ANIMATIONS_OF_PARTY_ATTACKS] = 0xCD5EC6u, [ANIMATIONS_OF_ENEMY_ATTACKS] = 0xCD5FF0u,
        [ANIMATIONS_OF_PARTY_TECHS] = 0xCD5EF0u,   [ANIMATIONS_OF_ENEMY_TECHS] = 0xCD61F0u,
        [ANIMATIONS_OF_ITEMS] = 0xCD63F0u,
    };
    return keeper_word(g, tables[keeper], set);
}

/* Where the compressed sprite tiles are in bank $D1: the party's attacks' own
 * table, everyone else's after it. */
static inline uint16_t battle_animation_tiles_place(const GameState *g, AnimationKeeper keeper, uint8_t tiles)
{
    return keeper_word(g, keeper == ANIMATIONS_OF_PARTY_ATTACKS ? 0xD10000u : 0xD10060u, tiles);
}
enum { ANIMATION_TILES_BANK = 0xD1 };

/* Where the palette animation's frames are (battle_palette_anim_frames): the
 * enemies share a table. */
static inline uint16_t battle_animation_palette_frames(const GameState *g, AnimationKeeper keeper, uint8_t palette)
{
    static const uint32_t tables[] = {
        [ANIMATIONS_OF_PARTY_ATTACKS] = 0xCD686Eu, [ANIMATIONS_OF_ENEMY_ATTACKS] = 0xCD6A2Cu,
        [ANIMATIONS_OF_PARTY_TECHS] = 0xCD692Cu,   [ANIMATIONS_OF_ENEMY_TECHS] = 0xCD6A2Cu,
        [ANIMATIONS_OF_ITEMS] = 0xCD6C2Cu,
    };
    return keeper_word(g, tables[keeper], palette);
}

/* Where the animation records are (battle_effect_records): the enemies share
 * a table, and the party's techs share one with the items. */
static inline uint16_t battle_animation_records_place(const GameState *g, AnimationKeeper keeper, uint8_t records)
{
    static const uint32_t tables[] = {
        [ANIMATIONS_OF_PARTY_ATTACKS] = 0xCD64B0u, [ANIMATIONS_OF_ENEMY_ATTACKS] = 0xCD666Eu,
        [ANIMATIONS_OF_PARTY_TECHS] = 0xCD656Eu,   [ANIMATIONS_OF_ENEMY_TECHS] = 0xCD666Eu,
        [ANIMATIONS_OF_ITEMS] = 0xCD656Eu,
    };
    return keeper_word(g, tables[keeper], records);
}

/* A script set: which of the sixteen channels run a script, for the set's
 * two halves (a word each, high byte first, the first channel the top bit),
 * then where each running channel's script starts, a word each in the set's
 * bank, the first half's channels' first. The original reads the halves
 * from the set's place in long addressing, a set at the bank's end running on
 * into the next bank. */
enum { SCRIPT_SET_CHANNELS = 16, SCRIPT_SET_HEADER_BYTES = 4 };

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
    return rom_read8(g, 0xCCF63Cu + character) != 0;
}

/* ---- The sixteen one-sprite effects ------------------------------------------------------- */

/* The effect sprite sets 32-47 hold one sprite each, from the effects'
 * one-sprite shapes ($D1:50A2, six bytes each): its tile and attributes. */
enum { ONE_SPRITE_SHAPE_BYTES = 6 };
static inline uint8_t battle_one_sprite_tile(const GameState *g, unsigned shape)
{
    return rom_read8(g, 0xD150A2u + shape * ONE_SPRITE_SHAPE_BYTES);
}
static inline uint8_t battle_one_sprite_attributes(const GameState *g, unsigned shape)
{
    return rom_read8(g, 0xD150A3u + shape * ONE_SPRITE_SHAPE_BYTES);
}

/* ---- The developers' switches -------------------------------------------------------------- */

/* Three bytes of the ROM ($CF:FFFD-$CF:FFFF), all 0 in the released game, by
 * which the developers forced every action's animation: as a critical hit, as
 * its second script set, or as neither. */
typedef enum { ANIMATIONS_AS_THEY_ARE, ANIMATIONS_ALL_CRITICAL, ANIMATIONS_ALL_SECOND, ANIMATIONS_ALL_PLAIN } AnimationSwitch;

static inline AnimationSwitch battle_animation_switch(const GameState *g)
{
    if (rom_read8(g, 0xCFFFFFu))
        return ANIMATIONS_ALL_CRITICAL;
    if (rom_read8(g, 0xCFFFFEu))
        return ANIMATIONS_ALL_SECOND;
    if (rom_read8(g, 0xCFFFFDu))
        return ANIMATIONS_ALL_PLAIN;
    return ANIMATIONS_AS_THEY_ARE;
}

/* ---- A party member's frames ---------------------------------------------------------------- */

/* The frame a character shows falling to the ground ($D1:5F42), by facing. */
enum { BATTLE_FACINGS = 4 };
static inline uint8_t battle_character_falling_frame(const GameState *g, uint8_t character, uint8_t facing)
{
    return rom_read8(g, 0xD15F42u + (uint8_t)(character * BATTLE_FACINGS + facing));
}

#endif
