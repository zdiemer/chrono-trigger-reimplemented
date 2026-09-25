/* Battle: the records an action's effect is worked out from, as the ROM
 * holds them.
 *
 * An enemy's attack and each of its techs is a record of eleven bytes: how
 * the action is marked for those it hits, the effect it has, and the modifier
 * applied to what the effect works out. The effect is a record of twelve
 * bytes: its kind (heal, cure, inflict a status, hit, ...) and the numbers the
 * kind takes, which say different things for different kinds. A modifier is
 * a routine and two numbers. */
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

/* $CC:2A05: a modifier is the routine's number and its two numbers. */
typedef struct {
    uint8_t routine, first, second;
} ModifierRecord;

static inline ModifierRecord modifier_record(const GameState *g, uint8_t modifier)
{
    uint32_t at = 0xCC2A05u + (uint32_t)(modifier * 3);
    return (ModifierRecord){ rom_read8(g, at), rom_read8(g, at + 1), rom_read8(g, at + 2) };
}

/* $CC:5E04 and $CC:5E05: the item an enemy can have stolen from it, the
 * common one and the rare one, in its record of seven bytes (by the id it is
 * fighting as, times seven: an offset the caller works out). */
static inline uint8_t enemy_common_item(const GameState *g, uint16_t record) { return rom_read8(g, 0xCC5E04u + record); }
static inline uint8_t enemy_rare_item(const GameState *g, uint16_t record) { return rom_read8(g, 0xCC5E05u + record); }

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
