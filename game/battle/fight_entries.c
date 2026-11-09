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

/* A party whose every member is empty or one of the characters, and whose
 * fight is listed: what the pieces after the listing take for granted. */
static bool party_real(Snes *s)
{
    vars_peek(true);
    bool real = setup_party_is_real(state_of(s));
    vars_peek(false);
    return real;
}

bool battle_fight_can_start(const GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t character = battle_combatant_fighting(g, member);
        if (character != COMBATANT_NONE && character >= SETUP_CHARACTERS)
            return false;
    }
    return true;
}

// $C1:FA8B

bool battle_start_fight_ready(Snes *s) { return battle_on_page(s) && party_real(s); }

void battle_start_fight_entry(Snes *s)
{
    battle_start_fight(state_of(s));
    m_rts(s);
}

// $FD:B2DE, $FD:B33F

// $FD:B22E

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
