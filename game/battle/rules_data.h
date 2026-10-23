/* Battle: the rules the battle reads from the ROM -- the enemies' scripts,
 * how long each of their actions is, and the random numbers.
 *
 * The scripts are bank CC's (battle/enemy_ai.h says how they read), a place
 * in them a 16-bit offset in the bank. */
#ifndef CT_GAME_BATTLE_RULES_DATA_H
#define CT_GAME_BATTLE_RULES_DATA_H

#include <stdint.h>

#include "rom.h"
#include "state.h"

/* Whether the battle's debugging path at the end of a pass is on: the bank's
 * last byte ($C1:FFFF), zero in the released game. */
static inline uint8_t battle_debugging_switch(const GameState *g) { return rom_read8(g, 0xC1FFFFu); }

/* The table of 256 random numbers the battle draws from ($FD:BA61). */
static inline uint8_t battle_random_number(const GameState *g, uint8_t at) { return rom_read8(g, 0xFDBA61u + at); }

/* Where each combatant's stat block and each party member's action record are
 * in work RAM, by battle slot ($FD:A80B and $FD:A849, a word each): the
 * original reaches them through these places, and leaves them in its cells. */
static inline uint16_t battle_stat_block_place(const GameState *g, uint8_t slot)
{
    return rom_read16(g, 0xFDA80Bu + (uint16_t)(slot * 2));
}
static inline uint16_t battle_member_record_place(const GameState *g, uint8_t member)
{
    return rom_read16(g, 0xFDA849u + (uint16_t)(member * 2));
}
/* Where each party member's tech records are in work RAM ($FD:A84F, a word a
 * member), which the setup leaves in its cells as it builds them. */
static inline uint16_t battle_member_tech_records_place(const GameState *g, uint8_t member)
{
    return rom_read16(g, 0xFDA84Fu + (uint16_t)(member * 2));
}

/* A character's single techs, from its first: the next character's first
 * ends them ($FD:A89D, a byte a character). */
static inline uint8_t character_first_single_tech(const GameState *g, uint8_t character)
{
    return rom_read8(g, 0xFDA89Du + character);
}

/* The word at a byte offset into the stat blocks' places, wherever it lands:
 * for the one selector that reads the table at a leftover offset
 * (battle/targets.c). */
static inline uint16_t battle_stat_block_place_stray(const GameState *g, uint16_t offset)
{
    return rom_read16(g, 0xFDA80Bu + offset);
}

/* ---- The enemies' scripts ---------------------------------------------------------------- */

typedef uint16_t ScriptPlace;

/* Where an enemy's script starts, by the enemy's id ($CC:8B08, a word an
 * enemy). */
static inline ScriptPlace enemy_script_start(const GameState *g, uint8_t enemy_id)
{
    return rom_read16(g, 0xCC8B08u + (uint16_t)(enemy_id * 2));
}

static inline uint8_t enemy_script_byte(const GameState *g, ScriptPlace at) { return rom_read8(g, 0xCC0000u | at); }

/* Two scripts no enemy has, which a status puts in place of an enemy's own:
 * attacking the nearest party member, and attacking any combatant at random
 * ($CC:8D08, $CC:8D1E). */
enum { SCRIPT_ATTACK_NEAREST = 0x8D08, SCRIPT_ATTACK_ANYONE = 0x8D1E };

/* How many bytes an action takes in a script, by the action ($FD:BA4A). */
static inline uint8_t enemy_action_length(const GameState *g, uint8_t action) { return rom_read8(g, 0xFDBA4Au + action); }

/* The original's routine for each selector, by number ($C1:B8BB, a word each
 * in bank C1): its time is the selector's own, which the port names. */
static inline uint32_t selector_routine(const GameState *g, uint8_t selector)
{
    return 0xC10000u | rom_read16(g, 0xC1B8BBu + (uint16_t)(selector * 2));
}

/* Where the scripts are, for the guards: the first enemy's script to the
 * table that follows the last ($CC:CBC9). */
enum { ENEMY_SCRIPTS_START = 0x8D08, ENEMY_SCRIPTS_END = 0xCBC9 };

#endif
