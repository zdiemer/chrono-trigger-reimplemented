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
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"

/* The steps' routines read the accumulator as the original's table lookup
 * leaves it (the step doubled) and the carry. The frame's work and an
 * action's showing may also read the overflow flag as the asker leaves it
 * (liveness can't rule that out for them), which the dispatch passes on
 * untouched; the others don't. Their askers read the accumulator and the
 * carry after (tools/dontcare_check.py checks both). */
enum { STEP_READS_A_AND_OVERFLOW = DC_X | DC_Y | DC_N | DC_Z, STEP_READS_A = STEP_READS_A_AND_OVERFLOW | DC_V };

/* The routine of each step, its hand port where the dispatch runs it on the
 * machine, what it doesn't read of what the dispatch hands it, and the step
 * as C. The setup is called on the machine as the original's routine, whose
 * ports (battle/opening.c) go on over frames from the waits they stop at,
 * which a call there can't hold. */
static const struct {
    uint32_t routine;
    void (*entry)(Snes *);
    uint16_t dont_care;
    bool (*as_c)(GameState *g, Platform *pf);
} STEPS[] = {
    [BATTLE_STEP_SET_UP] = { 0xC10023, NULL, STEP_READS_A, battle_set_up_runs },
    [BATTLE_STEP_OPEN_MENU] = { 0xC11B19, battle_turn_comes_step, STEP_READS_A, battle_turn_comes },
    [BATTLE_STEP_CALLED_OFF] = { 0xC11BAA, battle_called_off_step, STEP_READS_A, battle_menu_called_off },
    [BATTLE_STEP_NEXT_FRAME] = { 0xC1106E, battle_wait_step, STEP_READS_A_AND_OVERFLOW, battle_waits_a_frame },
    [BATTLE_STEP_SHOW_ACTION] = { 0xC14058, battle_show_action_entry, STEP_READS_A_AND_OVERFLOW, battle_show_action },
    [BATTLE_STEP_SEARCH_TARGET] = { 0xC12986, battle_search_for_target, STEP_READS_A, NULL },
    [BATTLE_STEP_ENEMIES_BEATEN] = { 0xC1006D, battle_see_to_the_beaten_enemies_entry, STEP_READS_A, battle_see_to_the_beaten_enemies },
    [BATTLE_STEP_SEARCH_AREA] = { 0xC11FDD, battle_find_the_noted_area, STEP_READS_A, NULL },
    [BATTLE_STEP_VICTORY] = { 0xC1356D, battle_victory_step, STEP_READS_A, battle_wins_the_fight },
    [BATTLE_STEP_DEFEAT] = { 0xC1354D, battle_defeat_step, STEP_READS_A, battle_loses_the_fight },
};
enum { STEP_COUNT = sizeof STEPS / sizeof STEPS[0] };

/* The dispatch's last pull, the asker's accumulator, leaves its zero and
 * negative. */
static void leave_the_last_pull(Snes *s, BattleFrame kept) { m_set_nz8(s, kept.kept[0]); }

/* Run a step as the dispatch's table lookup has it: the step's routine finds
 * the table's index, the step doubled, in the accumulator's low byte and in X,
 * with the accumulator's high byte, and the doubling's carry; what the asker
 * had in the accumulator and the index registers kept on the stack under the
 * step's call (`kept`), and handed back after it. False where the step
 * stopped, the original carrying on from it. */
static bool run_the_step(Snes *s, BattleStep step, BattleFrame kept)
{
    kept.carry = battle_carry_of((step & 0x80) != 0);
    m_set_a8(s, (uint8_t)(step << 1));
    s->cpu.x = s->cpu.a;
    bool ran;
    if (STEPS[step].entry)
        NATIVE_TIME_AS_SIBLING(s, STEPS[step].routine,
                               ran = battle_run_dc_under(s, 0xC1004A, STEPS[step].routine, STEPS[step].dont_care,
                                                         STEPS[step].entry, kept));
    else
        ran = battle_run_dc_under(s, 0xC1004A, STEPS[step].routine, STEPS[step].dont_care, NULL, kept);
    if (!ran)
        return false;
    battle_give_the_registers_back(s, kept);
    ORIGINAL_EFFECT(leave_the_last_pull(s, kept));
    return true;
}

// $C1:0003
/* Run the step the accumulator names, for the original's code: what the asker
 * has in the accumulator and the index registers is kept on the stack under
 * the step's call, and is what it has back after. */
void battle_run_a_step(Snes *s)
{
    BattleFrame kept = battle_keeping_the_registers(s, BATTLE_CARRY_AS_IT_IS);
    if (run_the_step(s, (BattleStep)kept.kept[0], kept))
        m_rts(s);
}

/* A step the table has, asked for on the battle's page with the
 * accumulator's high byte clear (the index the original's lookup makes). */
bool battle_step_is_known(Snes *s)
{
    return battle_on_page_a_high_clear(s) && (uint8_t)s->cpu.a < STEP_COUNT;
}

/* Where the dispatch keeps the asker's registers, below the asker's return
 * address: A's low byte, then X and Y, each high byte first. */
enum { KEPT_X_BELOW = 2, KEPT_Y_BELOW = 4, KEPT_REGISTER_BYTES = 2 };

/* The asker's X and Y the dispatch keeps are what its call declares the
 * dispatch doesn't read (tools/dontcare_check.py checks the declaration as
 * the original's call at the asker's site): the dispatch only pulls them back
 * after the step, which the port does from its own copy. So where the asker
 * declares them, its own, which aren't the original's, aren't compared while
 * they are kept. */
static void leave_the_kept_index_registers_uncompared(Snes *s, uint16_t dont_care)
{
    uint16_t top = s->cpu.s;
    if (dont_care & DC_X)
        native_stack_dont_care(s, (uint16_t)(top - KEPT_X_BELOW), KEPT_REGISTER_BYTES);
    if (dont_care & DC_Y)
        native_stack_dont_care(s, (uint16_t)(top - KEPT_Y_BELOW), KEPT_REGISTER_BYTES);
}

/* Whether the runtime would run the dispatch's hand port for the original's
 * call made now: its precondition and guard hold, and the run hasn't
 * left it out. Outside the runtime (ct_fuzz) it wouldn't, and the call is
 * made. */
static bool runs_the_dispatch_here(Snes *s)
{
    const NativeEntry *e = native_hand_entry_at(s, BATTLE_STEP);
    return e && e->fn == battle_run_a_step;
}

bool battle_take_step(Snes *s, uint32_t site, BattleStep step, uint16_t dont_care)
{
    m_set_a8(s, (uint8_t)step);
    if (!runs_the_dispatch_here(s))
        return battle_call(s, site, BATTLE_STEP, dont_care);
    uint16_t page = m_direct_page(s);
    uint8_t bank = s->cpu.db;
    native_poison_as_call(s, site, BATTLE_STEP, dont_care);
    battle_push_the_return(s, site);
    BattleFrame kept = battle_keeping_the_registers(s, BATTLE_CARRY_AS_IT_IS);
    leave_the_kept_index_registers_uncompared(s, dont_care);
    bool done = false;
    NATIVE_TIME_AS_SIBLING(s, BATTLE_STEP, done = run_the_step(s, step, kept));
    native_stack_cared(s);
    if (!done)
        return false;
    battle_pull_the_return(s);
    return battle_back(s, site + BATTLE_JSR_SIZE, page, bank);
}

/* ---- As C ------------------------------------------------------------------------------------ */

bool battle_step_taken(GameState *g, Platform *pf, BattleStep step)
{
    if ((unsigned)step >= STEP_COUNT || !STEPS[step].as_c)
        return battle_needs_the_machine(g, (unsigned)step < STEP_COUNT ? STEPS[step].routine : BATTLE_STEP, "a battle step");
    bool done = false;
    TIMED_AS(g, STEPS[step].routine, done = STEPS[step].as_c(g, pf));
    return done;
}
