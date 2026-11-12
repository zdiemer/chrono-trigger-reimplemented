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

/* ---- What the rewards' messages show (battle/rewards_messages.c) ---------------------------------- */

/* What a member's notes say it gained: a level (battle_rewards_levels_noted),
 * and single techs, double techs or a triple tech (battle_rewards_techs_noted). */
enum { NOTED_LEVEL_GAINED = 0x80 };
enum { NOTED_SINGLE_TECHS = 0x08, NOTED_DOUBLE_TECHS = 0x04, NOTED_TRIPLE_TECH = 0x02 };

/* A member's single techs have eight places in the messages' list. */
enum { PLACES_A_MEMBER = 8 };

/* No tech taught, in the messages' lists. */
enum { NO_TECH = 0xFF };

/* A party member whose id in the fight has its top bit set is out of it; a
 * character with no slot in the fight has its slot's top bit set. */
enum { OUT_OF_FIGHT = 0x80, NO_SLOT = 0x80 };

bool grower_at(uint16_t place, Grower *who);

/* A character's record's size, which the original multiplies by to find it. */
enum { CHARACTER_RECORD_BYTES = RAM_CHARACTER_RECORDS_STRIDE };

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

#endif
