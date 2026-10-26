/* Battle: the party's line for the command menu.
 *
 * Only one member at a time chooses its action from the menu, so the members
 * whose turns have come wait in line for it:
 *
 * - a member whose turn comes (the battle's step 1, its gauge full) joins the
 *   line, unless it has a slot of the menu already, its command cursor where
 *   the battle remembers it; a chime says its turn has come;
 * - every fourth frame the menu's turn (frame_step.c) lets the first in line
 *   take the menu: the slot of its own place, its window built afresh;
 * - a member whose action is called off (the battle's step 2: it can't act
 *   any more, the party stands down, the battle ends) steps out of the line,
 *   or gives its slot up; where its menu was the one open, the menu goes on
 *   to the next member holding a slot, or closes, and the targets it was
 *   choosing are let go.
 *
 * The line is battle_menu_queue, as long as battle_menu_queue_length; the
 * slots are battle_menu_slot_members, "nobody" kept in-band (window.h). */
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/choosing.h"
#include "battle/entries.h"
#include "battle/program.h"
#include "battle/steps.h"
#include "battle/window.h"
#include "charge.h"
#include "original_effects.h"
#include "platform/audio.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* The window is to be built afresh for the member, whichever slot it shows
 * (window.h). */
enum { WINDOW_TO_REBUILD = NO_SLOT_SHOWN };

// $C1:1B19
/* A party member's turn has come: unless it has a slot of the menu already,
 * it joins the line, its record marked (MEMBER_ACTION_CHOSEN, which the menu
 * clears as it queues the action chosen) and its command cursor where the
 * battle remembers it; and the chime of a turn come plays, from the middle. */
enum { TURN_CHIME = 0x42 };
#define DRIVER_IGNORES (DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C)

static void join_the_line(GameState *g, Platform *pf, uint8_t member)
{
    if (menu_slot_has_member(g, member))
        return;
    set_battle_member_actions_state(g, member, (uint8_t)(battle_member_actions_state(g, member) | MEMBER_ACTION_CHOSEN));
    uint8_t place = battle_menu_queue_length(g);
    set_battle_menu_queue(g, place, member);
    set_battle_command_cursor(g, member, battle_remembered_command(g, member));
    set_battle_menu_queue_length(g, (uint8_t)(place + 1));
    au_command(pf, PF_CALL_DC(0xC11B50, 0xC70004, DRIVER_IGNORES),
               (AudioCommand){ .command = AU_START_OTHER_EFFECT, .p1 = TURN_CHIME, .p2 = AU_CENTRE, .count = 2 });
}

// $C1:1B67
/* The member first in line for the menu takes it: its choice starts clean,
 * its tech list's cursor and scroll as the battle remembers them, it takes
 * the menu slot of its own place, and the window is built afresh; where no
 * slot is current, its slot is. The rest of the line moves up, as the
 * original moves it: the second place takes the third's member, and the
 * fourth too, the third keeping its own. */
void battle_next_member_takes_the_menu(GameState *g)
{
    uint8_t member = battle_menu_queue(g, 0);
    if (menu_nobody(member))
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
    if (menu_nobody(battle_menu_slot(g)))
        set_battle_menu_slot(g, member);
}

/* The member steps out of the line, the members behind it moving up a
 * place (the last place taking what lies past the line); one not in the
 * first three places isn't found. */
enum { PLACES_SEARCHED = 3 };

static void step_out_of_the_line(GameState *g, uint8_t member)
{
    unsigned place = 0;
    while (place < PLACES_SEARCHED && battle_menu_queue(g, place) != member)
        place++;
    if (place == PLACES_SEARCHED)
        return;
    for (; place < PLACES_SEARCHED; place++)
        set_battle_menu_queue(g, place, battle_menu_queue(g, place + 1));
    set_battle_menu_queue_length(g, (uint8_t)(battle_menu_queue_length(g) - 1));
}

void battle_hand_the_menu_on(GameState *g, uint8_t member)
{
    set_battle_menu_page(g, PAGE_COMMANDS);
    set_battle_menu_slot_shown(g, WINDOW_TO_REBUILD);
    set_battle_menu_slot_members(g, member, MENU_NONE);
    uint8_t next = MENU_NONE;
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++)
        if (menu_slot_has_member(g, slot)) {
            next = battle_menu_slot_members(g, slot);
            break;
        }
    set_battle_menu_slot(g, next);
    set_battle_menu_member(g, next);
    set_battle_timers_held(g, 0);
}

/* The member gives up the menu that was open: the targets it was choosing
 * are let go (and the page the party was shown from), and the menu goes on. */
static void give_up_the_open_menu(GameState *g, uint8_t member)
{
    if (party_shown_for_targets(g)) {
        forget_page_kept_for_targets(g);
        set_battle_ally_target_window(g, 0);
    }
    set_battle_targeting(g, 0);
    ORIGINAL_EFFECT(set_battle_target_choice_spare(g, 0));
    set_battle_target_choice(g, 0);
    battle_hand_the_menu_on(g, member);
}

/* The work cell the original keeps the member in as it calls the menu off. */
enum { CALLED_OFF_CELL = 0x80 };

// $C1:1BAA
/* A member's action is called off: out of the line, or out of its slot of
 * the menu, the window's frames put back as the battle starts them. */
void battle_call_off_the_menu(GameState *g, uint8_t member)
{
    ORIGINAL_EFFECT(battle_leave_work8(g, CALLED_OFF_CELL, member));
    if (!menu_slot_has_member(g, member)) {
        step_out_of_the_line(g, member);
        return;
    }
    set_battle_menu_member_count(g, (uint8_t)(battle_menu_member_count(g) - 1));
    if (battle_menu_slot_members(g, member) == battle_menu_member(g)) {
        give_up_the_open_menu(g, member);
    } else {
        set_battle_menu_slot_members(g, member, MENU_NONE);
        set_battle_menu_slot_shown(g, WINDOW_TO_REBUILD);
    }
    TIMED_AS(g, 0xC11C3A, battle_reset_frames(g));
    set_battle_tech_list_being_laid_out(g, 0);
}

/* ---- The entries ---------------------------------------------------------------------------- */

/* The steps' entries take their member from the step's cell; the dispatch
 * hands the asker's registers back after them (steps.c), but not A's high
 * byte and the carry, which the asker reads: the turn's step leaves them as
 * the dispatch gave them, the calling off A's high byte clear and the carry
 * of its last count, set. */

// $C1:1B19
void battle_turn_comes_step(Snes *s)
{
    GameState *g = state_of(s);
    join_the_line(g, m_platform(s), battle_step_member(g));
    m_rts(s);
}

// $C1:1BAA
void battle_called_off_step(Snes *s)
{
    GameState *g = state_of(s);
    battle_call_off_the_menu(g, battle_step_member(g));
    s->cpu.a &= 0x00FF;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C1:1B67
void battle_next_member_takes_the_menu_entry(Snes *s)
{
    battle_next_member_takes_the_menu(state_of(s));
    m_rts(s);
}

/* The guards: a party member, as the steps are asked for, with a place in
 * the line for it to join; the turn's step finds the member's cells by the
 * whole of A with the member, so A's high byte clear, as the main loop asks
 * for it. */
bool battle_turn_comes_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = battle_step_member(g) < BATTLE_PARTY_SIZE && battle_menu_queue_length(g) < BATTLE_MENU_QUEUE_COUNT;
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && ok;
}

bool battle_called_off_ready(Snes *s)
{
    vars_peek(true);
    bool ok = battle_step_member(state_of(s)) < BATTLE_PARTY_SIZE;
    vars_peek(false);
    return battle_on_page(s) && ok;
}

/* ---- As C ------------------------------------------------------------------------------------ */

/* The steps with their guards' state halves: a party member, and for the
 * turn a place in the line for it to join. */
bool battle_turn_comes(GameState *g, Platform *pf)
{
    if (battle_step_member(g) >= BATTLE_PARTY_SIZE || battle_menu_queue_length(g) >= BATTLE_MENU_QUEUE_COUNT)
        return battle_needs_the_machine(g, 0xC11B19, "a turn come with no place in the line");
    join_the_line(g, pf, battle_step_member(g));
    return true;
}

bool battle_menu_called_off(GameState *g, Platform *pf)
{
    (void)pf;
    if (battle_step_member(g) >= BATTLE_PARTY_SIZE)
        return battle_needs_the_machine(g, 0xC11BAA, "a menu called off for a member past the party");
    battle_call_off_the_menu(g, battle_step_member(g));
    return true;
}
