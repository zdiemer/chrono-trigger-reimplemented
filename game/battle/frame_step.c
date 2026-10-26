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
 * (battle/menu_input.c) and the cursors placed. Those are ports of their own that call the original's
 * code, so they are run as the original's calls make them (battle_run); so
 * are the combatants' and the enemies' animation steps (battle/animation.c)
 * and the frame work's tail ($CD:0009, battle/frame.c). */
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
#include "vars.h"

/* ---- The calls -------------------------------------------------------------------------------- */

/* The routines the frame's steps call, each where the original makes it:
 * the menu's window drawn, its buttons taken and its cursors placed
 * (battle/menu_window.c, battle/menu_bridge.c) from each of the three places
 * the menu's turn is taken; the frame work's tail ($CD:0009, battle/frame.c);
 * the battle's wait for the next frame ($C1:007E); the combatants' animation
 * step (battle/animation.c); the enemies' effects (battle/animation.c); and
 * the message line's description (battle/message.c). The program makes them
 * as C, and the machine's entries from the original's calls. */
typedef enum { MENU_WINDOW, MENU_BUTTONS, MENU_CURSORS, MENU_PARTS } MenuPart;
typedef enum { TURN_IN_THE_FRAME_STEP, TURN_IN_THE_WAIT, TURN_IN_THE_WAIT_STEP, TURN_PLACES } MenuTurnPlace;
typedef enum {
    STEP_MENU_PART,                    /* + the place's MENU_PARTS */
    STEP_FRAME_STEP_TAIL = STEP_MENU_PART + TURN_PLACES * MENU_PARTS,
    STEP_NOT_DUE,                      /* the menu's turn not due: the original tested its count */
    STEP_REFUSAL_CLEARED, STEP_REFUSAL_SAID,
    STEP_FIRST_FRAME, STEP_FIRST_TAIL, STEP_ANIMATE, STEP_FIRST_EFFECTS,
    STEP_SECOND_FRAME, STEP_SECOND_TAIL, STEP_SECOND_EFFECTS,
} StepCall;
typedef bool (*StepCalls)(void *ctx, StepCall call);

static StepCall menu_part(MenuTurnPlace place, MenuPart part) { return (StepCall)(STEP_MENU_PART + place * MENU_PARTS + part); }

/* ---- The menu's turn ------------------------------------------------------------------------- */

/* The menu takes its turn every fourth frame. */
static bool menu_turn_due(const GameState *g) { return battle_frame_tick_4(g) != 0; }

static bool take_the_menu_turn(GameState *g, MenuTurnPlace place, StepCalls call, void *ctx)
{
    set_battle_frame_tick_4(g, 0);
    TIMED_AS(g, 0xC11B67, battle_next_member_takes_the_menu(g));
    for (MenuPart part = MENU_WINDOW; part < MENU_PARTS; part++)
        if (!call(ctx, menu_part(place, part)))
            return false;
    return true;
}

/* The menu's turn between two steps of another wait: the frame's queued
 * uploads wait while it goes. */
static bool menu_turn_between_steps(GameState *g, MenuTurnPlace place, StepCalls call, void *ctx)
{
    if (!menu_turn_due(g))
        return call(ctx, STEP_NOT_DUE);
    set_battle_queued_uploads_on(g, 0);
    return take_the_menu_turn(g, place, call, ctx);
}

// $C1:10FA
/* The frame step: the menu's turn, then the frame work's tail. */
static bool frame_step(GameState *g, StepCalls call, void *ctx)
{
    return menu_turn_between_steps(g, TURN_IN_THE_FRAME_STEP, call, ctx) && call(ctx, STEP_FRAME_STEP_TAIL);
}

// $C1:0012, $C1:10E3
/* The menu's turn alone, as the frame work's wait for the next frame takes
 * it (battle/wait.c), by a long call that jumps on to it. */
static bool menu_turn(GameState *g, StepCalls call, void *ctx)
{
    return menu_turn_between_steps(g, TURN_IN_THE_WAIT, call, ctx);
}

/* ---- The battle's step 3: a frame of waiting ------------------------------------------------- */

/* Holding L and R in a battle that can't be run from says so once, until
 * they are let go (the frame update clears the state): the message line
 * clears, and says the party can't escape. */
enum { MESSAGE_CLEARED = 0xFF, MESSAGE_CANT_ESCAPE = 0x75 };

static bool refuse_to_run(GameState *g, StepCalls call, void *ctx)
{
    if (!battle_lr_held(g) || !(battle_setup_flags(g) & BATTLE_SETUP_NO_RUNNING))
        return true;
    set_battle_lr_held(g, 0);
    if (battle_lr_state(g))
        return true;
    set_battle_lr_state(g, (uint8_t)(battle_lr_state(g) + 1));
    return call(ctx, STEP_REFUSAL_CLEARED) && call(ctx, STEP_REFUSAL_SAID);
}

/* The combatants animate a step (a routine of its own), whose group goes by
 * the frame's phase; the phase is kept around it, the animations' step
 * moving it on where it ends a group early. */
static bool animate_the_combatants(GameState *g, StepCalls call, void *ctx)
{
    set_battle_frame_phase_saved(g, battle_frame_phase(g));
    if (!call(ctx, STEP_ANIMATE))
        return false;
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
static bool wait_step(GameState *g, StepCalls call, void *ctx)
{
    if (!battle_defeat_showing(g)) {
        set_battle_orbits_held(g, 0);
        if (!refuse_to_run(g, call, ctx))
            return false;
    }
    if (!call(ctx, STEP_FIRST_FRAME) || !call(ctx, STEP_FIRST_TAIL) || !animate_the_combatants(g, call, ctx)
        || !call(ctx, STEP_FIRST_EFFECTS))
        return false;
    TIMED_AS(g, 0xC1283D, battle_update_all_bounds(g));
    set_battle_queued_uploads_on(g, 0);
    if (!call(ctx, STEP_SECOND_FRAME) || !call(ctx, STEP_SECOND_TAIL))
        return false;
    set_battle_queued_uploads_on(g, (uint8_t)(battle_queued_uploads_on(g) + 1));
    if (menu_turn_due(g)) {
        if (!take_the_menu_turn(g, TURN_IN_THE_WAIT_STEP, call, ctx))
            return false;
        set_battle_menu_buttons(g, 0);
    }
    if (!call(ctx, STEP_SECOND_EFFECTS))
        return false;
    TIMED_AS(g, 0xC1283D, battle_update_all_bounds(g));
    return true;
}

/* The guards' state half: the menu's turn finds its member's cells with the
 * queue's first, so where the turn is due the queue's first is a party
 * member or nobody, as the game has it. */
static bool queue_holds_a_member(const GameState *g)
{
    uint8_t member = battle_menu_queue(g, 0);
    return menu_nobody(member) || battle_slot_is_party(member);
}

static bool frame_step_fits(const GameState *g) { return !menu_turn_due(g) || queue_holds_a_member(g); }

/* ---- As C ------------------------------------------------------------------------------------ */

typedef struct {
    GameState *game;
    Platform *pf;
} StepAsC;

enum { MENU_WINDOW_ENTRY = 0xC10C2D, MENU_BUTTONS_ENTRY = 0xC11153, MENU_CURSORS_ENTRY = 0xC117DD,
       FRAME_TAIL = 0xCD0009, FRAME_WAIT = 0xC1007E, ANIMATIONS = 0xC12D9F, ENEMY_EFFECTS = 0xC13714,
       DESCRIBE = 0xCD0027, WAIT_STEP = 0xC1106E };

static bool step_as_c(void *ctx, StepCall call)
{
    StepAsC *c = ctx;
    GameState *g = c->game;
    bool done = false;
    switch (call) {
    case STEP_NOT_DUE: return true;
    case STEP_FRAME_STEP_TAIL:
    case STEP_FIRST_TAIL:
    case STEP_SECOND_TAIL: TIMED_AS(g, FRAME_TAIL, done = battle_frame_wait_finished(g, c->pf)); return done;
    case STEP_FIRST_FRAME:
    case STEP_SECOND_FRAME: TIMED_AS(g, FRAME_WAIT, done = battle_wait_for_frame(g, c->pf)); return done;
    case STEP_ANIMATE:
        if (!battle_pose_looks_fit(g))
            return battle_needs_the_machine(g, ANIMATIONS, "the combatants' animations with looks the port doesn't take");
        TIMED_AS(g, ANIMATIONS, battle_step_combatants(g));
        return true;
    case STEP_FIRST_EFFECTS:
    case STEP_SECOND_EFFECTS: TIMED_AS(g, ENEMY_EFFECTS, done = battle_enemy_effects_stepped(g, c->pf)); return done;
    case STEP_REFUSAL_CLEARED:
    case STEP_REFUSAL_SAID: return battle_needs_the_machine(g, DESCRIBE, "the message line's description");
    default: break;
    }
    switch ((MenuPart)((call - STEP_MENU_PART) % MENU_PARTS)) {
    case MENU_WINDOW: TIMED_AS(g, MENU_WINDOW_ENTRY, done = battle_menu_window_drawn(g, c->pf)); return done;
    case MENU_BUTTONS: TIMED_AS(g, MENU_BUTTONS_ENTRY, done = battle_menu_input_taken(g, c->pf)); return done;
    default: TIMED_AS(g, MENU_CURSORS_ENTRY, done = battle_menu_cursors_placed(g, c->pf)); return done;
    }
}

bool battle_menu_turn(GameState *g, Platform *pf)
{
    if (!frame_step_fits(g))
        return battle_needs_the_machine(g, BATTLE_MENU_TURN_ENTRY, "a menu queue the port doesn't take");
    StepAsC c = { g, pf };
    return menu_turn(g, step_as_c, &c);
}

bool battle_frame_stepped(GameState *g, Platform *pf)
{
    if (!frame_step_fits(g))
        return battle_needs_the_machine(g, 0xC110FA, "a menu queue the port doesn't take");
    StepAsC c = { g, pf };
    return frame_step(g, step_as_c, &c);
}

bool battle_waits_a_frame(GameState *g, Platform *pf)
{
    if (!frame_step_fits(g))
        return battle_needs_the_machine(g, WAIT_STEP, "a menu queue the port doesn't take");
    StepAsC c = { g, pf };
    return wait_step(g, step_as_c, &c);
}

/* ---- On the machine ------------------------------------------------------------------------ */

/* Where the original's routines make the menu's three calls. */
static const uint32_t menu_sites[TURN_PLACES][MENU_PARTS] = {
    [TURN_IN_THE_FRAME_STEP] = { 0xC11107, 0xC1110A, 0xC1110D },
    [TURN_IN_THE_WAIT] = { 0xC110F0, 0xC110F3, 0xC110F6 },
    [TURN_IN_THE_WAIT_STEP] = { 0xC110D1, 0xC110D4, 0xC110D7 },
};

/* The message line's description ($CD:0027, battle/message.c) takes the
 * message in A's low byte. */
static bool show_message(Snes *s, uint32_t site, uint8_t message)
{
    m_set_a8(s, message);
    return battle_run_long(s, site, DESCRIBE, battle_describe);
}

/* Wait for the next frame ($C1:007E) from the call at `site`: the wait reads
 * none of the registers but A's high byte, which it takes along. */
enum { FRAME_WAIT_READS = DC_AL | DC_X | DC_Y | DC_FLAGS };

static bool wait_for_the_next_frame(Snes *s, uint32_t site)
{
    return battle_wait_frame(s, site, FRAME_WAIT, FRAME_WAIT_READS);
}

/* The frame work's tail ($CD:0009), from the call at `site`. */
static bool finish_the_frame(Snes *s, uint32_t site)
{
    return battle_run_long(s, site, FRAME_TAIL, battle_frame_wait_tail);
}

/* The combatants' animation step runs as C where its entry's guard would
 * take it (battle_steps_animations): A's high byte clear, which it indexes
 * with, and the party's palettes the game's; otherwise it is the original's. */
static bool animate_on_the_machine(Snes *s)
{
    if (!battle_steps_animations(s))
        return battle_call(s, 0xC110A9, ANIMATIONS, DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C);
    TIMED_AS(state_of(s), ANIMATIONS, battle_step_combatants(state_of(s)));
    return true;
}

static bool step_on_the_machine(void *ctx, StepCall call)
{
    Snes *s = ctx;
    switch (call) {
    case STEP_NOT_DUE:
        /* the original tests the menu's count in A, which the frame work's
         * tail is called with where the turn isn't due */
        m_set_a8(s, 0);
        m_set_nz8(s, 0);
        return true;
    case STEP_FRAME_STEP_TAIL: return finish_the_frame(s, 0xC11110);
    case STEP_REFUSAL_CLEARED: return show_message(s, 0xC11093, MESSAGE_CLEARED);
    case STEP_REFUSAL_SAID: return show_message(s, 0xC11099, MESSAGE_CANT_ESCAPE);
    case STEP_FIRST_FRAME: return wait_for_the_next_frame(s, 0xC1109D);
    case STEP_FIRST_TAIL: return finish_the_frame(s, 0xC110A0);
    case STEP_ANIMATE: return animate_on_the_machine(s);
    case STEP_FIRST_EFFECTS: return battle_run(s, 0xC110B5, ENEMY_EFFECTS, battle_step_enemy_effects);
    case STEP_SECOND_FRAME: return wait_for_the_next_frame(s, 0xC110BD);
    case STEP_SECOND_TAIL: return finish_the_frame(s, 0xC110C0);
    case STEP_SECOND_EFFECTS: return battle_run(s, 0xC110DC, ENEMY_EFFECTS, battle_step_enemy_effects);
    default: break;
    }
    unsigned at = call - STEP_MENU_PART;
    uint32_t site = menu_sites[at / MENU_PARTS][at % MENU_PARTS];
    switch ((MenuPart)(at % MENU_PARTS)) {
    case MENU_WINDOW: return battle_run(s, site, MENU_WINDOW_ENTRY, battle_draw_menu_window);
    case MENU_BUTTONS: return battle_run(s, site, MENU_BUTTONS_ENTRY, battle_take_menu_input);
    default: return battle_run(s, site, MENU_CURSORS_ENTRY, battle_place_menu_cursors);
    }
}

/* ---- The entries ---------------------------------------------------------------------------- */

void battle_frame_step(Snes *s)
{
    if (frame_step(state_of(s), step_on_the_machine, s))
        m_rts(s);
}

void battle_menu_turn_long(Snes *s)
{
    if (menu_turn(state_of(s), step_on_the_machine, s))
        m_rtl(s);
}

/* The step ends with the combatants' boxes worked out, which leaves A's high
 * byte clear and the carry of the last test, set, for the main loop. */
void battle_wait_step(Snes *s)
{
    if (!wait_step(state_of(s), step_on_the_machine, s))
        return;
    s->cpu.a = 0;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* The guards: on the battle's page, A's high byte clear where the turn is
 * due (the original takes it along into the member's index), and their
 * state half. */
bool battle_menu_queue_ready(Snes *s)
{
    vars_peek(true);
    bool fits = queue_holds_a_member(state_of(s));
    vars_peek(false);
    return battle_on_page(s) && (s->cpu.a & 0xFF00) == 0 && fits;
}

bool battle_frame_step_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool fits = !menu_turn_due(g) || ((s->cpu.a & 0xFF00) == 0 && queue_holds_a_member(g));
    vars_peek(false);
    return battle_on_page(s) && fits;
}
