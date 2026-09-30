/* Battle: what a won battle's rewards read from the ROM -- how a character
 * grows as it gains levels, and which techs it learns and when.
 *
 * Every read of cartridge data the rewards make (battle/rewards_*.c) goes
 * through here, by what the data is (the asset seam, docs/PLAN.md Phase 3).
 * Each takes the index the game keeps and does the original's index
 * arithmetic, a byte's wraparound included. */
#ifndef CT_GAME_BATTLE_REWARDS_DATA_H
#define CT_GAME_BATTLE_REWARDS_DATA_H

#include <stdint.h>

#include "rom.h"
#include "state.h"

/* ---- Levels ------------------------------------------------------------------------------ */

/* The experience from a level to the next, by the level reached ($CC:2632, a
 * word a level). */
static inline uint16_t experience_for_next_level(const GameState *g, uint8_t level)
{
    return rom_read16(g, 0xCC2632u + (uint8_t)(level * 2));
}

/* A character's stat growth: seven rates, one for each of the stats a level
 * grows, a stat growing by its rate times the levels past the first, in
 * hundredths ($CC:25FA). The original finds the character's by a byte's
 * product; where the rates are is what it leaves behind. */
enum { STAT_GROWTH_RATES = 7 };

static inline uint16_t stat_growth_rates_place(uint8_t character)
{
    return (uint16_t)(0x25FAu + (uint8_t)(character * STAT_GROWTH_RATES));
}
static inline uint8_t stat_growth_rate(const GameState *g, uint8_t character, unsigned stat)
{
    return rom_read8(g, 0xCC0000u + stat_growth_rates_place(character) + stat);
}

/* A character's HP and MP growth, four steps a character: up to a level, the
 * HP (MP) each level adds ($CC:258A and $CC:25C2, two bytes a step, eight a
 * character). The original finds a step by a byte's sum, which wraps. */
typedef struct {
    uint8_t up_to_level, gain;
} GrowthStep;

enum { GROWTH_STEP_BYTES = 2, GROWTH_STEPS_BYTES = 8 };

static inline uint8_t growth_step_place(uint8_t character, uint8_t step)
{
    return (uint8_t)(character * GROWTH_STEPS_BYTES + step * GROWTH_STEP_BYTES);
}
static inline GrowthStep hp_growth_step(const GameState *g, uint8_t character, uint8_t step)
{
    uint8_t place = growth_step_place(character, step);
    return (GrowthStep){ rom_read8(g, 0xCC258Au + place), rom_read8(g, 0xCC258Bu + place) };
}
static inline GrowthStep mp_growth_step(const GameState *g, uint8_t character, uint8_t step)
{
    uint8_t place = growth_step_place(character, step);
    return (GrowthStep){ rom_read8(g, 0xCC25C2u + place), rom_read8(g, 0xCC25C3u + place) };
}

/* ---- Techs ------------------------------------------------------------------------------- */

/* A character's single techs as the tech points teach them: its first, and
 * how many it has ($CC:276A, two bytes a character). */
typedef struct {
    uint8_t first, count;
} CharacterTechs;

static inline uint16_t character_techs_place(uint8_t character) { return (uint16_t)(0x276Au + (uint8_t)(character * 2)); }
static inline CharacterTechs character_techs(const GameState *g, uint8_t character)
{
    uint16_t place = character_techs_place(character);
    return (CharacterTechs){ rom_read8(g, 0xCC0000u + place), rom_read8(g, 0xCC0001u + place) };
}

/* The tech points each of a character's single techs takes after the one
 * before, by how many it knows ($CC:26FA, sixteen bytes a character). */
enum { TECH_COSTS_BYTES = 16 };
static inline uint16_t tech_points_for_next(const GameState *g, uint8_t character, uint8_t known)
{
    return rom_read16(g, 0xCC26FAu + (uint16_t)(character * TECH_COSTS_BYTES + (uint8_t)(known * 2)));
}

/* The combination techs, a record for each pair or trio of characters who
 * have some: the characters, a bit each from Crono's at the top; the first
 * tech and how many, in order ($FF for the first where it has none); and
 * where in bank CC the single techs each asks of them are, a byte for each
 * character in order, three a tech ($CC:2778, five bytes a record, $FF for the
 * characters past the last). A record's number among them is its learned
 * byte's. */
enum { COMBINATION_RECORD_BYTES = 5, COMBINATION_TECH_ASKS = 3, NO_MORE_COMBINATIONS = 0xFF, COMBINATION_HAS_NO_TECHS = 0xFF };

typedef struct {
    uint8_t characters, first, count;
    uint16_t asks;
} CombinationRecord;

static inline uint16_t combination_record_place(uint8_t record)
{
    return (uint16_t)(0x2778u + record * COMBINATION_RECORD_BYTES);
}
static inline uint8_t combination_record_characters(const GameState *g, uint8_t record)
{
    return rom_read8(g, 0xCC0000u + combination_record_place(record));
}
static inline CombinationRecord combination_record(const GameState *g, uint8_t record)
{
    uint16_t place = combination_record_place(record);
    return (CombinationRecord){ rom_read8(g, 0xCC0000u + place), rom_read8(g, 0xCC0001u + place),
                                rom_read8(g, 0xCC0002u + place), rom_read16(g, 0xCC0003u + place) };
}
/* Where the asks of a combination's tech are, from where its record says
 * the first's are. */
static inline uint16_t combination_tech_asks_place(uint16_t asks, uint8_t tech)
{
    return (uint16_t)(asks + tech * COMBINATION_TECH_ASKS);
}
/* What a combination's tech asks of the character at `position` in its
 * list. */
static inline uint8_t combination_tech_asks(const GameState *g, uint16_t asks, uint8_t tech, uint8_t position)
{
    return rom_read8(g, 0xCC0000u + (uint16_t)(combination_tech_asks_place(asks, tech) + position));
}

#endif
