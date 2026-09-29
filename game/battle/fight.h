/* Battle: the fight's start, the combatants made ready before the first turn
 * (battle/fight_start.c says how the pieces fit together).
 *
 * The screen and the objects are set up before this (battle/setup.h); what
 * is set up here is the fight itself: who is in it, every combatant's stat
 * block, the party's techs and items, the timers, and the state the turns
 * and the rewards work in, all cleared. */
#ifndef CT_GAME_BATTLE_FIGHT_H
#define CT_GAME_BATTLE_FIGHT_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

/* $C1:FA8B: the whole of it, in order. */
void battle_start_fight(GameState *g);

/* ---- The pieces (each an entry of its own too, battle/entries.h) --------------------------- */

/* battle/fight_enemies.c */
void fight_list_combatants(GameState *g);
void fight_load_enemy(GameState *g, uint8_t enemy);
void fight_clear_enemy_block(GameState *g, uint8_t enemy);
void fight_note_enemy_families(GameState *g);
void fight_mark_what_enemies_carry(GameState *g);

/* battle/fight_party.c */
void fight_load_demo_party(GameState *g);
void fight_start_demo_characters(GameState *g);
void fight_take_member_record(GameState *g, uint8_t member);
uint8_t fight_aylas_fists(GameState *g, uint8_t level);
bool fight_member_is_ayla(const GameState *g, uint8_t member);
void fight_list_party(GameState *g);
void fight_set_up_affinities(GameState *g, uint8_t member);
void fight_note_accessories(GameState *g);
void fight_note_weapons(GameState *g);
void fight_add_up_member_stats(GameState *g, uint8_t member);
void fight_apply_accessory_effects(GameState *g);
void fight_raise_critical_rate(GameState *g, uint8_t member);
void fight_apply_member_bonuses(GameState *g, uint8_t member);
void fight_ward_off_confusion(GameState *g);

/* battle/fight_timers.c */
void fight_clear_timers(GameState *g);
void fight_set_timer_lengths(GameState *g);
void fight_set_first_turns(GameState *g);

/* battle/fight_start.c */
void fight_clear_turn_state(GameState *g);
void fight_number_places(GameState *g);

/* battle/tech_records.c */
void battle_blank_tech_records(GameState *g);
void battle_set_up_single_techs(GameState *g, uint8_t member);
void battle_set_up_double_techs(GameState *g, uint8_t member);
void battle_set_up_triple_tech_of(GameState *g, uint8_t member);

/* battle/member_stats.c and battle/inventory.c, called as C */
void battle_set_up_member_stats_of(GameState *g, uint8_t member);
void battle_apply_boost(GameState *g, uint8_t member, uint8_t which);
void battle_copy_equipment_records_for(GameState *g, uint8_t worn_at, uint8_t copy_to, uint8_t bytes);
void battle_build_item_list_now(GameState *g);

#endif
