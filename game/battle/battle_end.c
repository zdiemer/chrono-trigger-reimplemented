/* Battle: what the battle puts right as it ends.
 *
 * However a battle ends -- won, lost, run from, or a demo's end -- the party
 * leaves it as it goes back to the field: every member's statuses fall away,
 * the knocked-out members stand again, and each member's stat block is kept
 * in its character's record, which is how the HP and MP it has carry over.
 * The enemies leave the field with the battle, all but those marked to stay
 * there (the field puts those back where they stood, battle_enemy_placement's
 * kind); at a demo's end the enemies still standing stay; and when the party
 * runs, the enemies count as out. Last, the enemies beaten are noted for the
 * story, which the events read.
 *
 * The main loop (battle/main_loop.c) runs these at the battle's end, each where
 * the original calls its routine. */
#include "charge.h"
#include "battle/battle_end.h"

#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/effect_parts.h"
#include "battle/entries.h"
#include "vars.h"

/* ---- The party ------------------------------------------------------------------------------ */

/* The original makes each member the actor in turn as it goes over the
 * party, finding its stat block by a multiply (the actor's block,
 * battle_effect_actor_block), so the last member is left the actor. */
static void leave_last_member_acting(GameState *g)
{
    enum { LAST_MEMBER = BATTLE_PARTY_SIZE - 1 };
    set_battle_actor(g, LAST_MEMBER);
    set_battle_effect_actor_block(g, effect_times(g, BATTLE_STAT_BLOCK_BYTES, LAST_MEMBER));
}

/* The block's bytes past the four status bytes the clearing takes too: what
 * the member shrugs off and ignores, the next byte, and the statuses it keeps
 * whatever happens. */
enum { STAT_AFTER_STATUSES = 0x24 };

// $C1:EA9D
/* Every status falls away from the party, the ones its equipment keeps
 * included, with what it shrugs off; a knocked-out member stays out for now.
 * The low HP noted goes, and so does a member's mark to be revived. */
void battle_clear_party_statuses(GameState *g)
{
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_stats_status(g, member, 0);
        set_battle_stats_block_byte(g, member, STAT_SHRUGS_OFF, 0);
        set_battle_stats_status_ignored(g, member, 0);
        set_battle_stats_block_byte(g, member, STAT_AFTER_STATUSES, 0);
        set_battle_stats_status_kept(g, member, 0);
        set_battle_stats_condition(g, member, battle_stats_condition(g, member) & CONDITION_KNOCKED_OUT);
        combatant_forget_low_hp(g, member);
        set_battle_member_revive_marks(g, member, 0);
    }
    leave_last_member_acting(g);
}

// $C1:EAE8
/* The knocked-out members stand again, with what HP they have. */
void battle_stand_party_up(GameState *g)
{
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_stats_condition(g, member, 0);
    leave_last_member_acting(g);
}

/* A character's record is a stat block's first $50 bytes. */
enum { RECORD_BYTES = 0x50, NO_CHARACTER = 0xFF };

static void keep_record(GameState *g, uint8_t member, uint8_t character)
{
    for (unsigned at = 0; at < RECORD_BYTES; at++)
        set_character_records_record_byte(g, character, at, battle_stats_block_byte(g, member, at));
}

/* A party slot holding a character past the seven, which the game never
 * makes: the original's indexed stores copy the block on past the records,
 * over whatever follows them. Kept as the original has it, in this one
 * place: a layout of the records other than the original's decides here what
 * such a slot does. */
static void copy_past_the_records(GameState *g, uint8_t member, uint8_t character)
{
    for (unsigned at = 0; at < RECORD_BYTES; at++)
        set_character_records_record_byte_unchecked(g, character, at, battle_stats_block_byte(g, member, at));
}

// $C1:B442
/* Each member's stat block is kept in its character's record. */
void battle_keep_party_records(GameState *g)
{
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t character = party_members(g, member);
        if (character == NO_CHARACTER)
            continue;
        /* the original finds the record by a multiply */
        battle_leave_multiply(g, RECORD_BYTES, character);
        if (character < CHARACTER_RECORDS_COUNT)
            keep_record(g, member, character);
        else
            copy_past_the_records(g, member, character);
    }
    /* its count of the members, kept in the cell the clock modifier reads */
    battle_keep_word(g, 0, BATTLE_PARTY_SIZE);
}

/* ---- The enemies ---------------------------------------------------------------------------- */

static unsigned slot_of(unsigned enemy) { return BATTLE_FIRST_ENEMY + enemy; }

// $C1:B3BB
/* The enemies leave the field with the battle, but for those marked to stay. */
void battle_take_enemies_away(GameState *g)
{
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        set_battle_enemy_placement_kind(g, enemy, battle_enemy_placement_kind(g, enemy) & BATTLE_ENEMY_STAYS_ON_THE_FIELD);
}

// $C1:B3D2
/* At a demo battle's end, the enemies still on the battlefield stay. */
void battle_keep_standing_enemies(GameState *g)
{
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        if (battle_combatant_id(g, slot_of(enemy)) == COMBATANT_NONE || (battle_enemy_standing(g, enemy) & ENEMY_IN_RESERVE))
            continue;
        set_battle_enemy_placement_kind(g, enemy, BATTLE_ENEMY_STAYS_ON_THE_FIELD);
    }
}

// $FD:AA98
/* The party ran: every enemy counts as out. */
void battle_count_enemies_out(GameState *g)
{
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        set_battle_stats_condition(g, slot_of(enemy), CONDITION_KNOCKED_OUT);
}

// $FD:AAB0
/* The enemies beaten in the battle are noted for the story, a bit each from
 * the first enemy's in bit 0. */
void battle_note_beaten_enemies(GameState *g)
{
    uint8_t beaten = 0;
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        if (battle_enemy_beaten_counted(g, enemy))
            beaten |= (uint8_t)(1u << enemy);
    set_battle_enemies_beaten_for_the_story(g, beaten);
    /* the bit it was at, shifted out of a byte in the clock modifier's cell */
    battle_keep_byte(g, 0, 0);
}

