/* Battle: what the rewards' pieces share (battle/rewards.h is what the rest
 * of the battle calls): a grower's record, a character's bit, and where the
 * original keeps what it works on, for what the pieces leave behind for it
 * and for their entries. */
#ifndef CT_GAME_BATTLE_REWARDS_WORK_H
#define CT_GAME_BATTLE_REWARDS_WORK_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/battle.h"
#include "battle/combatant.h"
#include "battle/rewards.h"
#include "battle/rewards_data.h"
#include "ram.h"
#include "state.h"
#include "vars.h"

enum { TOP_LEVEL = 99 };

/* The most HP and MP a level brings them to. */
enum { MOST_MAX_HP = 999, MOST_MAX_MP = 99 };

/* ---- The record a level grows -------------------------------------------------------------- */

static inline uint8_t record_byte(const GameState *g, Grower who, unsigned at)
{
    return who.in_reserve ? character_records_record_byte(g, who.who, at) : battle_stats_block_byte(g, who.who, at);
}
static inline void set_record_byte(GameState *g, Grower who, unsigned at, uint8_t v)
{
    if (who.in_reserve)
        set_character_records_record_byte(g, who.who, at, v);
    else
        set_battle_stats_block_byte(g, who.who, at, v);
}
static inline uint16_t record_word(const GameState *g, Grower who, unsigned at)
{
    return (uint16_t)(record_byte(g, who, at) | record_byte(g, who, at + 1) << 8);
}
static inline void set_record_word(GameState *g, Grower who, unsigned at, uint16_t v)
{
    set_record_byte(g, who, at, (uint8_t)v);
    set_record_byte(g, who, at + 1, (uint8_t)(v >> 8));
}
/* The experience and the tech points are three bytes. */
static inline uint32_t record_long(const GameState *g, Grower who, unsigned at)
{
    return record_word(g, who, at) | (uint32_t)record_byte(g, who, at + 2) << 16;
}
static inline void set_record_long(GameState *g, Grower who, unsigned at, uint32_t v)
{
    set_record_word(g, who, at, (uint16_t)v);
    set_record_byte(g, who, at + 2, (uint8_t)(v >> 16));
}

static inline uint8_t grower_character(const GameState *g, Grower who) { return record_byte(g, who, STAT_ID); }
static inline uint8_t grower_level(const GameState *g, Grower who) { return record_byte(g, who, STAT_LEVEL); }

/* ---- The characters ------------------------------------------------------------------------ */

/* A character's bit where the game keeps a bit a character, Crono's at the
 * top; past the eighth, none. */
static inline uint8_t character_bit(uint8_t character) { return character < 8 ? (uint8_t)(0x80 >> character) : 0; }

/* A party member whose id in the fight has its top bit set is out of it; a
 * character with no slot in the fight has its slot's top bit set. */
enum { OUT_OF_FIGHT = 0x80, NO_SLOT = 0x80 };

/* Joined the party, but not in the fight. */
static inline bool character_in_reserve(const GameState *g, uint8_t character)
{
    return (party_characters_joined(g) & character_bit(character)) && (battle_character_slot(g, character) & NO_SLOT);
}

/* ---- Where the original keeps its work -------------------------------------------------------- */

/* Where a grower's record is in work RAM, as the original keeps it
 * (battle_growth_record_places); and which record is at a place. */
static inline uint16_t grower_place(Grower who)
{
    if (who.in_reserve)
        return (uint16_t)(RAM_CHARACTER_RECORDS + who.who * RAM_CHARACTER_RECORDS_STRIDE);
    return (uint16_t)(RAM_BATTLE_STATS + who.who * RAM_BATTLE_STATS_STRIDE);
}
bool grower_at(uint16_t place, Grower *who);

/* The direct page's cells the pieces work in (battle_low_scratch). */
enum {
    WORK_BLOCK_AT = 0x00,     /* the member's stat block; a combination's learned byte; an HP or MP step's place */
    WORK_KNOWN_AT = 0x02,     /* the character's count of techs known; the combination record; the reserve's character */
    WORK_TECHS_AT = 0x04,     /* the character's techs' record; the combination's asks */
    WORK_CHARACTERS_AT = 0x08, /* a combination record's characters */
    WORK_BIT_AT = 0x0A,       /* a learned bit, a character's bit; the character's costs */
    WORK_SHIFTING_AT = 0x0C,  /* the combination's characters, shifted through */
    WORK_TESTED_AT = 0x0E,    /* the count of techs known tested */
    WORK_LISTED_AT = 0x10,    /* the combination's characters listed, which is tested; the HP or MP step */
    WORK_CHARACTER_AT = 0x12, /* the character looked at */
};

/* A character's record's size, which the original multiplies by to find it. */
enum { CHARACTER_RECORD_BYTES = RAM_CHARACTER_RECORDS_STRIDE };

/* A member's stat block, as an offset from the first, in the cell the tech
 * pieces take it in. */
static inline uint16_t member_block_place(uint8_t member) { return (uint16_t)(member * BATTLE_STAT_BLOCK_BYTES); }

/* Where a character's costs of its single techs start, as an offset into the
 * table (rewards_data.h, tech_points_for_next). */
static inline uint16_t tech_costs_place(uint8_t character) { return (uint16_t)(character * TECH_COSTS_BYTES); }

static inline uint16_t known_place(uint8_t character) { return (uint16_t)(RAM_PARTY_SINGLE_TECHS_KNOWN + character); }
static inline uint16_t combination_learned_place(uint8_t record) { return (uint16_t)(RAM_PARTY_DOUBLE_TECHS_LEARNED + record); }

#endif
