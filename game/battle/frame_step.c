/* Battle: the battle's own frame, and the menu's turn in it.
 *
 * Beside the frame work the vblank handler runs (battle/frame.c), the
 * battle's own code steps its world a frame at a time while it waits for
 * something: between two of its waits for the next frame it lets the party's
 * menu take its turn and finishes the frame's work (the frame step, which the
 * action's showing and the effects' loading take between their own steps),
 * and while nothing else happens (the battle's step 3, which the main loop
 * asks for) it animates the combatants and the enemies' effects as well.
 *
 * The menu takes its turn every fourth frame (battle_frame_tick_4), in the
 * frame step and in the frame work's wait for the next frame: the next
 * party member in line for the menu takes it (battle/menu_line.c), then the
 * menu's window is drawn (battle/menu_window.c), the buttons taken
 * (battle/menu_input.c) and the cursors placed. Those are routines of their
 * own, called where the original calls them (StepCall); so are the
 * combatants' and the enemies' animation steps (battle/animation.c) and the
 * frame work's tail ($CD:0009, battle/frame.c). */
#include "battle/animation.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/frame.h"
#include "battle/menu_bridge.h"
#include "battle/program.h"
#include "battle/steps.h"
#include "battle/wait.h"
#include "battle/message.h"
#include "battle/window.h"
#include "charge.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The routines the steps call ------------------------------------------------------------- */

enum {
    MENU_WINDOW_ENTRY = 0xC10C2D,
    MENU_BUTTONS_ENTRY = 0xC11153,
    MENU_CURSORS_ENTRY = 0xC117DD,
    FRAME_TAIL = 0xCD0009,
    ANIMATIONS = 0xC12D9F,
    ENEMY_EFFECTS = 0xC13714,
    WAIT_STEP = 0xC1106E,
    FRAME_STEP = 0xC110FA,
};

/* The frame work's tail ($CD:0009, battle/frame.c). */
static bool finish_the_frame_work(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, FRAME_TAIL, done = battle_frame_wait_finished(g, pf));
    return done;
}

/* The enemies' effects' step (battle/animation.c). */
static bool step_the_enemy_effects(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, ENEMY_EFFECTS, done = battle_enemy_effects_stepped(g, pf));
    return done;
}

/* ---- The menu's turn ------------------------------------------------------------------------- */

/* The menu takes its turn every fourth frame. */
static bool menu_turn_due(const GameState *g) { return battle_frame_tick_4(g) != 0; }

/* The next party member in line takes the menu, then its window is drawn,
 * its buttons taken and its cursors placed (battle/menu_window.c,
 * battle/menu_bridge.c), each a routine of its own. */
static bool take_the_menu_turn(GameState *g, Platform *pf)
{
    set_battle_frame_tick_4(g, 0);
    TIMED_AS(g, 0xC11B67, battle_next_member_takes_the_menu(g));
    bool done = false;
    TIMED_AS(g, MENU_WINDOW_ENTRY, done = battle_menu_window_drawn(g, pf));
    if (!done)
        return false;
    TIMED_AS(g, MENU_BUTTONS_ENTRY, done = battle_menu_input_taken(g, pf));
    if (!done)
        return false;
    TIMED_AS(g, MENU_CURSORS_ENTRY, done = battle_menu_cursors_placed(g, pf));
    return done;
}

// $C1:10FA
/* The menu's turn where it is due, between the frame's steps. */
static bool menu_turn_between_steps(GameState *g, Platform *pf)
{
    if (!menu_turn_due(g))
        return true;
    set_battle_queued_uploads_on(g, 0);
    return take_the_menu_turn(g, pf);
}

/* ---- The battle's step 3: a frame of waiting ------------------------------------------------- */

/* Holding L and R in a battle that can't be run from says so once, until
 * they are let go (the frame update clears the state): the message line
 * clears, and says the party can't escape. */
enum { MESSAGE_CLEARED = 0xFF, MESSAGE_CANT_ESCAPE = 0x75 };

static bool refuse_to_run(GameState *g)
{
    if (!battle_lr_held(g) || !(battle_setup_flags(g) & BATTLE_SETUP_NO_RUNNING))
        return true;
    set_battle_lr_held(g, 0);
    if (battle_lr_state(g))
        return true;
    set_battle_lr_state(g, (uint8_t)(battle_lr_state(g) + 1));
    return battle_described(g, MESSAGE_CLEARED) && battle_described(g, MESSAGE_CANT_ESCAPE);
}

/* The combatants animate a step (a routine of its own), whose group goes by
 * the frame's phase; the phase is kept around it, the animations' step
 * moving it on where it ends a group early. */
static bool animate_the_combatants(GameState *g)
{
    set_battle_frame_phase_saved(g, battle_frame_phase(g));
    if (!battle_animations_fit(g))
        return battle_contract_broken(g, ANIMATIONS, "the combatants' animation outside its look or frame buffers");
    TIMED_AS(g, ANIMATIONS, battle_step_combatants(g));
    set_battle_frame_phase(g, battle_frame_phase_saved(g));
    return true;
}

// $C1:106E
/* A frame of the battle waiting: the enemies' orbits go on unless the
 * defeat is showing, L and R held where the party can't run say so; then
 * two frames, the first animating the combatants and the enemies' effects,
 * the queued uploads held after it, the second turning them back on, giving
 * the menu its turn (the buttons it took forgotten) and the effects another
 * step; the combatants' boxes following where they stand after each. */
static bool wait_step(GameState *g, Platform *pf)
{
    if (!battle_defeat_showing(g)) {
        set_battle_orbits_held(g, 0);
        if (!refuse_to_run(g))
            return false;
    }
    battle_wait_for_frame(g, pf);
    if (!finish_the_frame_work(g, pf) || !animate_the_combatants(g) || !step_the_enemy_effects(g, pf))
        return false;
    TIMED_AS(g, 0xC1283D, battle_update_all_bounds(g));
    set_battle_queued_uploads_on(g, 0);
    battle_wait_for_frame(g, pf);
    if (!finish_the_frame_work(g, pf))
        return false;
    set_battle_queued_uploads_on(g, (uint8_t)(battle_queued_uploads_on(g) + 1));
    if (menu_turn_due(g)) {
        if (!take_the_menu_turn(g, pf))
            return false;
        set_battle_menu_buttons(g, 0);
    }
    if (!step_the_enemy_effects(g, pf))
        return false;
    TIMED_AS(g, 0xC1283D, battle_update_all_bounds(g));
    return true;
}

/* What the steps take: the menu's turn finds its member's cells with the
 * queue's first, so where the turn is due the queue's first is a party
 * member or nobody, as the game has it. */
static bool queue_holds_a_member(const GameState *g)
{
    uint8_t member = battle_menu_queue(g, 0);
    return menu_nobody(member) || battle_slot_is_party(member);
}

static bool frame_step_fits(const GameState *g) { return !menu_turn_due(g) || queue_holds_a_member(g); }

/* ---- The steps -------------------------------------------------------------------------------- */

// $C1:0012, $C1:10E3
/* The menu's turn alone, as the frame work's wait for the next frame takes
 * it (battle/wait.c), by a long call that jumps on to it. */
bool battle_menu_turn(GameState *g, Platform *pf)
{
    if (!PORT_CHECK(frame_step_fits(g)))
        return battle_contract_broken(g, BATTLE_MENU_TURN_ENTRY, "a menu queue the port doesn't take");
    return menu_turn_between_steps(g, pf);
}

/* The frame step: the menu's turn, then the frame work's tail. */
static bool frame_step(GameState *g, Platform *pf)
{
    if (!PORT_CHECK(frame_step_fits(g)))
        return battle_contract_broken(g, FRAME_STEP, "a menu queue the port doesn't take");
    return menu_turn_between_steps(g, pf) && finish_the_frame_work(g, pf);
}

bool battle_frame_stepped(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, FRAME_STEP, done = frame_step(g, pf));
    return done;
}

bool battle_waits_a_frame(GameState *g, Platform *pf)
{
    if (!PORT_CHECK(frame_step_fits(g)))
        return battle_contract_broken(g, WAIT_STEP, "a menu queue the port doesn't take");
    return wait_step(g, pf);
}
