/* Battle: what an action does, applied, and cleared between actions.
 *
 * An action is worked out into the damage table -- what each of its (up to
 * three) hits does to each combatant, which the effect routines fill
 * (battle/effect_amounts.c) -- and shown as the numbers that rise over the
 * combatants it hit (battle/numbers.c). Once it has been shown its hits land
 * (battle_apply_damage): a target any of whose hits missed takes none of
 * them; the rest come off its HP or its MP, held between nothing and its
 * maximum, and a combatant brought to nothing is knocked out, a felled
 * enemy's spoils going to the battle's winnings. The table is then cleared,
 * as the main loop clears it each pass, and the numbers once the action is
 * over. */
#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/effect_data.h"
#include "battle/effect_parts.h"
#include "battle/entries.h"
#include "battle/random.h"
#include "charge.h"
#include "original_effects.h"
#include "vars.h"

// $FD:ACFD
void battle_clear_damage_table(GameState *g)
{
    for (unsigned hit = 0; hit < BATTLE_DAMAGE_COUNT; hit++) {
        set_battle_damage_amount(g, hit, 0);
        set_battle_damage_kind(g, hit, 0);
        set_battle_damage_flags(g, hit, 0);
    }
}

// $FD:ACEE
void battle_clear_numbers_shown(GameState *g)
{
    set_battle_effect_tally(g, 0);
    for (unsigned record = 0; record < BATTLE_NUMBER_RECORDS_COUNT; record++) {
        set_battle_number_records_value(g, record, 0);
        set_battle_number_records_kind(g, record, 0);
        set_battle_number_records_last_byte(g, record, 0);
    }
}

/* ---- A felled enemy's spoils -------------------------------------------------------------------- */

/* The items the battle gives are two lists of three: the items enemies
 * dropped, and their charms. An item goes on its list once, where there is
 * room, and the rewards then say an item was given. */
enum { ITEMS_LISTED = 3, DROPPED_LIST = 0, CHARM_LIST = 3, NO_ITEM = 0 };
enum { REWARDED_AN_ITEM = 0x20 };

static void list_item_won(GameState *g, unsigned list, uint8_t item)
{
    for (unsigned place = list; place < list + ITEMS_LISTED; place++) {
        uint8_t listed = battle_items_won(g, place);
        if (listed == item)
            return;
        if (listed == NO_ITEM) {
            set_battle_items_won(g, place, item);
            set_battle_reward_flags(g, battle_reward_flags(g) | REWARDED_AN_ITEM);
            return;
        }
    }
}

/* An enemy that still has the item it drops lets it fall nine times in ten. */
enum { PERCENT = 100, DROPS_IN_A_HUNDRED = 90 };

/* The original keeps the enemy's place among the rewards, its place among the
 * enemies and the item in its first cells. */
enum { LEFT_ENEMY = 0x04, LEFT_PLACE = 0x06, LEFT_ITEM = 0x10, LEFT_FELLED = 0x0E };

static void leave_the_spoils_cells(GameState *g, uint8_t slot, uint16_t place, uint8_t item)
{
    battle_leave_word(g, LEFT_ENEMY, (uint16_t)(slot - BATTLE_FIRST_ENEMY));
    battle_leave_word(g, LEFT_PLACE, place);
    if (item != NO_ITEM)
        battle_leave_byte(g, LEFT_ITEM, item);
}

/* The felled enemy as the spoils find it. What the player gets from the
 * quirk this keeps: where the byte after the felled enemy's cell ($0E, read
 * as a word with it) is left set, the enemy gives the experience, gold, tech
 * points and item of whatever enemy number sits in unrelated memory that many
 * pages on -- or nothing, where that memory reads as an enemy needn't be
 * beaten or as nobody. The library never shows it: the byte is set (0x29,
 * left by the code before) at ten landings of hits in the Lavos fights of
 * late-lavos and the three endings movies, and none of them fells an enemy,
 * so every spoils taken over the library is the felled enemy's own, in the
 * shipping build and the native one alike. Off the library a native build,
 * which doesn't keep the cell, can differ from the original here (the
 * effects baseline; docs/TODO.md). */
typedef struct {
    uint8_t standing, id;
} FelledEnemy;

static FelledEnemy felled_enemy(const GameState *g, uint8_t slot)
{
    unsigned place = (unsigned)battle_low_scratch(g, LEFT_FELLED + 1) << 8 | slot;
    unsigned enemy = place - BATTLE_FIRST_ENEMY;
    return (FelledEnemy){
        enemy < BATTLE_ENEMY_STANDING_COUNT ? battle_enemy_standing(g, enemy)
                                            : battle_enemy_standing_unchecked(g, enemy),
        place < BATTLE_COMBATANT_ID_COUNT ? battle_combatant_id(g, place) : battle_combatant_id_unchecked(g, place),
    };
}

// $FD:ABA2
/* A felled enemy's spoils go to the battle's winnings, unless it needn't
 * have been beaten: its experience, gold and tech points, and its item --
 * its charm where the item is its charm, otherwise the item it drops where
 * it still has it and the drop comes. */
static void take_the_spoils(GameState *g, uint8_t slot)
{
    FelledEnemy felled = felled_enemy(g, slot);
    uint8_t id = felled.id;
    if ((felled.standing & ENEMY_NEEDNT_BE_BEATEN) || id == COMBATANT_NONE)
        return;
    uint16_t place;
    TIMED_AS(g, 0xC1C90B, place = (uint16_t)battle_multiply_words(g, id, ENEMY_REWARD_BYTES));
    EnemySpoils spoils = enemy_spoils(g, place);
    set_battle_experience_won(g, (uint16_t)(battle_experience_won(g) + spoils.experience));
    set_battle_gold_won(g, (uint16_t)(battle_gold_won(g) + spoils.gold));
    uint32_t points = battle_tech_points_won_low(g) + spoils.tech_points;
    if (points > 0xFFFF)
        set_battle_tech_points_won_high(g, (uint8_t)(battle_tech_points_won_high(g) + 1));
    set_battle_tech_points_won_low(g, (uint16_t)points);
    uint8_t item = spoils.items.dropped;
    ORIGINAL_EFFECT(leave_the_spoils_cells(g, slot, place, item));
    if (item == NO_ITEM)
        return;
    if (item == spoils.items.charm) {
        list_item_won(g, CHARM_LIST, item);
        return;
    }
    if (!(battle_steal_marks(g, slot) & HAS_DROPPED))
        return;
    uint8_t roll;
    TIMED_AS(g, 0xC1AF22, roll = battle_random(g, 0, PERCENT));
    if (roll < DROPS_IN_A_HUNDRED)
        list_item_won(g, DROPPED_LIST, item);
}

/* ---- The hits landing --------------------------------------------------------------------------- */

/* The damage table's entry for a hit on a combatant: a set of entries for
 * each hit. */
enum { HITS = 3 };
static unsigned hit_entry(unsigned hit, unsigned slot) { return hit * BATTLE_COMBATANTS + slot; }

static bool any_hit_missed(const GameState *g, unsigned slot)
{
    for (unsigned hit = 0; hit < HITS; hit++)
        if (battle_damage_flags(g, hit_entry(hit, slot)) & DAMAGE_MISSED)
            return true;
    return false;
}

// $C1:EC39
/* A target any of whose hits missed takes none of them, and an enemy so
 * missed isn't counted beaten by the action. */
void battle_drop_missed_targets(GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        if (!any_hit_missed(g, slot))
            continue;
        for (unsigned hit = 0; hit < HITS; hit++)
            set_battle_damage_amount(g, hit_entry(hit, slot), 0);
        if (battle_slot_is_party(slot))
            continue;
        set_battle_enemy_beaten_any(g, 0);
        set_battle_enemy_beaten(g, slot - BATTLE_FIRST_ENEMY, 0);
        set_battle_enemy_beaten_counted(g, slot - BATTLE_FIRST_ENEMY, 0);
    }
}

/* The hits that come off HP, or off MP, taken off the amount in turn (a hit
 * that heals is recorded negative), in 16 bits. */
static uint16_t after_the_hits(const GameState *g, unsigned slot, uint16_t amount, bool mp)
{
    for (unsigned hit = 0; hit < HITS; hit++) {
        unsigned entry = hit_entry(hit, slot);
        if (((battle_damage_flags(g, entry) & DAMAGE_TO_MP) != 0) == mp)
            amount = (uint16_t)(amount - battle_damage_amount(g, entry));
    }
    return amount;
}

/* An amount past its maximum is held there; one below nothing (the top bit
 * set) is nothing. */
static uint16_t held_to(uint16_t amount, uint16_t most)
{
    if (amount & 0x8000)
        return 0;
    return amount < most ? amount : most;
}

/* The combatant brought to nothing: one still standing and in the fight is
 * knocked out, a felled enemy's spoils taken first. */
static void bring_down(GameState *g, unsigned slot)
{
    if (!combatant_knocked_out(g, slot) && battle_combatant_fighting(g, slot) != COMBATANT_NONE) {
        if (!battle_slot_is_party(slot)) {
            ORIGINAL_EFFECT(battle_leave_byte(g, LEFT_FELLED, (uint8_t)slot));
            TIMED_AS(g, 0xFDABA2, take_the_spoils(g, (uint8_t)slot));
        }
        set_battle_stats_condition(g, slot, battle_stats_condition(g, slot) | CONDITION_KNOCKED_OUT);
    }
    set_battle_stats_hp(g, slot, 0);
}

/* The combatant's HP takes its hits, unless its first hit is to MP: true
 * where it is brought to nothing. */
static bool take_hp(GameState *g, unsigned slot)
{
    if (battle_damage_flags(g, hit_entry(0, slot)) & DAMAGE_TO_MP)
        return false;
    uint16_t hp = after_the_hits(g, slot, battle_stats_hp(g, slot), false);
    if ((hp & 0x8000) || hp == 0) {
        bring_down(g, slot);
        return true;
    }
    set_battle_stats_hp(g, slot, held_to(hp, battle_stats_max_hp(g, slot)));
    return false;
}

// $C1:EC7F
/* The action's hits land on every combatant: its HP, and unless that
 * brought it down, its MP; then the table is cleared. */
void battle_apply_damage(GameState *g)
{
    TIMED_AS(g, 0xC1EC39, battle_drop_missed_targets(g));
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        if (take_hp(g, slot))
            continue;
        uint16_t mp = after_the_hits(g, slot, battle_stats_mp(g, slot), true);
        set_battle_stats_mp(g, slot, held_to(mp, battle_stats_max_mp(g, slot)));
    }
    TIMED_AS(g, 0xFDACFD, battle_clear_damage_table(g));
}

/* ---- The entries -------------------------------------------------------------------------------- */

/* The clearings leave A 0 and the carry of their loop's last compare, set,
 * which their callers read; the hits' landing ends with the table's
 * clearing, and so leaves the same. */
static void leave_cleared(Snes *s)
{
    s->cpu.a = 0;
    m_set_flag(s, CPU_FLAG_C, true);
}

void battle_clear_damage(Snes *s)
{
    battle_clear_damage_table(state_of(s));
    leave_cleared(s);
    m_rtl(s);
}

void battle_clear_numbers(Snes *s)
{
    battle_clear_numbers_shown(state_of(s));
    leave_cleared(s);
    m_rtl(s);
}

void battle_apply_damage_entry(Snes *s)
{
    battle_apply_damage(state_of(s));
    leave_cleared(s);
    m_rts(s);
}

void battle_drop_missed_targets_entry(Snes *s)
{
    battle_drop_missed_targets(state_of(s));
    m_rts(s);
}

/* A felled enemy's spoils, for the enemy the original names in its cell. */
void battle_take_the_spoils(Snes *s)
{
    GameState *g = state_of(s);
    take_the_spoils(g, battle_low_scratch(g, LEFT_FELLED));
    m_rtl(s);
}

bool battle_spoils_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t slot = battle_low_scratch(g, LEFT_FELLED);
    bool fits = battle_names_combatant(slot) && !battle_slot_is_party(slot);
    vars_peek(false);
    return battle_on_page(s) && fits;
}
