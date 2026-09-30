/* Battle: the fight's start's machine side -- each piece's registered entry,
 * taking what the original's callers hand it in the registers and the
 * direct page's cells, and the guards that turn away what the game never
 * hands it (battle/fight_start.c says what the pieces are). */
#include "battle/fight.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/setup.h"
#include "port/machine.h"
#include "vars.h"

/* The index registers, as the callers hand the pieces their numbers. */
static uint16_t index_x(const Snes *s) { return s->cpu.x; }
static uint16_t index_y(const Snes *s) { return s->cpu.y; }

/* A party whose every member is empty or one of the characters, and whose
 * fight is listed: what the pieces after the listing take for granted. */
static bool party_real(Snes *s)
{
    vars_peek(true);
    bool real = setup_party_is_real(state_of(s));
    vars_peek(false);
    return real;
}

static bool members_fighting_real(const GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t character = battle_combatant_fighting(g, member);
        if (character != COMBATANT_NONE && character >= SETUP_CHARACTERS)
            return false;
    }
    return true;
}

static bool fighting_real(Snes *s)
{
    vars_peek(true);
    bool real = members_fighting_real(state_of(s));
    vars_peek(false);
    return real;
}

static bool enemies_listed(Snes *s)
{
    vars_peek(true);
    uint8_t count = battle_enemy_count(state_of(s));
    vars_peek(false);
    return count >= 1 && count <= BATTLE_ENEMIES;
}

// $C1:FA8B
bool battle_start_fight_ready(Snes *s) { return battle_on_page(s) && party_real(s); }

void battle_start_fight_entry(Snes *s)
{
    battle_start_fight(state_of(s));
    m_rts(s);
}

// $FD:B2DE, $FD:B33F
void fight_load_demo_party_entry(Snes *s)
{
    fight_load_demo_party(state_of(s));
    m_rtl(s);
}

void fight_start_demo_characters_entry(Snes *s)
{
    fight_start_demo_characters(state_of(s));
    m_rts(s);
}

// $FD:B22E
bool fight_list_combatants_ready(Snes *s) { return battle_on_page(s) && party_real(s); }

void fight_list_combatants_entry(Snes *s)
{
    fight_list_combatants(state_of(s));
    m_rtl(s);
}

/* An enemy in X, and the same in the cell the original finds its block by
 * ($02). */
enum { ENEMY_CELL = 0x02 };

bool fight_load_enemy_ready(Snes *s)
{
    vars_peek(true);
    uint16_t cell = battle_cell_word(state_of(s), ENEMY_CELL);
    vars_peek(false);
    return battle_on_page(s) && index_x(s) < BATTLE_ENEMIES && cell == index_x(s);
}

/* It leaves in A the count of the record's last part, its three bytes, which
 * its caller's next pieces find there. */
enum { LAST_PART_BYTES = 3 };

void fight_load_enemy_entry(Snes *s)
{
    fight_load_enemy(state_of(s), (uint8_t)index_x(s));
    s->cpu.a = LAST_PART_BYTES;
    m_rtl(s);
}

/* An enemy's block by its offset from the first enemy's, in Y; the registers
 * come back as they were, and A clear. */
bool fight_clear_enemy_block_ready(Snes *s)
{
    return battle_on_page(s) && index_y(s) % BATTLE_STAT_BLOCK_BYTES == 0 && index_y(s) / BATTLE_STAT_BLOCK_BYTES < BATTLE_ENEMIES;
}

void fight_clear_enemy_block_entry(Snes *s)
{
    fight_clear_enemy_block(state_of(s), (uint8_t)(index_y(s) / BATTLE_STAT_BLOCK_BYTES));
    s->cpu.a = 0;
    m_rts(s);
}

void fight_note_enemy_families_entry(Snes *s)
{
    fight_note_enemy_families(state_of(s));
    m_rtl(s);
}

/* A member in Y, fighting as one of the characters or out of the fight. */
bool fight_member_in_y_ready(Snes *s) { return battle_on_page(s) && index_y(s) < BATTLE_PARTY_SIZE && fighting_real(s); }

void fight_take_member_record_entry(Snes *s)
{
    fight_take_member_record(state_of(s), (uint8_t)index_y(s));
    m_rtl(s);
}

/* Ayla's level in A, a byte, with the carry her caller's test leaves set; her
 * fists come back in A's low byte. */
bool fight_aylas_fists_ready(Snes *s) { return battle_on_page(s) && s->cpu.a < 0x100 && m_flag(s, CPU_FLAG_C); }

void fight_aylas_fists_entry(Snes *s)
{
    uint8_t fists = fight_aylas_fists(state_of(s), (uint8_t)s->cpu.a);
    m_set_a8(s, fists);
    m_rts(s);
}

/* A member in X, its block's offset from the first in Y. */
bool fight_set_up_affinities_ready(Snes *s)
{
    return battle_on_page(s) && index_x(s) < BATTLE_PARTY_SIZE && index_y(s) == index_x(s) * BATTLE_STAT_BLOCK_BYTES;
}

void fight_set_up_affinities_entry(Snes *s)
{
    fight_set_up_affinities(state_of(s), (uint8_t)index_x(s));
    m_rtl(s);
}

void fight_note_accessories_entry(Snes *s)
{
    fight_note_accessories(state_of(s));
    m_rtl(s);
}

void fight_note_weapons_entry(Snes *s)
{
    fight_note_weapons(state_of(s));
    m_rtl(s);
}

void fight_clear_timers_entry(Snes *s)
{
    fight_clear_timers(state_of(s));
    m_rtl(s);
}

void fight_set_timer_lengths_entry(Snes *s)
{
    fight_set_timer_lengths(state_of(s));
    m_rtl(s);
}

bool fight_set_first_turns_ready(Snes *s) { return battle_on_page(s) && enemies_listed(s); }

void fight_set_first_turns_entry(Snes *s)
{
    fight_set_first_turns(state_of(s));
    m_rtl(s);
}

void fight_apply_accessory_effects_entry(Snes *s)
{
    fight_clear_enemy_animations(state_of(s));
    fight_apply_equipment_effects(state_of(s));
    m_rtl(s);
}

/* A member's block by its offset from the first, in X. */
bool fight_raise_critical_rate_ready(Snes *s)
{
    return battle_on_page(s) && index_x(s) % BATTLE_STAT_BLOCK_BYTES == 0 && index_x(s) / BATTLE_STAT_BLOCK_BYTES < BATTLE_PARTY_SIZE;
}

void fight_raise_critical_rate_entry(Snes *s)
{
    fight_raise_critical_rate(state_of(s), (uint8_t)(index_x(s) / BATTLE_STAT_BLOCK_BYTES));
    m_rtl(s);
}

void fight_clear_turn_state_entry(Snes *s)
{
    fight_clear_turn_state(state_of(s));
    m_rtl(s);
}

void fight_apply_member_bonuses_entry(Snes *s)
{
    fight_apply_member_bonuses(state_of(s), (uint8_t)index_y(s));
    m_rtl(s);
}

void fight_number_places_entry(Snes *s)
{
    fight_number_places(state_of(s));
    m_rtl(s);
}

void fight_ward_off_confusion_entry(Snes *s)
{
    fight_ward_off_confusion(state_of(s));
    m_rtl(s);
}
