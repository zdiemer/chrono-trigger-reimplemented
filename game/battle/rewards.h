/* Battle: the rewards of a won battle -- the experience shared out and the
 * levels it brings, the tech points and the techs they teach, and the items
 * the party chose but didn't use put back (battle/inventory.h) -- battle/
 * rewards_levels.c says how the pieces fit together.
 *
 * The main loop gives them as the battle is won (battle/main_loop.c): the
 * experience turned to gold for a party that wears the accessory that does
 * that, then the gold, then the experience and the techs, the rewards'
 * messages (the original's, which wait for the player), and the items put
 * back. A party that runs away puts its items back too. */
#ifndef CT_GAME_BATTLE_REWARDS_H
#define CT_GAME_BATTLE_REWARDS_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

/* What the rewards were, for their messages (battle_reward_flags): the
 * experience shared out, the gold and the items given, tech points counted
 * towards a tech. */
enum { REWARD_EXPERIENCE = 0x80, REWARD_GOLD = 0x40, REWARD_ITEMS = 0x20, REWARD_TECH_POINTS = 0x10 };

/* The member the rewards note a level for when it is one of the characters
 * in reserve (battle_rewards_member). */
enum { REWARDS_RESERVE = 3 };

/* Whose record a level-up grows: a party member's stat block, or the record of
 * a character in reserve. Both hold the level, the experience and the stats
 * at the same places (battle/combatant.h's offsets). */
typedef struct {
    bool in_reserve;
    uint8_t who; /* the member, or the character */
} Grower;

static inline Grower grower_member(uint8_t member) { return (Grower){ false, member }; }
static inline Grower grower_in_reserve(uint8_t character) { return (Grower){ true, character }; }

/* $FD:B201: the experience turned to gold, where a member wears the accessory
 * that does it. */
void battle_turn_experience_to_gold(GameState *g);

/* $C1:F93E: the experience shared out to the members still fighting, each
 * the whole of it, with the levels it brings; the techs the tech points
 * teach; then a share for the characters in reserve. */
void battle_share_experience(GameState *g);

/* The pieces (each an entry of its own too, battle/entries.h). */

/* battle/rewards_levels.c */
/* `pot` is the experience left to share out, which a level-up pays from. */
void rewards_gain_experience(GameState *g, Grower who, uint16_t amount);
void rewards_level_up(GameState *g, Grower who, uint8_t member, uint16_t *pot);
bool rewards_reach_level(GameState *g, Grower who, uint8_t member, uint16_t *pot);
void rewards_grow_hp_and_mp(GameState *g, Grower who);
void rewards_share_with_reserve(GameState *g);

/* battle/rewards_techs.c */
void rewards_learn_techs(GameState *g);
void rewards_sort_combination_members(GameState *g);
/* `at` is where in the messages' list of single techs the next is noted. */
void rewards_count_tech_points(GameState *g, uint8_t member, uint8_t character, uint8_t *at);
bool rewards_reach_next_tech(GameState *g, uint8_t member, uint8_t character, uint16_t points);
void rewards_learn_single_tech(GameState *g, uint8_t member, uint8_t character, uint8_t first, uint8_t *at);
typedef struct {
    uint8_t record;      /* its number among the combination records, which is its learned byte's */
    uint8_t characters;  /* its characters, a bit each */
    uint8_t first, count; /* its techs */
    uint16_t asks;       /* where in bank CC its record says what each tech asks is */
} Combination;
void rewards_learn_combination_techs(GameState *g, Combination c, uint8_t member);

#endif
