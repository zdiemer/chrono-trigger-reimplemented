/* Battle mode operations and game-state validation. */
#ifndef CT_GAME_BATTLE_ENTRIES_H
#define CT_GAME_BATTLE_ENTRIES_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
#include "state.h"

bool battle_load_effect_graphics(GameState *g, Platform *pf, uint8_t effect);

/* What the original's routines leave in the carry for its callers, worked out
 * for the bridges that call them: the marking of the first target ($C1:AD09)
 * and of the others ($C1:AD35), from the candidates and the carry it found
 * (battle/targets.c); and a combatant's screen box (battle/objects.c), its
 * last sum's. */
bool battle_first_mark_carry(const GameState *g, bool carry);
/* ... and the area search (battle/area_search.c), for the area rules' shape
 * it searched, the side it searched, and the combatants it was placed on and
 * reached toward. */
bool battle_area_search_carry(uint8_t shape, bool among_party, uint8_t centre, uint8_t toward);
bool battle_other_marks_carry(const GameState *g, bool carry);
bool battle_bounds_carry(const GameState *g, unsigned slot);
/* The enemies' minds: battle/random.c, positions.c, targets.c, enemy_scripts.c,
 * enemy_conditions.c, enemy_actions.c, enemy_turn.c, and main_loop.c's
 * beaten enemies. */
/* battle/enemy_turn.c */
/* An enemy's turn ($C1:8CF9), as C. */
bool battle_enemy_takes_turn(GameState *g, Platform *pf);
/* Its guard's state half: the enemy acting one of the six, no member coming
 * back, its actions in the table; and the action shown ($C1:AC57: the
 * battle's step 4 and its hits landing), as C. */
bool battle_enemy_turn_fits(const GameState *g);
bool battle_action_shown(GameState *g, Platform *pf);
/* battle/effect_work.c: an enemy's action's effect worked out, each kind of
 * effect as the party's code runs it, and the notes of the enemy's action. */
/* An enemy's tech's or attack's effect worked out on its targets ($C1:D7C4,
 * $C1:D8D1), as C, and their guards' state half: combatants named, and an
 * effect the port works out. */
bool battle_enemy_action_worked_out(const GameState *g, bool tech);
void battle_work_out_enemy_action(GameState *g, Platform *pf, bool tech);
/* The carry a kind's work hands back: set after the note of what acted on the
 * target, clear after the restoring that shows the actor's HP, the losing of
 * a condition's bit and the kinds that do nothing (whose jump shifts the
 * kind's number, clearing it). */
bool battle_kind_leaves_carry(uint8_t kind);
/* battle/aim.c: whom a tech or an item aims at, and its call to the area
 * search. */
/* The action aimed ($C1:C1DD), from the carry its caller leaves, as C. */
bool battle_aim_action(GameState *g, Platform *pf, bool carry);
/* battle/member_turn.c: a party member's chosen action carried out, its
 * attack, the end of its turn, and the battle's step for a called-off
 * action. */
/* A member's chosen action carried out ($C1:B967), as C. */
bool battle_carry_out_member_action(GameState *g, Platform *pf);
/* A member's attack on the action's first target ($C1:BFAA), as C. */
bool battle_member_attacks(GameState *g, Platform *pf);
/* A member's turn its status took, ended ($C1:BCE1), as C. */
bool battle_end_member_turn(GameState *g);
/* battle/party_work.c: a party member's tech, or its attack, worked out, and
 * the item it uses. battle/inventory.c: an item added to the inventory. */
/* A member's tech worked out ($C1:BF79), and an item used ($C1:BF46), as C. */
bool battle_work_out_member_tech(GameState *g, Platform *pf);
bool battle_use_item(GameState *g, Platform *pf);
/* battle/fight_entries.c */
/* battle/scripts.c: the script commands whose operations are C, each at its
 * handler's address */
/* battle/script_walk.c */
struct ScriptCommand;

/* battle/stats.c */
/* Every combatant updated for the turn ($C1:B223), as C: false where a
 * member's coming back went no further. */
bool battle_update_all_combatants(GameState *g);

/* battle/battlefield_change.c */
/* The battlefield changed ($CD:0018), and an effect's rows of the
 * battlefield's tiles loaded, the first set or the second ($CD:0015,
 * $CD:002A), as C. */
bool battle_change_battlefield(GameState *g, Platform *pf, uint16_t battlefield);
bool battle_load_effect_tile_rows(GameState *g, Platform *pf, uint8_t first_row_set, bool second);

/* battle/rewards_messages.c */
/* The rewards' messages ($FD:AD17), as C; and the experience's share's guard's
 * state half ($C1:F93E). */
bool battle_show_the_rewards(GameState *g, Platform *pf);
bool battle_experience_can_be_shared(const GameState *g);

/* battle/timers.c */
/* battle/status_timers.c: the timed statuses' handlers, $C1:8876-$C1:8C08,
 * and the showing of a status's number, $C1:895B. */
/* A status's number, or the running away, shown as an action of its own
 * ($C1:895B), as C. */
bool battle_show_status_number(GameState *g, Platform *pf, uint8_t showing);

/* battle/opening.c */
void battle_start_the_frame_work(GameState *g);

/* battle/effect_paths.c */
/* The frame's effects' paths ($CF:EFAF), the kinds of path the game gives
 * (its guard's state half), and the draw pass ($CF:E83A, with someone on the
 * battlefield; false where nobody is). */
void battle_step_effect_slots(GameState *g);
bool battle_effect_paths_known(const GameState *g);
bool battle_objects_drawn(GameState *g);
void battle_place_formation(GameState *g);
/* battle/objects.c */
/* The places the scripts follow worked out ($C1:4212), as C. */
bool battle_update_animation_positions(GameState *g, Platform *pf);

/* battle/frame.c */
/* The map's layers that scroll by themselves, a frame's worth ($CD:0C89). */
void battle_scroll_the_map_layers(GameState *g);
enum { BATTLE_SCROLL_MAP_LAYERS_ENTRY = 0xCD0C89 };

/* battle/scripts.c */
/* The animation scripts' channels run for a frame ($C1:4BBE), as C. */
bool battle_run_scripts(GameState *g, Platform *pf);

/* battle/object_motion.c */
void battle_raise_numbers(GameState *g);
void battle_step_scripted_moves(GameState *g);
void battle_update_anchors(GameState *g);
void battle_record_trails(GameState *g);
void battle_cycle_party_colors(GameState *g);

/* battle/sprites.c */
/* Up to four of the bitmap shapes, a frame on ($C1:4943), as C. */
bool battle_build_bitmap_shapes(GameState *g, Platform *pf);

/* battle/window.c, battle/status.c, battle/digits.c, battle/menu_window.c,
 * battle/numbers.c: the windows and the numbers, and the guard they run under */
/* Whose combination techs are called off ($C1:B575), as C. */
bool battle_update_combo_members(GameState *g, Platform *pf);

/* battle/action_showing.c, battle/action_animation.c, battle/frame_step.c:
 * an action shown, its animation loaded, and the battle's own frame */
/* The action's animation loaded ($C1:4310), as C. */
bool battle_load_action_animation(GameState *g, Platform *pf);
/* $C1:0003: a battle step by its number (battle/steps.c), and its guard. */
/* The battle's steps 1 and 2 (battle/menu_line.c): a member's turn comes and
 * it joins the line for the menu; a member's action is called off. */
/* The party's poses (battle/party_poses.c): as a whole, a frame's step,
 * and the battle's steps 8 and 9, the victory and the defeat, that end in
 * them; the poses from a routine that goes on into them. */
/* The poses the party strikes: the opening's and the victory's by each
 * member taking part in the fight, the defeat's by all three. */
typedef enum { PARTY_POSE_OPENING, PARTY_POSE_VICTORY, PARTY_POSE_DEFEAT } PartyPose;
void battle_give_the_party_poses(GameState *g, PartyPose pose);

/* battle/arith.c, battle/buttons.c */
/* The controller's held buttons turned into the menus', with their repeat
 * ($CD:0340). */
void battle_update_button_repeat(GameState *g);

#endif
