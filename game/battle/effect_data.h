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

/* $CC:88CA and $CC:6FCA, a record an attack or a tech by number. */
static inline uint32_t action_record_place(ActionTable table, uint8_t number)
{
    return (table == ENEMY_ATTACKS ? 0xCC88CAu : 0xCC6FCAu) + (uint32_t)(number * ACTION_RECORD_BYTES);
}

static inline ActionRecord action_record(const GameState *g, ActionTable table, uint8_t number)
{
    uint32_t at = action_record_place(table, number);
    return (ActionRecord){ rom_read8(g, at), rom_read8(g, at + 1), rom_read8(g, at + 2), rom_read8(g, at + 4),
                           rom_read8(g, at + 7) };
}

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
    default: e.hit.chance = b[1]; e.hit.statuses = b[2]; break; /* a hit, and the hits repeated */
    }
    return e;
}

/* $CC:89C6 and $CC:7AC9, an effect by number, for the attacks and the
 * techs. */
static inline uint32_t effect_record_place(ActionTable table, uint8_t effect)
{
    return (table == ENEMY_ATTACKS ? 0xCC89C6u : 0xCC7AC9u) + (uint32_t)(effect * EFFECT_RECORD_BYTES);
}

static inline uint8_t effect_record_byte(const GameState *g, ActionTable table, uint8_t effect, unsigned n)
{
    return rom_read8(g, effect_record_place(table, effect) + n);
}

/* ---- Modifiers and items ----------------------------------------------------------------- */

/* $CC:2A05: a modifier is its routine's number and its two numbers. */
typedef struct {
    uint8_t routine, first, second;
} ModifierRecord;

static inline ModifierRecord modifier_record(const GameState *g, uint8_t modifier)
{
    uint32_t at = 0xCC2A05u + (uint32_t)(modifier * 3);
    return (ModifierRecord){ rom_read8(g, at), rom_read8(g, at + 1), rom_read8(g, at + 2) };
}

/* $CC:05CD: an item's healing, four bytes an item: what it restores or cures,
 * how much (a multiple, or $0F for all), and the power of it. The original
 * finds the item's four bytes by its number times four in a byte, so the
 * items from 64 on read the first ones' again. */
enum { ITEM_HEALS, ITEM_AMOUNT, ITEM_POWER };

static inline uint8_t item_healing_byte(const GameState *g, uint8_t item, unsigned n)
{
    return rom_read8(g, 0xCC05CDu + (uint8_t)(item << 2) + n);
}

#endif
