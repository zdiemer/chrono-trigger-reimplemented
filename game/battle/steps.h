/* Battle: the battle's steps (BattleStep, battle.h) as C (battle/steps.c). */
#ifndef CT_GAME_BATTLE_STEPS_H
#define CT_GAME_BATTLE_STEPS_H

#include <stdbool.h>

#include "battle/battle.h"
#include "platform/platform.h"
#include "state.h"

/* A step the main loop or a turn asks for, run as C through the steps'
 * table: false where it went no further. */
bool battle_step_taken(GameState *g, Platform *pf, BattleStep step);

/* The steps as C, in their files: the setup (battle/opening.c) and a frame
 * of waiting (battle/frame_step.c). */
bool battle_set_up_runs(GameState *g, Platform *pf);
bool battle_waits_a_frame(GameState *g, Platform *pf);
/* The victory's and the defeat's steps, and the opening's poses
 * (battle/party_poses.c). */
bool battle_wins_the_fight(GameState *g, Platform *pf);
bool battle_loses_the_fight(GameState *g, Platform *pf);
bool battle_party_strikes_its_poses(GameState *g, Platform *pf);
/* A member's turn come, and its menu called off (battle/menu_line.c). */
bool battle_turn_comes(GameState *g, Platform *pf);
bool battle_menu_called_off(GameState *g, Platform *pf);
/* An action shown (battle/action_showing.c). */
bool battle_show_action(GameState *g, Platform *pf);
/* The enemies beaten seen off (battle/beaten_enemies.c). */
bool battle_see_to_the_beaten_enemies(GameState *g, Platform *pf);

#endif
