/* Battle: what the window's routines leave in the registers for the
 * original's code (battle/window_leaves.h). */
#include "battle/window_leaves.h"

#include "vars.h"

/* The redraw leaves the carry of its last compare: of the last enemy kind's
 * index (set) where nobody is choosing or the menu is on another page; of the
 * ally target against the one marked (set where they are the same); of
 * doubling the ally target where its name is marked; and clear from the
 * command cursor's place where it draws the command windows. */
bool window_redraw_carry(const GameState *g)
{
    if (battle_menu_member_count(g) == 0)
        return true;
    if (battle_ally_target_page(g)) {
        uint8_t target = battle_targets(g, 0);
        return target == battle_ally_target_marked(g) || menu_nobody(target);
    }
    return battle_menu_page(g) != PAGE_COMMANDS;
}

PartyStatusNote note_party_status(const GameState *g)
{
    return (PartyStatusNote){ .numbers = !battle_status_numbers_covered(g, battle_window_layout_now(g)),
                              .went_round = (uint8_t)(battle_status_member(g) + 1) >= BATTLE_PARTY_SIZE };
}

static bool member_absent(const GameState *g, PartyStatusNote note)
{
    return note.numbers && !battle_slot_present(g, battle_status_member(g));
}

/* A member not in the fight leaves the carry of counting the turn on (set
 * where it went round); the gauges leave that of their last count (set); and
 * otherwise the MP's tens, compared with the zero, do. */
bool party_status_carry(const GameState *g, PartyStatusNote note)
{
    if (member_absent(g, note))
        return note.went_round;
    return layout_has_gauges(battle_window_layout_now(g)) || battle_digits(g, BATTLE_DIGIT_TENS) >= TILE_DIGIT_ZERO;
}

bool party_status_clears_a(const GameState *g, PartyStatusNote note) { return !member_absent(g, note); }
