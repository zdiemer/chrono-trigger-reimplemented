/* Battle: the main loop, from a battle's start to its end.
 *
 * A battle starts at $C1:8000: its turn state cleared, the battle set up
 * (battle step 0, below), and the order the combatants' timers are looked for
 * in drawn at random (battle/timers.h), then the enemies' own order. Then the
 * loop, a pass a frame:
 *
 *   - the frame goes by (battle step 3), and the action's damage table is
 *     cleared and every combatant updated (battle/combatants.c);
 *   - the battle ends in a defeat when the party is all out, in a victory
 *     when every enemy that must be beaten is out, and the party runs away
 *     once L and R have been held long enough;
 *   - otherwise the combatants' low HP and statuses are brought up to date,
 *     and the timers counted down and the run-out ones handled
 *     (battle/timers.c), the turn's handler among them: the turn below.
 *
 * A turn (the handler of the turn timer, battle_give_turn) gives the
 * combatant whose timer ran out its turn: an enemy carries out the action its
 * script chose (battle/enemy_turn.c), a party member's command menu opens
 * (or, in a demo battle or under a status that decides for it, its action is
 * picked for it). The party's chosen actions are carried out one at a time in
 * the order they were chosen. After an action the enemies it hit react, each
 * through its script's reactions, and the enemies with nothing chosen choose
 * again (battle/enemy_scripts.h).
 *
 * The loop's own logic is one sequence for each stretch of it, calling the
 * battle's other routines -- the battle steps ($C1:0003, which wait for
 * frames), the turns, carrying out the party's actions, the rewards -- where
 * the original calls them. */
#include "battle/main_loop.h"

#include "battle/battle.h"
#include "battle/battle_end.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/effect_parts.h"
#include "battle/enemy_scripts.h"
#include "battle/entries.h"
#include "battle/fight.h"
#include "battle/inventory.h"
#include "battle/message.h"
#include "battle/party_turns.h"
#include "battle/program.h"
#include "battle/setup.h"
#include "battle/random.h"
#include "battle/rules_data.h"
#include "battle/rewards.h"
#include "battle/status_timers.h"
#include "battle/steps.h"
#include "battle/targets.h"
#include "battle/timers.h"
#include "charge.h"
#include "vars.h"
#include "wram.h"

/* The battle's end's bookkeeping (battle/battle_end.c), each timed as the
 * routine the original calls. */
enum { CLEAR_TURN_WORK = 0xFDA982 };   /* the turns' work cleared (below) */

static void clear_party_statuses(GameState *g) { TIMED_AS(g, 0xC1EA9D, battle_clear_party_statuses(g)); }

static void stand_party_up(GameState *g) { TIMED_AS(g, 0xC1EAE8, battle_stand_party_up(g)); }

static void keep_party_records(GameState *g) { TIMED_AS(g, 0xC1B442, battle_keep_party_records(g)); }

static void take_enemies_away(GameState *g) { TIMED_AS(g, 0xC1B3BB, battle_take_enemies_away(g)); }

/* The party member the loop is working on, kept as a word in the work cell
 * where the routines it calls look for it. */
static void leave_member_for_callees(GameState *g, uint8_t member)
{
    set_battle_low_scratch(g, 0x14, member);
    set_battle_low_scratch(g, 0x15, 0);
}

/* ---- The routines the loop calls --------------------------------------------------------- */

/* The routines the loop calls, each where the original makes it, which
 * their work is charged as and a refusal names. */
enum {
    START_FIGHT = 0xC1FA8B,
    UPDATE_COMBATANTS = 0xC1B223,
    CANCEL_IF_UNABLE = 0xC1B3F9,
    GIVE_TURN = 0xC18461,
    PARTY_STANDS_DOWN = 0xC1B7F2,
    EXPERIENCE_TO_GOLD = 0xFDB201,
    GIVE_GOLD = 0xC1D0A2,
    SHARE_EXPERIENCE = 0xC1F93E,
    SHOW_REWARDS = 0xFDAD17,
    PUT_ITEMS_BACK = 0xC1B4E9,
    GIVE_ITEM = 0xC1D005,
    SHOW_MESSAGE = 0xCD0021,
    START_ACTION = 0xC1895B,
    ENEMY_TURN = 0xC18CF9,
    MEMBER_TURN = 0xFDAB30,
    CHECK_COMBINATIONS = 0xC1B575,
    MEMBER_ACTS_FOR_ITSELF = 0xC1BCE1,
    CARRY_OUT_ACTION = 0xC1B967,
    AFTER_ACTION = 0xC1B762,
    PARTY_COUNTERS = 0xFDAC6E,
    FINISH_BEATEN = 0xC1AC5E,
    PASS_END = 0xC18345,
    STARTING_WITH_THE_ENEMIES = 0xC18030,
};

/* The fight's start (battle/fight_start.c), refused with a party the port
 * doesn't take. */
static bool start_the_fight(GameState *g)
{
    if (!setup_party_is_real(g))
        return battle_contract_broken(g, START_FIGHT, "a fight's start with a party the port doesn't take");
    TIMED_AS(g, START_FIGHT, battle_start_fight(g));
    return true;
}

/* Every combatant updated (battle/combatants.c). */
static bool update_the_combatants(GameState *g)
{
    bool done = false;
    TIMED_AS(g, UPDATE_COMBATANTS, done = battle_update_all_combatants(g));
    return done;
}

/* A member's action dropped if it can't act (battle/party_turns.c). */
static bool cancel_if_unable(GameState *g, Platform *pf, uint8_t member)
{
    bool done = false;
    TIMED_AS(g, CANCEL_IF_UNABLE, done = battle_cancel_if_unable(g, pf, member));
    return done;
}

/* Each member's menu closed as the battle ends (battle/party_turns.c). */
static bool stand_down(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, PARTY_STANDS_DOWN, done = battle_party_stands_down(g, pf));
    return done;
}

/* The rewards (battle/rewards_entries.c, battle/rewards_messages.c): the
 * experience turned to gold, the gold won given, the experience shared
 * (refused with records the port doesn't take), the rewards shown, the items
 * chosen and not used put back (battle/inventory.c), and an item won given. */
static void turn_experience_to_gold(GameState *g) { TIMED_AS(g, EXPERIENCE_TO_GOLD, battle_turn_experience_to_gold(g)); }

static void give_the_gold(GameState *g) { TIMED_AS(g, GIVE_GOLD, (void)battle_give_gold(g, battle_gold_won(g))); }

static bool share_the_experience(GameState *g)
{
    if (!battle_experience_can_be_shared(g))
        return battle_contract_broken(g, SHARE_EXPERIENCE, "the experience shared with records the port doesn't take");
    TIMED_AS(g, SHARE_EXPERIENCE, battle_share_experience(g));
    return true;
}

static bool show_the_rewards(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, SHOW_REWARDS, done = battle_show_the_rewards(g, pf));
    return done;
}

static void put_items_back(GameState *g) { TIMED_AS(g, PUT_ITEMS_BACK, battle_put_items_back(g)); }

static void give_an_item(GameState *g, uint8_t item) { TIMED_AS(g, GIVE_ITEM, (void)battle_inventory_add(g, item)); }

/* The running away's message (battle/message.c) and its action
 * (battle/status_timers.c). */
enum { RAN_AWAY_MESSAGE = 8, RUN_AWAY_ACTION = 0x79 };

static bool say_the_party_ran_away(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, SHOW_MESSAGE, done = battle_show_a_battle_message(g, pf, RAN_AWAY_MESSAGE));
    return done;
}

static bool show_the_running_away(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, START_ACTION, done = battle_show_status_number(g, pf, RUN_AWAY_ACTION));
    return done;
}

/* An enemy's turn, or its reaction (battle/enemy_turn.c). */
static bool enemy_takes_its_turn(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, ENEMY_TURN, done = battle_enemy_takes_turn(g, pf));
    return done;
}

/* What a member's statuses decide on its turn (battle/party_turns.c). */
static bool member_status_turn(GameState *g, Platform *pf, uint8_t member)
{
    bool done = false;
    TIMED_AS(g, MEMBER_TURN, done = battle_member_status_turn(g, pf, member));
    return done;
}

/* Which members can join a combination (battle/menu_window.c). */
static bool check_combinations(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, CHECK_COMBINATIONS, done = battle_update_combo_members(g, pf));
    return done;
}

/* A member whose status picks its action takes it (battle/member_turn.c). */
static bool act_for_itself(GameState *g)
{
    bool done = false;
    TIMED_AS(g, MEMBER_ACTS_FOR_ITSELF, done = battle_end_member_turn(g));
    return done;
}

/* A member's chosen action carried out (battle/member_turn.c). */
static bool carry_out_the_action(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, CARRY_OUT_ACTION, done = battle_carry_out_member_action(g, pf));
    return done;
}

/* The enemies an action knocked out seen to, and the party's counters
 * (battle/party_turns.c). */
static bool see_to_the_knocked_out(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, AFTER_ACTION, done = battle_see_to_knocked_out_enemies(g, pf));
    return done;
}

static bool party_counters(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, PARTY_COUNTERS, done = battle_party_counters(g, pf));
    return done;
}

/* ---- The battle's start ------------------------------------------------------------------ */

/* The battle's work on its turns, from $7E:AD89 to $B3F4: the action under
 * way and its targets, the combatants' timers and their order, the enemies'
 * scripts' places, the effects' tallies and the rewards' notes. It is every
 * battle variable there rather than one the RAM map names, so it is cleared by
 * address (game/wram.h). */
enum { TURN_WORK_AT = 0x7EAD89, TURN_WORK_BYTES = 0xB3F5 - 0xAD89 };

// $FD:A982
/* Every battle starts with its turns' work cleared. The fill takes none of
 * the port's own time, so the original's, a store a byte, is charged for it. */
void battle_clear_the_turn_work(GameState *g)
{
    wram_fill8(g, TURN_WORK_AT, TURN_WORK_BYTES, 0);
    charge_as(g, CLEAR_TURN_WORK, TURN_WORK_BYTES);
}

/* Where the battle's random numbers are drawn from its table: kept between
 * battles, and among the battle's work cells while it runs. */
static void take_random_position(GameState *g)
{
    uint8_t kept = battle_random_kept(g);
    set_battle_random_position(g, kept);
    set_battle_random_other_position(g, kept);
}

/* A demo battle is set up as a normal one: its bit of the setup flags is kept
 * apart until then. */

static void set_demo_aside(GameState *g)
{
    uint8_t flags = battle_setup_flags(g);
    set_battle_demo_kept(g, flags & BATTLE_SETUP_DEMO);
    set_battle_setup_flags(g, flags & (uint8_t)~BATTLE_SETUP_DEMO);
}

/* The orders are drawn from a pool, which the battle keeps where the enemies'
 * foes will be: its entries are a battle slot plus one (or an enemy's id),
 * and a place drawn is emptied. */
enum { DRAWN = 0xFF };

static uint8_t pool(const GameState *g, unsigned place) { return battle_foes(g, place); }
static void set_pool(GameState *g, unsigned place, uint8_t entry) { set_battle_foes(g, place, entry); }

/* The combatants to be drawn into the timers' order: the party members in
 * the fight, by the slot each character fights in, then the enemies, last to
 * first. */
static void list_combatants_for_the_order(GameState *g)
{
    for (unsigned i = 0; i < BATTLE_TIMER_ORDER_COUNT; i++) {
        set_battle_timer_order(g, i, DRAWN);
        set_pool(g, i, DRAWN);
    }
    set_battle_timer_kinds_due(g, 0);
    unsigned listed = 0;
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t character = battle_combatant_fighting(g, member);
        if (character & 0x80)
            continue;
        set_pool(g, listed++, (uint8_t)(battle_character_slot(g, character) + 1));
    }
    for (uint8_t slot = (uint8_t)(battle_enemy_count(g) + BATTLE_FIRST_ENEMY); slot != BATTLE_FIRST_ENEMY; slot--)
        set_pool(g, listed++, slot);
}

/* A random number below `limit`, from the battle's own position. */
static uint8_t draw_below(GameState *g, uint8_t limit)
{
    uint8_t drawn = 0;
    TIMED_AS(g, 0xC1AF22, drawn = battle_random(g, 0, limit));
    return drawn;
}

/* Draw the combatants into the timers' order at random, each once: a place
 * already drawn is drawn again. */
static void draw_timer_order(GameState *g)
{
    uint8_t length = battle_timer_order_length(g);
    unsigned placed = 0;
    do {
        uint8_t drawn = draw_below(g, length);
        uint8_t entry = pool(g, drawn);
        if (entry == DRAWN)
            continue;
        set_battle_timer_order(g, placed++, (uint8_t)(entry - 1));
        set_pool(g, drawn, DRAWN);
    } while (placed < length);
}

/* Then the enemies' own order, drawn the same way from their ids; it ends when
 * every enemy with an id is placed. */
static void draw_enemy_order(GameState *g)
{
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        set_battle_enemy_order(g, enemy, DRAWN);
        set_pool(g, enemy, battle_combatant_id(g, BATTLE_FIRST_ENEMY + enemy));
    }
    unsigned placed = 0;
    for (;;) {
        uint8_t drawn = draw_below(g, BATTLE_ENEMIES);
        if (pool(g, drawn) == DRAWN)
            continue;
        set_pool(g, drawn, DRAWN);
        set_battle_enemy_order(g, placed++, drawn);
        unsigned left = 0;
        while (pool(g, left) == DRAWN && ++left < battle_enemy_count(g))
            ;
        if (left >= battle_enemy_count(g))
            return;
    }
}

/* The enemies with nothing chosen choose what they do next. */
static void enemies_choose(GameState *g) { TIMED_AS(g, 0xC18C09, enemy_scripts_choose(g)); }

/* A demo battle's party acts at once and its enemies never. */
enum { AT_ONCE = 1, NEVER = 0xFF };

static void finish_start(GameState *g)
{
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        set_battle_enemy_last_condition(g, enemy, NEVER);
    set_battle_enemy_turn(g, 0);
}

static void put_demo_back(GameState *g)
{
    set_battle_setup_flags(g, battle_setup_flags(g) | battle_demo_kept(g));
    if (!battle_is_demo(g))
        return;
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++)
        set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot, battle_slot_is_party(slot) ? AT_ONCE : NEVER);
}

/* How a stretch of the loop ends: where it went no further, the battle left
 * for the field, or on to the loop's next stretch. */
typedef enum {
    LOOP_STOPPED,
    LOOP_LEFT,
    LOOP_TO_THE_HEAD,
    LOOP_TO_THE_TIMERS,
    LOOP_TO_THE_REST,
    LOOP_NOT_OURS,
} LoopGoesOn;

static LoopGoesOn finish_battle_start(GameState *g);
static bool give_the_turn(GameState *g, Platform *pf);
static bool finish_the_beaten(GameState *g, Platform *pf);

// $C1:8000
/* Start the battle, then on to the loop's head at $C1:8120. No battle is set
 * up without enemies, or with more than there are places for: the original's
 * lists would run on through memory for one, and the port refuses such a
 * setup (LOOP_NOT_OURS). */
static LoopGoesOn start_battle(GameState *g, Platform *pf)
{
    TIMED_AS(g, CLEAR_TURN_WORK, battle_clear_the_turn_work(g));
    take_random_position(g);
    if (!start_the_fight(g))
        return LOOP_STOPPED;
    set_demo_aside(g);
    if (!battle_step_taken(g, pf, BATTLE_STEP_SET_UP))
        return LOOP_STOPPED;
    return finish_battle_start(g);
}

// $C1:8030
static LoopGoesOn finish_battle_start(GameState *g)
{
    if (battle_enemy_count(g) == 0 || battle_enemy_count(g) > BATTLE_ENEMIES)
        return LOOP_NOT_OURS;
    list_combatants_for_the_order(g);
    draw_timer_order(g);
    draw_enemy_order(g);
    finish_start(g);
    enemies_choose(g);
    put_demo_back(g);
    return LOOP_TO_THE_HEAD;
}

/* ---- A pass of the loop ------------------------------------------------------------------ */

// $C1:8120
/* The pass's frame: the pass counted, the damage table's hit back to the
 * first, and the frame's work and wait (battle step 3). */
static bool next_frame(GameState *g, Platform *pf)
{
    set_battle_loop_count(g, (uint8_t)(battle_loop_count(g) + 1));
    set_battle_damage_hit(g, 0);
    return battle_step_taken(g, pf, BATTLE_STEP_NEXT_FRAME);
}

/* Every enemy that must be beaten is out (or none is there). */
static bool enemies_beaten(const GameState *g)
{
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        if (battle_combatant_fighting(g, BATTLE_FIRST_ENEMY + enemy) != COMBATANT_NONE
            && !(battle_enemy_standing(g, enemy) & ENEMY_NEEDNT_BE_BEATEN))
            return false;
    return true;
}

/* The six items a victory can give. */
enum { ITEMS_WON = 6 };

/* The party is all out: its defeat, over eight frames and the defeat's step,
 * and away without rewards. */
enum { DEFEAT_FRAMES = 8 };

static void restore_party_max_hp(GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_stats_max_hp(g, member, battle_party_max_hp_kept(g, member));
}

static LoopGoesOn leave_battle(GameState *g)
{
    set_battle_random_kept(g, battle_random_position(g));
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_stats_carried(g, member, battle_member_carried_weapon(g, member));
    TIMED_AS(g, 0xFDAAB0, battle_note_beaten_enemies(g));
    return LOOP_LEFT;
}

static LoopGoesOn leave_without_rewards(GameState *g)
{
    restore_party_max_hp(g);
    clear_party_statuses(g);
    stand_party_up(g);
    keep_party_records(g);
    return leave_battle(g);
}

static LoopGoesOn leave_with_rewards(GameState *g, Platform *pf)
{
    restore_party_max_hp(g);
    if (battle_is_demo(g))
        return leave_without_rewards(g);
    uint8_t held = battle_rewards_held(g);
    if (held == 0) {
        turn_experience_to_gold(g);
        give_the_gold(g);
        set_battle_reward_flags(g, battle_reward_flags(g) | REWARD_GOLD);
        if (!share_the_experience(g))
            return LOOP_STOPPED;
    }
    if (!show_the_rewards(g, pf))
        return LOOP_STOPPED;
    stand_party_up(g);
    keep_party_records(g);
    put_items_back(g);
    for (unsigned item = 0; item < ITEMS_WON; item++) {
        uint8_t won = battle_items_won(g, item);
        if (won == 0)
            continue;
        give_an_item(g, won);
        set_battle_reward_flags(g, battle_reward_flags(g) | REWARD_ITEMS);
    }
    return leave_battle(g);
}

static LoopGoesOn lose(GameState *g, Platform *pf)
{
    clear_party_statuses(g);
    take_enemies_away(g);
    battle_ends(g, BATTLE_DEFEATED);
    if (!stand_down(g, pf))
        return LOOP_STOPPED;
    set_battle_defeat_showing(g, true);
    for (unsigned frame = 0; frame < DEFEAT_FRAMES; frame++) {
        if (!battle_step_taken(g, pf, BATTLE_STEP_NEXT_FRAME))
            return LOOP_STOPPED;
    }
    set_battle_defeat_showing(g, false);
    if (!battle_step_taken(g, pf, BATTLE_STEP_DEFEAT))
        return LOOP_STOPPED;
    return leave_without_rewards(g);
}

/* Every enemy is beaten: the victory, and away with the rewards. */
enum { VICTORY_MARK = 0xFF };

static LoopGoesOn win(GameState *g, Platform *pf)
{
    take_enemies_away(g);
    battle_ends(g, BATTLE_WON_OR_RAN);
    set_battle_won(g, VICTORY_MARK);
    clear_party_statuses(g);
    if (!battle_step_taken(g, pf, BATTLE_STEP_VICTORY))
        return LOOP_STOPPED;
    return leave_with_rewards(g, pf);
}

/* The party's low HP noted, and every combatant's statuses followed
 * (battle/combatants.c). */
static LoopGoesOn run_away(GameState *g, Platform *pf)
{
    battle_ends(g, BATTLE_WON_OR_RAN);
    TIMED_AS(g, 0xFDAA98, battle_count_enemies_out(g));
    if (!say_the_party_ran_away(g, pf))
        return LOOP_STOPPED;
    clear_party_statuses(g);
    if (!stand_down(g, pf) || !show_the_running_away(g, pf))
        return LOOP_STOPPED;
    stand_party_up(g);
    keep_party_records(g);
    put_items_back(g);
    take_enemies_away(g);
    return leave_without_rewards(g);
}

static void follow_combatants(GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        if (battle_slot_is_party(slot))
            TIMED_AS(g, 0xC1B093, combatant_note_low_hp(g, slot));
        TIMED_AS(g, 0xC1B0B6, combatant_follow_statuses(g, slot));
    }
}

/* A party member whose status keeps it from acting has its action dropped,
 * for each member in a slot (battle_update_all_combatants's order). The
 * member's number goes in its cell. */
static bool cancel_unable_members(GameState *g, Platform *pf)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_member_updating(g, (uint8_t)member);
        if (battle_combatant_id(g, member) == COMBATANT_NONE)
            continue;
        if (!cancel_if_unable(g, pf, (uint8_t)member))
            return false;
    }
    set_battle_member_updating(g, BATTLE_PARTY_SIZE);
    return true;
}

// $C1:812C
/* A pass after its frame: the damage table cleared, the combatants updated;
 * the battle's ends; the combatants' low HP and statuses; then the timers, or
 * while they are held the members' checks, or while an action runs over
 * passes the turn's handler at once. */
static LoopGoesOn run_pass(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xFDACFD, battle_clear_damage_table(g));
    if (!update_the_combatants(g))
        return LOOP_STOPPED;
    if (battle_party_defeated(g))
        return lose(g, pf);
    if (enemies_beaten(g))
        return win(g, pf);
    follow_combatants(g);
    if (battle_timers_held(g))
        return cancel_unable_members(g, pf) ? LOOP_TO_THE_REST : LOOP_STOPPED;
    if (!battle_is_demo(g) && battle_run_counter(g) != 0)
        return run_away(g, pf);
    if (battle_action_running(g) == 0)
        return LOOP_TO_THE_TIMERS;
    return give_the_turn(g, pf) ? LOOP_TO_THE_REST : LOOP_STOPPED;
}

// $C1:8345
/* The end of a pass: back to the loop's head, or a demo battle's end. (A
 * debugging path the ROM can turn on, off in the released game, the port
 * refuses: debugging_switch_on.) */
static LoopGoesOn end_pass(GameState *g, Platform *pf)
{
    if (!battle_is_demo(g))
        return LOOP_TO_THE_HEAD;
    TIMED_AS(g, 0xC1B3D2, battle_keep_standing_enemies(g));
    return leave_with_rewards(g, pf);
}

/* ---- A turn ------------------------------------------------------------------------------ */

/* The combatant whose turn timer ran out: the order's place before the turn
 * timers' cursor (the order's last when the cursor went round to the top). */
static uint8_t combatant_whose_turn(const GameState *g)
{
    uint8_t place = battle_timer_kind_cursors(g, BATTLE_TIMER_TURN);
    if (place == 0)
        place = battle_timer_order_length(g);
    return battle_timer_order_at(g, (uint8_t)(place - 1));
}

/* An enemy takes its turn only from an animation it can act from -- standing,
 * stopped, or a few others -- or when it is marked to act whatever it shows;
 * and not while its turn is held. */
static bool enemy_can_act(GameState *g, unsigned enemy)
{
    uint8_t animation = battle_enemy_animation(g, enemy);
    bool from_animation = animation == ENEMY_ANIMATION_IDLE || animation == ENEMY_ANIMATION_STOPPED || animation == 6
                          || (animation >= 0x0A && animation < 0x11);
    if (!from_animation && !battle_enemy_acts_anyway(g, enemy))
        return false;
    set_battle_enemy_acts_anyway(g, enemy, false);
    return battle_enemy_script_outcome(g, enemy) == OUTCOME_HELD;
}

/* An enemy's turn: its script picks its action, its turn wait starts again
 * (halved while it is hasted, doubled while slowed) and its turn timer runs. */
static bool enemy_turn(GameState *g, uint8_t slot, Platform *pf)
{
    set_battle_enemy_acting(g, (uint8_t)(slot - BATTLE_FIRST_ENEMY));
    if (!enemy_takes_its_turn(g, pf))
        return false;
    (void)battle_restart_turn_wait(g, slot);
    battle_restart_timer(g, BATTLE_TIMER_TURN, slot);
    return true;
}

static uint8_t member_action(const GameState *g, unsigned member) { return battle_member_actions_state(g, member); }
static void set_member_action(GameState *g, unsigned member, uint8_t v) { set_battle_member_actions_state(g, member, v); }

/* A member with its action chosen is noted ready (its record's byte kept). */

static void note_members_ready(GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (member_action(g, member) & MEMBER_ACTION_CHOSEN)
            set_battle_member_ready(g, member, member_action(g, member));
}

static bool any_member_ready(const GameState *g)
{
    return (battle_member_ready(g, 0) | battle_member_ready(g, 1) | battle_member_ready(g, 2)) != 0;
}

/* A party member's turn: what its statuses decide first (a status that picks
 * its action for it: its combination techs checked, and the action taken);
 * otherwise, a member with no action yet has its menu opened (in a demo
 * battle the demo's party picks its actions). */
typedef enum { ACTED_FOR_ITSELF, DEMO_PICKED, TO_THE_QUEUE } MemberTurn;

static bool member_turn(GameState *g, uint8_t member, MemberTurn *turn, Platform *pf)
{
    if (!member_status_turn(g, pf, member))
        return false;
    leave_member_for_callees(g, member);
    if (battle_member_acts_for_itself(g)) {
        *turn = ACTED_FOR_ITSELF;
        set_battle_acting_for_itself(g, member);
        return check_combinations(g, pf) && act_for_itself(g);
    }
    *turn = TO_THE_QUEUE;
    if (member_action(g, member) != MEMBER_NO_ACTION)
        return true;
    if (battle_is_demo(g)) {
        *turn = DEMO_PICKED;
        TIMED_AS(g, 0xC1B70E, battle_demo_party_ready(g));
        return true;
    }
    set_member_action(g, member, MEMBER_MENU_OPEN);
    set_battle_step_member(g, member);
    return battle_step_taken(g, pf, BATTLE_STEP_OPEN_MENU);
}

/* The next party member whose chosen action is carried out, from the queue of
 * them; none is $FF. */
enum { NOBODY = 0xFF };

/* Carry out the member's action, if its record still says it chose one (its
 * chosen bit as when it was noted ready): the action, then both forgotten. */
static uint8_t take_from_queue(GameState *g)
{
    uint8_t member = battle_action_queue(g, 0);
    if (member & 0x80)
        return NOBODY;
    set_battle_action_queue(g, 0, battle_action_queue(g, 1));
    set_battle_action_queue(g, 1, battle_action_queue(g, 2));
    set_battle_action_queue(g, 2, NOBODY);
    set_battle_action_queue_length(g, (uint8_t)(battle_action_queue_length(g) - 1));
    set_battle_actor(g, member);
    return member;
}

static bool carry_out(GameState *g, uint8_t member, bool *carried, Platform *pf)
{
    *carried = ((member_action(g, member) ^ battle_member_ready(g, member)) & MEMBER_ACTION_CHOSEN) != 0;
    if (!*carried)
        return true;
    set_battle_actor(g, member);
    if (!carry_out_the_action(g, pf))
        return false;
    set_battle_member_ready(g, member, MEMBER_NO_ACTION);
    set_member_action(g, member, MEMBER_NO_ACTION);
    return true;
}

// $C1:883D
/* Every member in a slot: its combination techs checked, and its action
 * dropped if its status keeps it from acting. It is a subroutine of the
 * turn's. */
static bool check_members(GameState *g, Platform *pf)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_member_updating(g, (uint8_t)member);
        if (battle_combatant_id(g, member) == COMBATANT_NONE)
            continue;
        leave_member_for_callees(g, (uint8_t)member);
        if (!check_combinations(g, pf) || !cancel_if_unable(g, pf, (uint8_t)member))
            return false;
    }
    set_battle_member_updating(g, BATTLE_PARTY_SIZE);
    return true;
}

/* How the turn's first half ends: on to the members' checks, or past them. */
typedef enum { CHECK_MEMBERS, SKIP_CHECKS, TO_THE_END } TurnGoesOn;

/* The members with their actions chosen cycle through the spare turns: the
 * next one down (from the third) is the member the loop's steps name. */
static void next_spare_member(GameState *g)
{
    uint8_t member = (uint8_t)(battle_spare_turn_member(g) - 1);
    if (member & 0x80)
        member = BATTLE_PARTY_SIZE - 1;
    set_battle_spare_turn_member(g, member);
    leave_member_for_callees(g, member);
}

// $C1:8461 (to $C1:8626)
/* The turn of the combatant whose turn timer ran out (on the passes the timers
 * are looked at): an enemy's, or a party member's, then the party's queued
 * actions. */
static bool run_queue(GameState *g, bool note_ready, TurnGoesOn *next, Platform *pf)
{
    if (note_ready)
        note_members_ready(g);
    if (battle_is_demo(g))
        TIMED_AS(g, 0xC1B725, battle_demo_next_turn(g));
    uint8_t member = take_from_queue(g);
    *next = CHECK_MEMBERS;
    if (member == NOBODY || member >= BATTLE_PARTY_SIZE)
        return true;
    bool carried;
    if (!carry_out(g, member, &carried, pf))
        return false;
    /* the third member's goes on to the members' checks, the others' past them */
    if (member != BATTLE_PARTY_SIZE - 1 || !carried)
        *next = SKIP_CHECKS;
    return true;
}

static bool give_turn(GameState *g, TurnGoesOn *next, Platform *pf)
{
    set_battle_enemy_beaten_any(g, false);
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        set_battle_enemy_beaten(g, enemy, false);
    uint8_t slot = combatant_whose_turn(g);
    set_battle_turn_slot(g, slot);
    *next = CHECK_MEMBERS;
    if (!battle_slot_is_party(slot)) {
        unsigned enemy = (uint8_t)(slot - BATTLE_FIRST_ENEMY);
        if (combatant_in_fight(g, slot) && battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot) == 0
            && enemy_can_act(g, enemy) && combatant_in_fight(g, slot))
            return enemy_turn(g, slot, pf);
        if (!((battle_member_ready(g, 0) | battle_member_ready(g, 1) | battle_member_ready(g, 2)) & MEMBER_ACTION_CHOSEN)) {
            *next = TO_THE_END;
            return true;
        }
        next_spare_member(g);
        return run_queue(g, true, next, pf);
    }
    if (!battle_timer_run_out(g, BATTLE_TIMER_TURN, slot))
        return true;
    MemberTurn turn;
    if (!member_turn(g, slot, &turn, pf))
        return false;
    if (turn == ACTED_FOR_ITSELF)
        return true;
    return run_queue(g, turn != DEMO_PICKED, next, pf);
}

/* ---- After an action: the enemies' reactions ---------------------------------------------- */

/* A beaten enemy reacts only where its reactions start with the condition
 * that brings it back (battle/enemy_conditions.c): it is back in the fight
 * for them. */
enum { COMES_BACK_TO_REACT = 0x20 };

/* The statuses that keep an enemy from reacting. */
enum { STATUSES_THAT_STOP_REACTIONS = 0x8F };

/* The enemy's turn for its reaction; the enemy it is (battle_enemy_acting)
 * is the one acting again after it. Then every combatant's update. */
static bool carry_out_reaction(GameState *g, Platform *pf)
{
    uint8_t reacting = battle_enemy_acting(g);
    if (!enemy_takes_its_turn(g, pf))
        return false;
    set_battle_enemy_acting(g, reacting);
    return update_the_combatants(g);
}

// $C1:8671 (to $C1:8809)
/* One enemy's reactions to the action: its script's reaction section looked
 * through for the actor (its foe while it reacts), and a block whose
 * conditions hold carried out at once, until one that holds and leaves the
 * outcome held, or the section's end. The reaction's place among the action's
 * targets (`entry`) indexes the per-enemy cells the original keeps it in. */
static bool react(GameState *g, unsigned entry, uint8_t target, Platform *pf)
{
    uint8_t enemy = (uint8_t)(target - BATTLE_FIRST_ENEMY);
    bool gone = !combatant_in_fight(g, target);
    set_battle_enemy_acting(g, enemy);
    uint8_t id = gone ? battle_combatant_id(g, target) : battle_combatant_fighting(g, target);
    ScriptChoosing c = enemy_choosing(g, enemy);
    TIMED_AS(g, 0xC1B488, c.place = enemy_reactions_start(g, enemy, id));
    c.block_start = c.actions_from = c.place;
    if (gone) {
        if (enemy_script_byte(g, c.block_start) != COMES_BACK_TO_REACT) {
            enemy_leave_choosing(g, &c);
            return true;
        }
        set_battle_combatant_fighting(g, target, battle_combatant_id(g, target));
    }
    set_battle_animation_kept(g, battle_enemy_animation(g, enemy));
    if (battle_stats_status(g, target) & STATUSES_THAT_STOP_REACTIONS) {
        enemy_leave_choosing(g, &c);
        return true;
    }
    for (;;) {
        c.outcome = OUTCOME_HELD;
        set_battle_foe_kept(g, battle_foes(g, enemy));
        set_battle_foes(g, enemy, battle_last_actor(g));
        set_battle_reaction_place_kept(g, battle_enemy_next_action(g, entry));
        enemy_test_block(g, &c);
        if (c.outcome == OUTCOME_HELD) {
            set_battle_foes(g, enemy, battle_last_actor(g));
            /* the turn takes the reaction's actions from the cells, and its
             * outcome and how far it read the script come back through them
             * (issue #41) */
            enemy_hand_back_choosing(g, &c);
            if (!carry_out_reaction(g, pf))
                return false;
            c = enemy_choosing_in_cells(g);
            set_battle_enemy_next_action(g, entry, battle_reaction_place_kept(g));
            set_battle_enemy_script_outcome(g, entry, c.outcome);
            if (c.outcome == OUTCOME_NOBODY_TO_ACT_ON)
                set_battle_enemy_action_chosen(g, entry, false);
        }
        if (c.outcome == OUTCOME_HELD || !enemy_script_next_block(g, &c))
            break;
        c.block_start = c.place;
        set_battle_enemy_script_block(g, entry, (uint8_t)(battle_enemy_script_block(g, entry) + 1));
        set_battle_foes(g, enemy, battle_foe_kept(g));
    }
    enemy_leave_choosing(g, &c);
    set_battle_enemy_animation(g, battle_enemy_acting(g), battle_animation_kept(g));
    return true;
}

/* Forget the actor's chosen action. The original finds it at four times the
 * actor, a byte, so an actor past the combatants lands in the tables after. */
static void forget_actors_action(GameState *g)
{
    unsigned at = (uint8_t)(battle_actor(g) * 4) / 4;
    if (battle_names_combatant((uint16_t)at))
        set_battle_chosen_actions(g, at, 0);
    else
        set_battle_chosen_actions_unchecked(g, at, 0);
}

/* Each enemy the action hit, in the order of the action's targets, reacts;
 * between them the actor's chosen action is forgotten again, and the party's
 * combination techs are looked at after each. */
static bool actor_silenced(const GameState *g, uint8_t actor)
{
    if (battle_slot_is_party(actor))
        return false;
    uint32_t status = battle_names_combatant(actor) ? battle_stats_status(g, actor) : battle_stats_status_unchecked(g, actor);
    return (status & STATUS_CONFUSED) != 0;
}

static bool enemies_react(GameState *g, Platform *pf)
{
    /* the original starts them with the choosing's cells at no enemy, no
     * pair and nothing failed */
    ScriptChoosing none_yet = enemy_choosing(g, 0);
    none_yet.pair = PAIR_NONE;
    none_yet.outcome = OUTCOME_HELD;
    enemy_leave_choosing(g, &none_yet);
    set_battle_reaction_entry(g, 0);
    for (unsigned entry = 0; entry < BATTLE_ENEMIES; entry++) {
        set_battle_reaction_entry(g, (uint8_t)entry);
        if (entry > 0)
            forget_actors_action(g);
        uint8_t target = battle_reaction_targets(g, entry);
        if (target >= BATTLE_FIRST_ENEMY && target != NOBODY && !react(g, entry, target, pf))
            return false;
        if (!check_combinations(g, pf))
            return false;
    }
    set_battle_reaction_entry(g, BATTLE_ENEMIES);
    return true;
}

/* What an action draws after it (none in a demo): the party's counters when
 * it hit no enemy; else the reactions of the enemies it hit, unless a status
 * keeps the enemy that acted from them. */
static bool follow_up(GameState *g, uint8_t actor, Platform *pf)
{
    if (battle_is_demo(g))
        return true;
    if (!battle_action_hit_enemy(g))
        return party_counters(g, pf);
    return actor_silenced(g, actor) || enemies_react(g, pf);
}

/* The turn's handler: the turn itself on the passes that look at timers;
 * then, while no party member has an action waiting, the turn no longer due;
 * then what follows every action. */
static bool after_action(GameState *g, bool skip_to_end, Platform *pf)
{
    if (!skip_to_end) {
        if (!update_the_combatants(g))
            return false;
        for (unsigned i = 0; i < BATTLE_COMBATANTS; i++)
            set_battle_reaction_targets(g, i, battle_action_targets(g, i));
        set_battle_last_actor(g, battle_actor(g));
        if (!see_to_the_knocked_out(g, pf))
            return false;
        uint8_t actor = battle_last_actor(g);
        if (!follow_up(g, actor, pf))
            return false;
    }
    if (!finish_the_beaten(g, pf))
        return false;
    set_battle_action_hit_enemy(g, false);
    if (!battle_party_defeated(g))
        enemies_choose(g);
    set_battle_actor(g, battle_last_actor(g));
    forget_actors_action(g);
    TIMED_AS(g, 0xC1AC46, battle_clear_candidates(g));
    TIMED_AS(g, 0xFDACEE, battle_clear_numbers_shown(g));
    set_battle_action_hit_enemy(g, false);
    return true;
}

static bool take_turns(GameState *g, Platform *pf)
{
    set_battle_action_hit_enemy(g, false);
    set_battle_action_hit_party(g, false);
    TurnGoesOn next = CHECK_MEMBERS;
    if (battle_loop_count(g) & 0x01) {
        if (!give_turn(g, &next, pf))
            return false;
        if (next == CHECK_MEMBERS && !check_members(g, pf))
            return false;
        if (next != TO_THE_END && !any_member_ready(g))
            set_battle_timer_kinds_due(g, battle_timer_kinds_due(g) & (uint16_t)~1u);
    }
    return after_action(g, next == TO_THE_END, pf);
}

// $C1:AC5E
/* The enemies the action beat are counted as beaten, and (but in a demo) the
 * battle shows them beaten (battle step 6); the action no longer beat any. */
static bool finish_beaten_enemies(GameState *g, Platform *pf)
{
    if (battle_enemy_beaten_any(g)) {
        for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
            if (battle_enemy_beaten(g, enemy))
                set_battle_enemy_beaten_counted(g, enemy, true);
        if (!battle_is_demo(g) && !battle_step_taken(g, pf, BATTLE_STEP_ENEMIES_BEATEN))
            return false;
    }
    set_battle_enemy_beaten_any(g, false);
    return true;
}

/* What the loop takes: the turn is given to the combatant the timers'
 * order names at the turn's place, as every battle has it; the pass's end has
 * a debugging path the ROM turns on with its last byte, off in the released
 * game, which the port refuses. */
static bool turn_names_a_combatant(const GameState *g) { return battle_names_combatant(combatant_whose_turn(g)); }
static bool debugging_switch_on(const GameState *g) { return battle_debugging_switch(g) != 0; }

/* The enemies the action beat, finished ($C1:AC5E, above). */
static bool finish_the_beaten(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, FINISH_BEATEN, done = finish_beaten_enemies(g, pf));
    return done;
}

static bool give_the_turn(GameState *g, Platform *pf)
{
    if (!turn_names_a_combatant(g))
        return battle_contract_broken(g, GIVE_TURN, "a turn whose place names no combatant");
    bool done = false;
    TIMED_AS(g, GIVE_TURN, done = take_turns(g, pf));
    return done;
}

bool battle_turn_given(GameState *g, Platform *pf) { return give_the_turn(g, pf); }

bool battle_main_loop_runs(GameState *g, Platform *pf)
{
    LoopGoesOn on = start_battle(g, pf);
    if (on == LOOP_NOT_OURS)
        return battle_contract_broken(g, STARTING_WITH_THE_ENEMIES, "a battle with enemies the port doesn't list");
    while (on == LOOP_TO_THE_HEAD) {
        if (!next_frame(g, pf))
            return false;
        on = run_pass(g, pf);
        if (on == LOOP_TO_THE_TIMERS) {
            TIMED_AS(g, BATTLE_TICK_TIMERS_ENTRY, battle_count_timers_down(g));
            on = battle_timers_seen_to(g, pf) ? LOOP_TO_THE_REST : LOOP_STOPPED;
        }
        if (on != LOOP_TO_THE_REST)
            break;
        if (debugging_switch_on(g))
            return battle_contract_broken(g, PASS_END, "a pass's end with the debugging switch on");
        TIMED_AS(g, PASS_END, on = end_pass(g, pf));
    }
    return on == LOOP_LEFT;
}
