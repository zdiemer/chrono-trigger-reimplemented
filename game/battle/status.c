/* Battle: keeping the party's numbers and gauges up to date as the fight goes.
 *
 * Between whole redraws (window.c) the battle window is kept current a little
 * at a time: each frame the menu redraws one member's HP and MP, taking the
 * members in turn, and in the layouts with gauges every member's gauge. A
 * gauge is four cells that fill, eight steps a cell, as the member's wait for
 * its turn runs out; it is shaded while the wait lasts. */
#include "battle/window.h"

#include "battle/arith.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/window_leaves.h"
#include "original_effects.h"
#include "vars.h"

/* ---- The gauges ------------------------------------------------------------------------ */

/* A gauge's cells: empty, full, or part full ($67 plus how far). */
enum { GAUGE_CELLS = 4, STEPS_PER_CELL = 8, GAUGE_STEPS = GAUGE_CELLS * STEPS_PER_CELL };
enum { TILE_GAUGE_EMPTY = 0x67, TILE_GAUGE_FULL = 0x6F };

/* The work cells the original keeps the gauge's numbers in: the steps passed
 * after the full cells, the full cells (counted down), their steps; and the
 * wait and the full wait it hands the divider, a word each, and the steps
 * left. */
enum { PART_STEPS = 0x82, FULL_CELLS = 0x83, FULL_STEPS = 0x86 };
enum { WAIT = 0, FULL_WAIT_HIGH = 3, STEPS_LEFT = 4 };

static void leave_gauge_cells(GameState *g, uint8_t wait, uint16_t left, uint8_t passed)
{
    uint8_t full_steps = (uint8_t)(passed / STEPS_PER_CELL * STEPS_PER_CELL);
    set_battle_multiply_factors(g, WAIT, wait);
    set_battle_multiply_factors(g, WAIT + 1, 0);
    set_battle_division(g, FULL_WAIT_HIGH, 0);
    set_battle_division(g, STEPS_LEFT, (uint8_t)left);
    set_battle_division(g, STEPS_LEFT + 1, (uint8_t)(left >> 8));
    battle_leave_work8(g, PART_STEPS, (uint8_t)(passed - full_steps));
    battle_leave_work8(g, FULL_CELLS, 0);
    battle_leave_work8(g, FULL_STEPS, full_steps);
}

// $C1:06F0
/* The gauge shows how much of the member's wait has passed: the wait left is
 * worked out as a share of the full wait in 256ths, and then in the gauge's
 * steps, and what passed is the rest of the steps, all in bytes. A wait
 * longer than the full one, or a full wait of 0, runs the full cells on past
 * the gauge's end, as it does in the original. */
void battle_show_gauge(GameState *g, unsigned member)
{
    WindowCell at = (WindowCell)(battle_member_line(LAYOUT_NUMBERS_AFTER, member).gauge + 1);
    uint8_t wait = battle_gauge_wait(g, member);
    BattleQuotient share_left = battle_divide_by(g, (uint16_t)(wait << 8), battle_gauge_wait_full(g, member));
    uint16_t left = (uint16_t)(share_left.quotient / (256 / GAUGE_STEPS));
    uint8_t passed = (uint8_t)(GAUGE_STEPS - (uint8_t)left);
    uint8_t full_cells = passed / STEPS_PER_CELL, part = passed % STEPS_PER_CELL;
    for (unsigned i = 0; i < GAUGE_CELLS; i++)
        window_put(g, (WindowCell)(at + i), TILE_GAUGE_EMPTY, wait ? TEXT_SHADED : TEXT_PLAIN);
    for (unsigned i = 0; i < full_cells; i++)
        window_set_tile(g, (WindowCell)(at + i), TILE_GAUGE_FULL);
    if (part)
        window_set_tile(g, (WindowCell)(at + full_cells), (uint8_t)(TILE_GAUGE_EMPTY + part));
    ORIGINAL_EFFECT(leave_gauge_cells(g, wait, left, passed));
}

/* ---- A member's numbers, in turn ---------------------------------------------------------- */

/* In the second layout the numbers sit where the third menu slot's command
 * window goes, so while it is up (unless the window was rebuilt for aiming
 * at a party member, which shows no command windows) only the gauges are
 * redrawn. */
bool battle_status_numbers_covered(const GameState *g, WindowLayout layout)
{
    return !(battle_menu_slot_members(g, 2) & 0x80) && !battle_ally_target_window(g) && layout == LAYOUT_NUMBERS_FIRST;
}

/* The member whose numbers are redrawn next: each in turn. */
static unsigned next_status_member(GameState *g)
{
    uint8_t member = (uint8_t)(battle_status_member(g) + 1);
    if (member >= BATTLE_PARTY_SIZE)
        member = 0;
    set_battle_status_member(g, member);
    return member;
}

/* The original keeps the member in a work cell, a word, and where its HP goes
 * (bytes); and the loop over the gauges keeps its index there too. */
enum { MEMBER = 0x80, HP_PLACE = 0x86, STAT_BLOCK_BYTES = 0x80 };

static void leave_member_cells(GameState *g, unsigned member, WindowLayout layout, bool shaded)
{
    battle_leave_work16(g, MEMBER, (uint16_t)member);
    battle_leave_work16(g, HP_PLACE, (uint16_t)(2 * battle_member_line(layout, member).hp));
    set_battle_member_stat_block(g, (uint16_t)(member * STAT_BLOCK_BYTES));
    set_battle_digit_highlight(g, shaded);
}

// $C1:05A7
void battle_show_party_status(GameState *g)
{
    WindowLayout layout = battle_window_layout_now(g);
    if (!battle_status_numbers_covered(g, layout)) {
        unsigned member = next_status_member(g);
        if (!battle_slot_present(g, member)) {
            ORIGINAL_EFFECT(battle_leave_work16(g, MEMBER, (uint16_t)member));
            return;
        }
        bool shaded = member_numbers_shaded(g, member);
        window_show_hp(g, layout, member, shaded);
        window_show_mp(g, layout, member, shaded);
        ORIGINAL_EFFECT(leave_member_cells(g, member, layout, shaded));
    }
    if (!layout_has_gauges(layout))
        return;
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (battle_slot_present(g, member))
            battle_show_gauge(g, member);
    ORIGINAL_EFFECT(battle_leave_work16(g, MEMBER, BATTLE_PARTY_SIZE));
}

/* ---- The entries ------------------------------------------------------------------------ */

/* The member in its work cell, a word. It leaves A's high byte clear (the
 * divider's routine's last act). */
void battle_draw_gauge(Snes *s)
{
    GameState *g = state_of(s);
    battle_show_gauge(g, battle_work16(g, MEMBER));
    s->cpu.a = 0;
    m_rts(s);
}

/* It leaves the carry, and A, as window_leaves.c works them out. */
void battle_draw_party_status(Snes *s)
{
    GameState *g = state_of(s);
    vars_peek(true);
    PartyStatusNote note = note_party_status(g);
    vars_peek(false);
    battle_show_party_status(g);
    vars_peek(true);
    bool carry = party_status_carry(g, note), clears_a = party_status_clears_a(g, note);
    vars_peek(false);
    m_set_flag(s, CPU_FLAG_C, carry);
    if (clears_a)
        s->cpu.a = 0;
    m_rts(s);
}
