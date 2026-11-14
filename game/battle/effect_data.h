/* Battle: the records an action's effect is worked out from, as the ROM
 * holds them, decoded.
 *
 * An enemy's attack and each of its techs is a record of eleven bytes: how
 * the action is marked for those it hits, the effect it has, and the modifier
 * applied to what the effect works out. The effect is a record of twelve
 * bytes: its kind (heal, cure, inflict a status, hit, ...) and the numbers the
 * kind takes, which say different things for different kinds. A modifier is
 * a routine and two numbers (battle/effect_modifiers.c decodes them). */
#ifndef CT_GAME_BATTLE_EFFECT_DATA_H
#define CT_GAME_BATTLE_EFFECT_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "rom.h"
#include "state.h"

/* ---- An enemy's attack or tech ------------------------------------------------------------ */

enum { ACTION_RECORD_BYTES = 11 };

/* The record's first byte: the action stops at the first target its effect
 * misses; it can't miss (with bit 7 as well, only its first effect's test
 * stands); and in the low three bits, how many hits' numbers are added up
 * into the first (the hits after the first). */
enum { ACTION_STOPS_AT_MISS = 0x80, ACTION_SURE = 0x40, ACTION_HITS_ADDED = 0x07 };

typedef struct {
    uint8_t flags;
    uint8_t marks;     /* how those it hits have it recorded as acting on them */
    uint8_t elements;  /* its elements, which its targets record too */
    uint8_t effect;    /* the effect's number */
    uint8_t modifier;  /* the modifier's number */
} ActionRecord;

typedef enum { ENEMY_ATTACKS, ENEMY_TECHS } ActionTable;

/* ---- An effect ----------------------------------------------------------------------------- */

enum { EFFECT_RECORD_BYTES = 12, NO_EFFECT = 0xFF };

/* The kinds of effect, by the record's first byte; 4 and 9-$28 do nothing,
 * and none is past $2C. */
typedef enum {
    EFFECT_RESTORE = 0x00,             /* restore HP or MP by the actor's magic */
    EFFECT_CURE = 0x01,                /* cure statuses, perhaps revive, perhaps heal */
    EFFECT_INFLICT = 0x02,             /* a status, where it lands */
    EFFECT_HIT = 0x03,                 /* a hit, and its chances of a status and a steal */
    EFFECT_DRAIN = 0x05,               /* HP or MP drained to the actor */
    EFFECT_STEAL = 0x06,               /* an item stolen */
    EFFECT_RESTORE_SHOWING_HP = 0x07,  /* restore, and show the actor's HP */
    EFFECT_HIT_REPEATEDLY = 0x08,      /* hits, several of them */
    EFFECT_ITEM_RESTORE = 0x29,        /* an item's restoring */
    EFFECT_ITEM_CURE = 0x2A,           /* an item's cure */
    EFFECT_ITEM_STATUS = 0x2B,         /* an item's status */
    EFFECT_LOSE_CONDITION_BIT = 0x2C,  /* the target's condition loses its bit 6 */
    EFFECT_KINDS = 0x2D,
} EffectKind;

/* The effect's flags, the first byte: a hit that can't be turned away, one
 * that can't miss, a barrier and a shield passed; a hit's chances of a status
 * and to steal. The second: the attacker's weapon's modifier as well, and the
 * defence scaled by quarters. */
enum {
    EFFECT_STEALS = 0x02, EFFECT_GIVES_STATUS = 0x04, EFFECT_CANT_BE_TURNED_AWAY = 0x10, EFFECT_CANT_MISS = 0x20,
    EFFECT_PASSES_BARRIER = 0x40, EFFECT_PASSES_SHIELD = 0x80,
};
enum { EFFECT_WITH_WEAPON_MODIFIER = 0x80, EFFECT_DEFENCE_QUARTERS = 0x03 };

/* What a cure does: bit 7 revives, bit 6 heals as well; a healing's multiple
 * is the low five bits. What a drain takes: bit 7 HP, bit 6 MP. */
enum { CURE_REVIVES = 0x80, CURE_HEALS = 0x40, MULTIPLE_BITS = 0x1F, DRAINS_HP = 0x80, DRAINS_MP = 0x40 };

typedef struct {
    uint8_t kind;
    union {                                               /* the second and third bytes */
        struct { uint8_t multiple, restores; } restore;   /* restore */
        struct { uint8_t how, restores; } cure;          /* cure: how, and a healing's restores */
        struct { uint8_t status_byte, statuses; } inflict;/* inflict and an item's status */
        struct { uint8_t chance, statuses; } hit;         /* a hit's chance (a percent) of statuses of the first byte */
        struct { uint8_t what, share; } drain;            /* drain: what, and the twentieths of its most */
        struct { uint8_t hits; } repeated;                /* hits repeatedly (and each hit's chance, as a hit's) */
        struct { uint8_t chance; } steal;                 /* a steal's chance, a percent */
    };
    uint8_t aim_stat, evasion_stat;                       /* a hit's test of landing */
    union {
        struct { uint8_t formula, defence_stat; } damage; /* a hit's damage */
        struct { uint8_t base_chance, magic_divisor; } landing; /* a status's chance */
    };
    uint8_t flags, more_flags;
    uint8_t power;
    uint8_t kinds_turned_away;                            /* the target kinds (STAT_KIND) that turn a hit away */
    uint8_t misses;                                       /* bit 7 the attacker's miss shows, bit 6 the dodge */
} EffectRecord;

/* ---- The party's techs ---------------------------------------------------------------------- */

/* A party tech is a record of eleven bytes ($CC:1BEB): which members perform
 * it, its flags, how it marks those it acts on and its elements, and up to
 * three parts, each an effect of the party's table ($CC:213F, records as the
 * enemies' are) with a modifier. A single tech has one part, a combination
 * tech one for each member. */
enum { PARTY_TECH_BYTES = 11, PARTY_TECH_PARTS = 3, NO_PART = 0x80 };

/* The flags: its effects can't miss, nor be turned away (EFFECT_CANT_MISS and
 * EFFECT_CANT_BE_TURNED_AWAY, a place lower), and the hits' numbers added
 * up as an enemy's action adds them (ACTION_HITS_ADDED). */
enum { TECH_CANT_MISS = 0x40, TECH_CANT_BE_TURNED_AWAY = 0x20 };

typedef struct {
    uint8_t members;                     /* the members' record ($CC:249F), the low seven bits */
    uint8_t flags;
    uint8_t marks;                       /* how those it acts on have it recorded as acting on them */
    uint8_t elements;
    uint8_t effects[PARTY_TECH_PARTS];   /* each part's effect; NO_PART set, no part */
    uint8_t modifiers[PARTY_TECH_PARTS];
} PartyTech;

/* $CC:249F: the characters who perform a tech, three bytes a record ($FF past
 * them). */
enum { TECH_MEMBERS = 3, NO_MEMBER = 0xFF };

/* $CC:1ACB: two bytes a tech for its record in the member's tech list: a
 * byte the menu copies (not worked out), and its flags' low six bits. */
typedef struct {
    uint8_t detail, flags;
} TechMenuBytes;

/* ---- What an enemy has to steal --------------------------------------------------------------- */

/* $CC:5E00: an enemy's rewards, seven bytes an enemy: of them, the item it
 * drops (the one a steal takes) and its charm (the rare one a steal may take
 * instead). */
typedef struct {
    uint8_t dropped, charm;
} EnemyItems;

enum { ENEMY_REWARD_BYTES = 7 };

/* The whole of a beaten enemy's rewards, at the record's place among them
 * (the enemy's id times its bytes): the experience and the gold, a word
 * each, its items, and the tech points it gives. */
typedef struct {
    uint16_t experience, gold;
    EnemyItems items;
    uint8_t tech_points;
} EnemySpoils;

/* ---- Modifiers and items ----------------------------------------------------------------- */

/* $CC:2A05: a modifier is its routine's number and its two numbers. */
typedef struct {
    uint8_t routine, first, second;
} ModifierRecord;

/* The same record as armor and helmets read it at the fight's start: a byte
 * of the member's stat block, and the bits the piece sets in it. The start
 * finds the record by its number times three in a byte, so the modifiers from
 * 86 on read earlier ones'. */
typedef struct {
    uint8_t at, bits;
} ArmorModifier;

/* $CC:05CD: an item's healing, four bytes an item: what it restores or cures,
 * how much (a multiple, or $0F for all), and the power of it. The original
 * finds the item's four bytes by its number times four in a byte, so the
 * items from 64 on read the first ones' again. */
enum { ITEM_HEALS, ITEM_AMOUNT, ITEM_POWER };

/* $CC:05CC: the byte before an item's healing, which says what using it in
 * battle does: restore, cure, or give a status. Its place in the bank is found
 * as the healing's is, by the item's number times four in a byte (so the
 * items from 64 on read the first ones' again). */
enum { ITEM_USE_RESTORES = 0x80, ITEM_USE_GIVES_STATUS = 0x40, ITEM_USE_CURES = 0x04 };

/* $CC:0ABC and $CC:1BB5: the items from $BC on have a record of three bytes,
 * whose first byte's bit 7 says a party member who steals one keeps it apart
 * from the inventory, and a byte of another table the kept one takes along. */
enum { FIRST_RECORDED_ITEM = 0xBC, ITEM_RECORD_BYTES = 3, ITEM_KEPT_APART = 0x80 };

/* $CC:88CA and $CC:6FCA, a record an attack or a tech by number. */
static inline ActionRecord action_record(const GameState *g, ActionTable table, uint8_t number)
{
    AssetId records = table == ENEMY_ATTACKS ? ASSET_ENEMY_ATTACK_RECORDS : ASSET_ENEMY_TECH_RECORDS;
    uint32_t at = (uint32_t)(number * ACTION_RECORD_BYTES);
    return (ActionRecord){ asset_read8(g, records, at), asset_read8(g, records, at + 1), asset_read8(g, records, at + 2),
                           asset_read8(g, records, at + 4), asset_read8(g, records, at + 7) };
}

/* The record decoded for the kind it is worked out as (which is its first
 * byte, where the work reads it there). */
static inline EffectRecord effect_record_decode(const uint8_t b[EFFECT_RECORD_BYTES], uint8_t kind)
{
    EffectRecord e = { .kind = b[0], .aim_stat = b[3], .evasion_stat = b[4], .flags = b[7], .more_flags = b[8],
                       .power = b[9], .kinds_turned_away = b[10], .misses = b[11] };
    e.damage.formula = b[5];
    e.damage.defence_stat = b[6];
    switch (kind) {
    case EFFECT_RESTORE:
    case EFFECT_RESTORE_SHOWING_HP: e.restore.multiple = (uint8_t)(b[1] & MULTIPLE_BITS); e.restore.restores = b[2]; break;
    case EFFECT_CURE: e.cure.how = b[1]; e.cure.restores = b[2]; break;
    case EFFECT_DRAIN: e.drain.what = b[1]; e.drain.share = b[2]; break;
    case EFFECT_INFLICT: e.inflict.status_byte = b[1]; e.inflict.statuses = b[2]; break;
    case EFFECT_STEAL: e.steal.chance = b[1]; break;
    default: e.hit.chance = b[1]; e.hit.statuses = b[2]; break; /* a hit, and the hits repeated */
    }
    return e;
}

/* $CC:89C6 and $CC:7AC9, an effect by number, for the attacks and the
 * techs. */
static inline uint8_t effect_record_byte(const GameState *g, ActionTable table, uint8_t effect, unsigned n)
{
    AssetId effects = table == ENEMY_ATTACKS ? ASSET_ENEMY_ATTACK_EFFECTS : ASSET_ENEMY_TECH_EFFECTS;
    return asset_read8(g, effects, (uint32_t)(effect * EFFECT_RECORD_BYTES) + n);
}

/* $CC:1BEB, a tech of the party's by number. */
static inline PartyTech party_tech(const GameState *g, uint8_t tech)
{
    uint32_t at = (uint32_t)(tech * PARTY_TECH_BYTES);
    PartyTech t = { (uint8_t)(asset_read8(g, ASSET_PARTY_TECHS, at) & 0x7F), asset_read8(g, ASSET_PARTY_TECHS, at + 1),
                    asset_read8(g, ASSET_PARTY_TECHS, at + 2), asset_read8(g, ASSET_PARTY_TECHS, at + 3), { 0 }, { 0 } };
    for (unsigned part = 0; part < PARTY_TECH_PARTS; part++) {
        t.effects[part] = asset_read8(g, ASSET_PARTY_TECHS, at + 5 + part);
        t.modifiers[part] = asset_read8(g, ASSET_PARTY_TECHS, at + 8 + part);
    }
    return t;
}

/* A tech whose record has its top bit set is locked: the tech points don't
 * teach it until the character's techs are unlocked (battle/rewards_techs.c),
 * nor a combination's its characters. */
static inline bool party_tech_locked(const GameState *g, uint8_t tech)
{
    return (asset_read8(g, ASSET_PARTY_TECHS, (uint32_t)(tech * PARTY_TECH_BYTES)) & 0x80) != 0;
}

static inline uint8_t tech_member_at(const GameState *g, uint16_t place, unsigned part)
{
    return asset_read8(g, ASSET_TECH_MEMBER_AT, place + part);
}

static inline uint8_t tech_member(const GameState *g, uint8_t members, unsigned part)
{
    return tech_member_at(g, (uint16_t)(members * TECH_MEMBERS), part);
}

static inline TechMenuBytes tech_menu_bytes(const GameState *g, uint8_t tech)
{
    uint32_t at = (uint32_t)(tech * 2);
    return (TechMenuBytes){ asset_read8(g, ASSET_PARTY_TECH_AIM, at),
                            (uint8_t)(asset_read8(g, ASSET_PARTY_TECH_AIM, at + 1) & 0x3F) };
}

/* $CC:213F: a party effect's record, twelve bytes as the enemies' are: as the
 * work copies it, and decoded for the kind it is. */
static inline void party_effect_bytes(const GameState *g, uint8_t effect, uint8_t out[EFFECT_RECORD_BYTES])
{
    for (unsigned n = 0; n < EFFECT_RECORD_BYTES; n++)
        out[n] = asset_read8(g, ASSET_PARTY_EFFECT_BYTES, (uint32_t)(effect * EFFECT_RECORD_BYTES) + n);
}

static inline EffectRecord party_effect_record(const GameState *g, uint8_t effect)
{
    uint8_t bytes[EFFECT_RECORD_BYTES];
    party_effect_bytes(g, effect, bytes);
    return effect_record_decode(bytes, bytes[0]);
}

/* $CC:2583: each character's attack, as the party tech it is worked out as. */
static inline uint8_t character_attack_tech(const GameState *g, uint8_t character)
{
    return asset_read8(g, ASSET_CHARACTER_ATTACK_TECH, character);
}

/* $CC:2BDC and $CC:2BEB: how much longer each member of a tech waits for its
 * next turn after it, in tenths of its turn wait: the first two members'
 * nibbles of the first table's byte, the third's the low nibble of the
 * second's. */
static inline uint8_t tech_wait_tenths(const GameState *g, uint8_t tech, unsigned part)
{
    uint8_t first = asset_read8(g, ASSET_TECH_WAIT_TENTHS, tech),
            third = asset_read8(g, ASSET_TECH_WAIT_TENTHS_2BEB, tech);
    return part == 0 ? (uint8_t)(first >> 4) : part == 1 ? (uint8_t)(first & 0x0F) : (uint8_t)(third & 0x0F);
}

/* $CC:253B: what a part's effect costs its member in MP. */
static inline uint8_t party_effect_mp_cost(const GameState *g, uint8_t effect)
{
    return asset_read8(g, ASSET_PARTY_EFFECT_MP_COST, (uint8_t)(effect & 0x7F));
}

static inline EnemyItems enemy_items(const GameState *g, uint8_t enemy)
{
    uint32_t at = (uint32_t)(enemy * ENEMY_REWARD_BYTES);
    return (EnemyItems){ asset_read8(g, ASSET_ENEMY_ITEMS, at + 4), asset_read8(g, ASSET_ENEMY_ITEMS, at + 5) };
}

static inline EnemySpoils enemy_spoils(const GameState *g, uint16_t place)
{
    uint32_t at = place;
    return (EnemySpoils){ asset_read16(g, ASSET_ENEMY_ITEMS, at),
                          asset_read16(g, ASSET_ENEMY_ITEMS, at + 2),
                          { asset_read8(g, ASSET_ENEMY_ITEMS, at + 4), asset_read8(g, ASSET_ENEMY_ITEMS, at + 5) },
                          asset_read8(g, ASSET_ENEMY_ITEMS, at + 6) };
}

static inline ModifierRecord modifier_record(const GameState *g, uint8_t modifier)
{
    uint32_t at = (uint32_t)(modifier * 3);
    return (ModifierRecord){ asset_read8(g, ASSET_MODIFIER_RECORD, at), asset_read8(g, ASSET_MODIFIER_RECORD, at + 1),
                             asset_read8(g, ASSET_MODIFIER_RECORD, at + 2) };
}

static inline ArmorModifier armor_modifier(const GameState *g, uint8_t modifier)
{
    uint32_t at = (uint8_t)(modifier * 3);
    return (ArmorModifier){ asset_read8(g, ASSET_MODIFIER_RECORD, at), asset_read8(g, ASSET_MODIFIER_RECORD, at + 1) };
}

/* The other items' records ($CC:05CC, four bytes an item): the use's byte,
 * then the healing's. An item's record is found by its number times four in
 * a byte (so the items from 64 on read the first ones' again). */
enum { ITEM_HEALING = 1 };

static inline uint8_t item_healing_byte(const GameState *g, uint8_t item, unsigned n)
{
    return asset_read8(g, ASSET_OTHER_ITEM_RECORDS, ITEM_HEALING + (uint8_t)(item << 2) + n);
}

/* Where an item's use is in its bank, which the original leaves in a cell. */
static inline uint16_t item_use_place(uint8_t item)
{
    return (uint16_t)((uint16_t)asset_address(ASSET_OTHER_ITEM_RECORDS) + (uint8_t)(item << 2));
}

static inline uint8_t item_use(const GameState *g, uint8_t item)
{
    return asset_read8(g, ASSET_OTHER_ITEM_RECORDS, (uint8_t)(item << 2));
}

static inline uint8_t item_record_flags(const GameState *g, uint8_t item)
{
    return asset_read8(g, ASSET_ITEM_RECORD_FLAGS,
                       (uint32_t)((uint8_t)(item - FIRST_RECORDED_ITEM) * ITEM_RECORD_BYTES));
}

static inline uint8_t item_record_detail(const GameState *g, uint8_t item)
{
    return asset_read8(g, ASSET_ITEM_AIM_MODE, (uint8_t)(item - FIRST_RECORDED_ITEM));
}

#endif
