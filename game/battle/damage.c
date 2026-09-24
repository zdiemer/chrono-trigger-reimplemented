/* Battle: what an action does, cleared between actions.
 *
 * An action is worked out into the damage table -- what each of its (up to
 * three) hits does to each combatant, which the original's effect routines
 * fill and apply -- and shown as the numbers that rise over the combatants it
 * hit (battle/numbers.c). The main loop clears the table each pass, and the
 * numbers once the action is over. */
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "vars.h"

// $FD:ACFD
static void clear_damage(GameState *g)
{
    for (unsigned hit = 0; hit < BATTLE_DAMAGE_COUNT; hit++) {
        set_battle_damage_amount(g, hit, 0);
        set_battle_damage_kind(g, hit, 0);
        set_battle_damage_flags(g, hit, 0);
    }
}

// $FD:ACEE
static void clear_numbers(GameState *g)
{
    set_battle_effect_tally(g, 0);
    for (unsigned record = 0; record < BATTLE_NUMBER_RECORDS_COUNT; record++) {
        set_battle_number_records_value(g, record, 0);
        set_battle_number_records_kind(g, record, 0);
        set_battle_number_records_last_byte(g, record, 0);
    }
}

/* Both leave A 0 and the carry of their loop's last compare, set, which
 * their callers read. */
void battle_clear_damage(Snes *s)
{
    clear_damage(state_of(s));
    s->cpu.a = 0;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rtl(s);
}

void battle_clear_numbers(Snes *s)
{
    clear_numbers(state_of(s));
    s->cpu.a = 0;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rtl(s);
}
