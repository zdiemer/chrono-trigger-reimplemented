/* The battle's steps: the stretches of the battle's own frame work the main
 * loop and the turns ask for by number (BattleStep, battle.h) -- setting the
 * battle up, a member's command menu opened or its action called off, the
 * frame's work and the wait for the next, an action's showing, a target or an
 * area searched for, the enemies beaten seen to, the victory and the defeat.
 *
 * Each step is a routine of its own, most of which wait for frames, and the
 * asker's registers are kept across it, so that a turn can ask for a step in
 * the middle of its own work. The steps are found through a C table; those
 * that are hand ports are run as the original's call reaches them
 * (battle_run), the others are still the original's. */
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/program.h"
#include "battle/steps.h"
#include "charge.h"

/* The routine of each step, its hand port where the dispatch runs it on the
 * machine, what it doesn't read of what the dispatch hands it, and the step
 * as C. Setup leaves saveable waits; its return continuation restores the
 * scheduler's kept registers before resuming the main loop. */

static const struct { uint32_t routine; bool (*run)(GameState *, Platform *); } STEPS[] = {
    [BATTLE_STEP_SET_UP] = { 0xC10023, battle_set_up_runs },
    [BATTLE_STEP_OPEN_MENU] = { 0xC11B19, battle_turn_comes },
    [BATTLE_STEP_CALLED_OFF] = { 0xC11BAA, battle_menu_called_off },
    [BATTLE_STEP_NEXT_FRAME] = { 0xC1106E, battle_waits_a_frame },
    [BATTLE_STEP_SHOW_ACTION] = { 0xC14058, battle_show_action },
    [BATTLE_STEP_SEARCH_TARGET] = { 0xC12986, NULL },
    [BATTLE_STEP_ENEMIES_BEATEN] = { 0xC1006D, battle_see_to_the_beaten_enemies },
    [BATTLE_STEP_SEARCH_AREA] = { 0xC11FDD, NULL },
    [BATTLE_STEP_VICTORY] = { 0xC1356D, battle_wins_the_fight },
    [BATTLE_STEP_DEFEAT] = { 0xC1354D, battle_loses_the_fight },
};

enum { STEP_COUNT = sizeof STEPS / sizeof STEPS[0] };

/* ---- As C ------------------------------------------------------------------------------------ */

bool battle_step_taken(GameState *g, Platform *pf, BattleStep step)
{
    if ((unsigned)step >= STEP_COUNT || !STEPS[step].run)
        return battle_contract_broken(g, (unsigned)step < STEP_COUNT ? STEPS[step].routine : BATTLE_STEP, "a battle step");
    bool done = false;
    TIMED_AS(g, STEPS[step].routine, done = STEPS[step].run(g, pf));
    return done;
}
