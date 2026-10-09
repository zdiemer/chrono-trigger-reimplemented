/* Battle: stealing (battle/effect_parts.h).
 *
 * An enemy has two items a steal can take, the one it drops and its charm,
 * and marks which of them it still has (battle_steal_marks). A steal lands
 * with a percent's chance, three times better for a thief wearing the
 * accessory that says so; then the steal's modifier draws the charm's chance,
 * and the steal takes the charm where that came and the enemy still has it,
 * or else the item it drops. What it takes goes to the party: most items to
 * the inventory (battle/inventory.c), and the few the item records mark so to
 * the member who stole them, kept apart until the battle ends. How the steal
 * came out is kept for the member's turn to show (battle_steal_outcome). */
#include "battle/effect_parts.h"

#include "battle/combatant.h"
#include "battle/inventory.h"
#include "battle/random.h"
#include "charge.h"

/* The accessory that makes its wearer's steals three times as likely to land. */
enum { THIEFS_ACCESSORY = 0xA5 };


/* The original hands the outcome back in the cell it took the chance in
 * ($16), and the item taken in $06, where the member's turn reads it to name
 * in its message (battle.h, kept plainly). */
enum { ENEMY_PLACE_CELL = 0x0C };

/* How the steal came out, kept for the member's turn (battle_steal_outcome)
 * and handed back: a miss and nothing to take as themselves; an item taken as
 * its giving left it, and 0 handed back. */
static StealOutcome came_out(GameState *g, StealOutcome outcome)
{
    uint8_t kept = outcome == STEAL_MISSED    ? STEAL_OUTCOME_MISSED
                   : outcome == STEAL_NOTHING ? STEAL_OUTCOME_NOTHING
                                              : battle_steal_outcome(g);
    set_battle_steal_outcome(g, kept);
    ORIGINAL_EFFECT(battle_leave_byte(g, EFFECT_ARG_1, outcome == STEAL_TOOK ? 0 : kept));
    return outcome;
}

static uint8_t marks_of(const GameState *g, uint8_t slot)
{
    return slot < BATTLE_STEAL_MARKS_COUNT ? battle_steal_marks(g, slot) : battle_steal_marks_unchecked(g, slot);
}
static void set_marks_of(GameState *g, uint8_t slot, uint8_t marks)
{
    if (slot < BATTLE_STEAL_MARKS_COUNT)
        set_battle_steal_marks(g, slot, marks);
    else
        set_battle_steal_marks_unchecked(g, slot, marks);
}

// $FD:B655
/* The stolen item goes to the party: an item from $BC on that its record
 * marks so, to the thief, kept apart until the battle ends; any other to the
 * inventory. The giving clears the flag the battle's code shares
 * (battle_action_came_to_nothing), which the member's turn reads after. The
 * original works in the direct page's cells as it goes: the thief's record
 * by a multiply, the item's record's place, which the scripts' stat test may
 * read after ($08 and $0A, kept plainly), and $00, which the clock's modifier
 * may read (battle.h). */
enum { KEPT_ITEM_BYTES = 8, RECORD_PLACE_CELL = 0x02, THIEF_RECORD_CELL = 0x04 };

/* One of an item kept for a party member until the battle ends, with the
 * detail and kind its record gives it. */
static void battle_member_keep_item(GameState *g, uint8_t member, uint8_t item, uint8_t detail, uint8_t kind)
{
    set_battle_member_kept_item_id(g, member, item);
    set_battle_member_kept_item_detail(g, member, detail);
    set_battle_member_kept_item_kind(g, member, kind);
    set_battle_member_kept_item_held(g, member, 1);
}

static void give_stolen(GameState *g, uint8_t item)
{
    battle_keep_word(g, BATTLE_CLOCK_READS, 0);
    battle_keep_word(g, BATTLE_NUMBER_TESTED, 0);
    battle_keep_stat_offset_word(g, 0);
    ORIGINAL_EFFECT(battle_leave_word(g, RECORD_PLACE_CELL, 0));
    uint8_t thief = battle_actor(g);
    uint16_t record = effect_times(g, thief, KEPT_ITEM_BYTES);
    /* entered with the carry the item's test left, set */
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, true));
    ORIGINAL_EFFECT(battle_leave_word(g, THIEF_RECORD_CELL, record));
    set_battle_action_came_to_nothing(g, 0);
    if (item >= FIRST_RECORDED_ITEM) {
        uint8_t past = (uint8_t)(item - FIRST_RECORDED_ITEM);
        battle_keep_stat_offset_word(g, past);
        ORIGINAL_EFFECT(battle_leave_word(g, RECORD_PLACE_CELL, (uint8_t)(past * ITEM_RECORD_BYTES)));
        uint8_t flags = item_record_flags(g, item);
        if (flags & ITEM_KEPT_APART) {
            battle_member_keep_item(g, thief, item, item_record_detail(g, item), (uint8_t)(flags & ~ITEM_KEPT_APART));
            set_battle_steal_outcome(g, STEAL_OUTCOME_TOOK | STEAL_OUTCOME_KEPT_BY_MEMBER);
            return;
        }
    }
    TIMED_AS(g, 0xC1D005, battle_inventory_add(g, item));
}

// $C1:E9D5
StealOutcome effect_steal(GameState *g, uint8_t chance)
{
    set_battle_steal_outcome(g, STEAL_OUTCOME_NOT_TRIED);
    /* trebled in a byte: a chance over 85 runs round to a small one */
    if (effect_actor_stat(g, STAT_ACCESSORY) == THIEFS_ACCESSORY)
        chance = (uint8_t)(chance * 3);
    uint8_t drawn = battle_random(g, 0, 100);
    if (drawn > chance)
        return came_out(g, STEAL_MISSED);
    uint8_t target = battle_effect_target(g);
    ORIGINAL_EFFECT(battle_leave_word(g, ENEMY_PLACE_CELL, target));
    uint8_t enemy = combatant_id(g, target);
    battle_keep_stat_offset_word(g, effect_times(g, enemy, 7));
    bool charm = effect_charm_comes(g);
    EnemyItems items = enemy_items(g, enemy);
    uint8_t marks = marks_of(g, target), item;
    if (charm && (marks & HAS_CHARM)) {
        set_marks_of(g, target, (uint8_t)(marks & ~HAS_CHARM));
        item = items.charm;
    } else if (marks & HAS_DROPPED) {
        set_marks_of(g, target, (uint8_t)(marks & ~HAS_DROPPED));
        item = items.dropped;
    } else
        item = NO_ITEM;
    if (item == NO_ITEM)
        return came_out(g, STEAL_NOTHING);
    set_battle_steal_outcome(g, STEAL_OUTCOME_TOOK);
    battle_keep_byte(g, BATTLE_MEMBER_CELL, item);
    give_stolen(g, item);
    return came_out(g, STEAL_TOOK);
}
