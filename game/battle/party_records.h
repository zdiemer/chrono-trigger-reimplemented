/* Battle: the party's records as a fight starts them -- each member's stats
 * with its equipment (battle/member_stats.c) and its tech records
 * (battle/tech_records.c) -- for the fight's start (battle/fight_start.c) to
 * call as C. */
#ifndef CT_GAME_BATTLE_PARTY_RECORDS_H
#define CT_GAME_BATTLE_PARTY_RECORDS_H

#include <stdint.h>

#include "state.h"

/* $C1:CE3A: the member's stats as it fights, with its weapon's, armor's and
 * helmet's boosts and affinities. */
void battle_equip_member(GameState *g, uint8_t member);

/* $C1:CF15: for each member, `bytes` of the record of the piece its block
 * names at `worn_at`, copied into its block at `copy_to`. */
void battle_take_equipment_records(GameState *g, uint8_t worn_at, uint8_t copy_to, uint8_t bytes);

/* $FD:B14D: a stat boost added to the member's stats. */
void battle_apply_boost(GameState *g, uint8_t member, uint8_t which);

/* $FD:AD09: every member's tech records blank. */
void battle_blank_tech_records(GameState *g);

/* $C1:C96A, $C1:CA1A, $C1:CCCB: a member's single techs, its double techs
 * with a partner in the fight, and its triple tech. */
void battle_set_up_single_techs(GameState *g, uint8_t member);
void battle_set_up_double_techs(GameState *g, uint8_t member);
void battle_find_triple_tech(GameState *g, uint8_t member);

#endif
