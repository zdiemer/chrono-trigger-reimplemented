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
 * The menu takes its turn every fourth frame (battle_frame_tick_4): the next
 * party member waiting for the menu takes it, then the menu's window is
 * drawn (battle/menu_window.c), the buttons taken (battle/menu_input.c) and
 * the cursors placed. Those are ports of their own that call the original's
 * code, so they are run as the original's calls make them (battle_run); so
 * are the combatants' and the enemies' animation steps (battle/animation.c)
 * and the frame work's tail ($CD:0009, battle/frame.c). */
#include "battle/animation.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "charge.h"
#include "vars.h"

/* ---- The menu's queue ------------------------------------------------------------------------ */

/* A queue entry's member, or none (negative). */
enum { NOBODY_WAITING = 0x80, NO_MENU_SLOT = 0x80 };

/* The window is to be built afresh for the member, whichever slot it shows. */
enum { WINDOW_TO_REBUILD = 0xFE };

// $C1:1B67
/* The member first in line for the menu takes it: its choice starts clean,
 * its tech list's cursor and scroll as the battle remembers them, it takes
 * the menu slot of its own place, and the window is built afresh; where no
 * slot is current, its slot is. The rest of the line moves up, as the
 * original moves it: the second place takes the third's member, and the
 * fourth too, the third keeping its own. */
static void next_member_takes_the_menu(GameState *g)
{
    uint8_t member = battle_menu_queue(g, 0);
    if (member & NOBODY_WAITING)
        return;
    set_battle_member_choice_extra(g, member, 0);
    set_battle_tech_cursor(g, member, battle_remembered_tech_row(g, member));
    set_battle_tech_scroll(g, member, battle_remembered_tech_scroll(g, member));
    set_battle_menu_slot_members(g, member, member);
    set_battle_menu_slot_shown(g, WINDOW_TO_REBUILD);
    set_battle_menu_queue(g, 0, battle_menu_queue(g, 1));
    set_battle_menu_queue(g, 1, battle_menu_queue(g, 2));
    set_battle_menu_queue(g, 3, battle_menu_queue(g, 2));
    set_battle_menu_queue_length(g, (uint8_t)(battle_menu_queue_length(g) - 1));
    set_battle_menu_member_count(g, (uint8_t)(battle_menu_member_count(g) + 1));
    if (battle_menu_slot(g) & NO_MENU_SLOT)
        set_battle_menu_slot(g, member);
}

/* ---- The menu's turn ------------------------------------------------------------------------- */

/* Where the original's routines make the menu's four calls. */
typedef struct {
    uint32_t window, buttons, cursors;
} MenuCalls;

/* The menu takes its turn every fourth frame. */
static bool menu_turn_due(const GameState *g) { return battle_frame_tick_4(g) != 0; }

static bool take_the_menu_turn(Snes *s, MenuCalls calls)
{
    GameState *g = state_of(s);
    set_battle_frame_tick_4(g, 0);
    TIMED_AS(g, 0xC11B67, next_member_takes_the_menu(g));
    return battle_run(s, calls.window, 0xC10C2D, battle_draw_menu_window)
           && battle_run(s, calls.buttons, 0xC11153, battle_take_menu_input)
           && battle_run(s, calls.cursors, 0xC117DD, battle_place_menu_cursors);
}

/* The original tests the menu's count in A, which the frame work's tail is
 * called with where the turn isn't due. */
static void leave_the_turn_tested(Snes *s)
{
    m_set_a8(s, 0);
    m_set_nz8(s, 0);
}

/* The menu's turn between two steps of another wait: the frame's queued
 * uploads wait while it goes. */
static bool menu_turn_between_steps(Snes *s, MenuCalls calls)
{
    GameState *g = state_of(s);
    if (!menu_turn_due(g)) {
        leave_the_turn_tested(s);
        return true;
    }
    set_battle_queued_uploads_on(g, 0);
    return take_the_menu_turn(s, calls);
}

/* The frame work's tail ($CD:0009), from the call at `site`. */
static bool finish_the_frame(Snes *s, uint32_t site)
{
    return battle_run_long(s, site, 0xCD0009, battle_frame_wait_tail);
}

// $C1:10FA
/* The frame step: the menu's turn, then the frame work's tail. */
static bool frame_step(Snes *s)
{
    return menu_turn_between_steps(s, (MenuCalls){ 0xC11107, 0xC1110A, 0xC1110D }) && finish_the_frame(s, 0xC11110);
}

void battle_frame_step(Snes *s)
{
    if (frame_step(s))
        m_rts(s);
}

/* ---- The battle's step 3: a frame of waiting ------------------------------------------------- */

/* Holding L and R in a battle that can't be run from says so once, until
 * they are let go (the frame update clears the state): the message line
 * clears, and says the party can't escape. */
enum { MESSAGE_CLEARED = 0xFF, MESSAGE_CANT_ESCAPE = 0x75 };

/* The message routine ($CD:0027) reads the message in A's low byte. */
enum { MESSAGE_READS = DC_AH | DC_X | DC_Y | DC_FLAGS };

static bool show_message(Snes *s, uint32_t site, uint8_t message)
{
    m_set_a8(s, message);
    return battle_call_long(s, site, 0xCD0027, MESSAGE_READS);
}

static bool refuse_to_run(Snes *s)
{
    GameState *g = state_of(s);
    if (!battle_lr_held(g) || !(battle_setup_flags(g) & BATTLE_SETUP_NO_RUNNING))
        return true;
    set_battle_lr_held(g, 0);
    if (battle_lr_state(g))
        return true;
    set_battle_lr_state(g, (uint8_t)(battle_lr_state(g) + 1));
    return show_message(s, 0xC11093, MESSAGE_CLEARED) && show_message(s, 0xC11099, MESSAGE_CANT_ESCAPE);
}

/* Wait for the next frame ($C1:007E) from the call at `site`: the wait reads
 * none of the registers but A's high byte, which it takes along. */
enum { FRAME_WAIT_READS = DC_AL | DC_X | DC_Y | DC_FLAGS };

static bool wait_for_the_next_frame(Snes *s, uint32_t site)
{
    return battle_wait_frame(s, site, 0xC1007E, FRAME_WAIT_READS);
}

/* The combatants animate a step, whose group goes by the frame's phase; the
 * phase is kept around it, the animations' step moving it on where it ends
 * a group early. The step runs as C where its entry's guard would take it
 * (battle_steps_animations): A's high byte clear, which it indexes with, and
 * the party's palettes the game's; otherwise it is the original's. */
static bool animate_the_combatants(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_frame_phase_saved(g, battle_frame_phase(g));
    if (!battle_steps_animations(s)) {
        if (!battle_call(s, 0xC110A9, 0xC12D9F, DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C))
            return false;
    } else {
        TIMED_AS(g, 0xC12D9F, battle_step_combatants(g));
    }
    set_battle_frame_phase(g, battle_frame_phase_saved(g));
    return true;
}

/* The enemies' effects run their step, and the combatants' boxes follow
 * where they stand. */
static bool step_the_enemies_effects(Snes *s, uint32_t site)
{
    if (!battle_run(s, site, 0xC13714, battle_step_enemy_effects))
        return false;
    GameState *g = state_of(s);
    TIMED_AS(g, 0xC1283D, battle_update_all_bounds(g));
    return true;
}

// $C1:106E
/* A frame of the battle waiting: the enemies' orbits go on unless the
 * defeat is showing, L and R held where the party can't run say so; then
 * two frames, the first animating the combatants and the enemies' effects,
 * the queued uploads held after it, the second turning them back on, giving
 * the menu its turn (the buttons it took forgotten) and the effects another
 * step. */
static bool wait_step(Snes *s)
{
    GameState *g = state_of(s);
    if (!battle_defeat_showing(g)) {
        set_battle_orbits_held(g, 0);
        if (!refuse_to_run(s))
            return false;
    }
    if (!wait_for_the_next_frame(s, 0xC1109D) || !finish_the_frame(s, 0xC110A0) || !animate_the_combatants(s)
        || !step_the_enemies_effects(s, 0xC110B5))
        return false;
    set_battle_queued_uploads_on(g, 0);
    if (!wait_for_the_next_frame(s, 0xC110BD) || !finish_the_frame(s, 0xC110C0))
        return false;
    set_battle_queued_uploads_on(g, (uint8_t)(battle_queued_uploads_on(g) + 1));
    if (menu_turn_due(g)) {
        if (!take_the_menu_turn(s, (MenuCalls){ 0xC110D1, 0xC110D4, 0xC110D7 }))
            return false;
        set_battle_menu_buttons(g, 0);
    }
    return step_the_enemies_effects(s, 0xC110DC);
}

/* The step ends with the combatants' boxes worked out, which leaves A's high
 * byte clear and the carry of the last test, set, for the main loop. */
void battle_wait_step(Snes *s)
{
    if (!wait_step(s))
        return;
    s->cpu.a = 0;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

void battle_next_member_takes_the_menu_entry(Snes *s)
{
    next_member_takes_the_menu(state_of(s));
    m_rts(s);
}

/* The guards. The menu's turn finds its member's cells by the whole of A
 * with the queue's first, so the queue's first is a party member or nobody
 * and A's high byte clear, as the game has them, where the turn is due. */
static bool queue_fits(Snes *s)
{
    uint8_t member = battle_menu_queue(state_of(s), 0);
    return (s->cpu.a & 0xFF00) == 0 && ((member & NOBODY_WAITING) || battle_slot_is_party(member));
}

bool battle_menu_queue_ready(Snes *s)
{
    vars_peek(true);
    bool fits = queue_fits(s);
    vars_peek(false);
    return battle_on_page(s) && fits;
}

bool battle_frame_step_ready(Snes *s)
{
    vars_peek(true);
    bool fits = !menu_turn_due(state_of(s)) || queue_fits(s);
    vars_peek(false);
    return battle_on_page(s) && fits;
}
