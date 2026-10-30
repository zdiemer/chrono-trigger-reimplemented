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
 * The loop's own logic is C, one sequence for each stretch of it; its calls
 * on the battle's other routines -- the battle steps (the routine at $C1:0003
 * with the step in A, which waits for frames), the turns, carrying out the
 * party's actions, the rewards -- are a callback (LoopCalls) that the program
 * answers as C (battle_main_loop_runs) and the machine's entries from the
 * original's calls, each stretch an entry of its own there that hands on to
 * the next by the original's jumps. */
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
#include "original_effects.h"
#include "port/native.h"
#include "vars.h"
#include "wram.h"

/* ---- The loop's calls ------------------------------------------------------------------------ */

/* The loop's calls on the battle's other routines, each where the original
 * makes it. `arg` is what the call is about: a party member, an item, a
 * timer's kind, an enemy, or the rewards held. */
typedef enum {
    /* the start */
    CALL_START_THE_FIGHT,              /* battle/fight_start.c */
    CALL_SET_UP,                       /* battle step 0 */
    /* a pass */
    CALL_NEXT_FRAME,                   /* battle step 3 */
    CALL_UPDATE_IN_THE_PASS,           /* every combatant updated (battle/combatants.c) */
    CALL_CANCEL_IN_THE_PASS,           /* a member's action dropped if it can't act (battle/party_turns.c) */
    CALL_TURN_IN_THE_PASS,             /* the turn, while an action runs over passes */
    /* the battle's ends */
    CALL_STAND_DOWN_DEFEATED,          /* each member's menu closed (battle/party_turns.c) */
    CALL_DEFEAT_FRAME,                 /* battle step 3, the defeat's frames */
    CALL_DEFEAT,                       /* battle step 9 */
    CALL_VICTORY,                      /* battle step 8 */
    CALL_EXPERIENCE_TO_GOLD,           /* battle/rewards_entries.c */
    CALL_GIVE_GOLD,                    /* the gold won given */
    CALL_SHARE_EXPERIENCE,             /* battle/rewards_entries.c */
    CALL_SHOW_THE_REWARDS,             /* battle/rewards_messages.c; arg: the rewards held, or 0 */
    CALL_PUT_ITEMS_BACK_AFTER_WINNING, /* the items chosen and not used (battle/inventory.c) */
    CALL_GIVE_AN_ITEM,                 /* arg: the item won */
    CALL_RAN_AWAY_MESSAGE,             /* a battle message (battle/message.c) */
    CALL_STAND_DOWN_RUNNING,
    CALL_RUNNING_AWAY,                 /* the running away's action (battle/status_timers.c) */
    CALL_PUT_ITEMS_BACK_AFTER_RUNNING,
    /* a turn */
    CALL_ENEMY_TURN,                   /* battle/enemy_turn.c */
    CALL_MEMBER_STATUS_TURN,           /* arg: the member (battle/party_turns.c) */
    CALL_COMBINATIONS_ACTING_FOR_ITSELF, /* which members can join a combination (battle/menu_window.c) */
    CALL_ACT_FOR_ITSELF,               /* a member whose status picks its action (battle/member_turn.c) */
    CALL_OPEN_THE_MENU,                /* battle step 1 */
    CALL_CARRY_OUT,                    /* arg: the member whose action is carried out (battle/member_turn.c) */
    CALL_CHECKS_BEGIN,                 /* the members' checks, a subroutine of the turn's */
    CALL_COMBINATIONS_IN_THE_CHECKS,   /* arg: the member */
    CALL_CANCEL_IN_THE_CHECKS,         /* arg: the member */
    CALL_CHECKS_END,
    CALL_UPDATE_AFTER_THE_ACTION,
    CALL_SEE_TO_THE_KNOCKED_OUT,       /* battle/party_turns.c */
    CALL_PARTY_COUNTERS,               /* battle/party_turns.c */
    CALL_REACTION,                     /* arg: the enemy reacting; its turn (battle/enemy_turn.c) */
    CALL_UPDATE_AFTER_A_REACTION,
    CALL_COMBINATIONS_AFTER_A_REACTION,
    CALL_FINISH_THE_BEATEN,            /* the enemies the action beat (below) */
    CALL_ENEMIES_BEATEN,               /* battle step 6 */
} LoopCall;

typedef bool (*LoopCalls)(void *ctx, LoopCall call, uint8_t arg);

/* The battle's end's bookkeeping (battle/battle_end.c), each timed as the
 * routine the original calls. */
enum { CLEAR_TURN_WORK = 0xFDA982 };   /* the turns' work cleared (below) */

static void clear_party_statuses(GameState *g) { TIMED_AS(g, 0xC1EA9D, battle_clear_party_statuses(g)); }
static void stand_party_up(GameState *g) { TIMED_AS(g, 0xC1EAE8, battle_stand_party_up(g)); }
static void keep_party_records(GameState *g) { TIMED_AS(g, 0xC1B442, battle_keep_party_records(g)); }
static void take_enemies_away(GameState *g) { TIMED_AS(g, 0xC1B3BB, battle_take_enemies_away(g)); }

/* The party member the loop is working on, where the original's code it
 * calls finds it (a word on the direct page). */
static void leave_member_for_callees(GameState *g, uint8_t member)
{
    set_battle_low_scratch(g, 0x14, member);
    set_battle_low_scratch(g, 0x15, 0);
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
    ORIGINAL_EFFECT(charge_as(g, CLEAR_TURN_WORK, TURN_WORK_BYTES));
}

/* Where the battle's random numbers are drawn from its table: kept between
 * battles, and on the battle's page while it runs. */
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
    ORIGINAL_EFFECT(set_battle_low_scratch(g, 0, BATTLE_PARTY_SIZE));
    ORIGINAL_EFFECT(set_battle_low_scratch(g, 1, 0));
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
typedef enum { LOOP_STOPPED, LOOP_LEFT, LOOP_TO_THE_HEAD, LOOP_TO_THE_TIMERS, LOOP_TO_THE_REST, LOOP_NOT_OURS } LoopGoesOn;

// $C1:8000
/* Start the battle, then on to the loop's head at $C1:8120. No battle is set
 * up without enemies, or with more than there are places for: the original's
 * lists would run on through memory for one, and such a setup is the
 * original's from the step's return. */
static LoopGoesOn start_battle(GameState *g, LoopCalls call, void *ctx)
{
    TIMED_AS(g, CLEAR_TURN_WORK, battle_clear_the_turn_work(g));
    take_random_position(g);
    if (!call(ctx, CALL_START_THE_FIGHT, 0))
        return LOOP_STOPPED;
    set_demo_aside(g);
    if (!call(ctx, CALL_SET_UP, 0))
        return LOOP_STOPPED;
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
static bool next_frame(GameState *g, LoopCalls call, void *ctx)
{
    set_battle_loop_count(g, (uint8_t)(battle_loop_count(g) + 1));
    set_battle_damage_hit(g, 0);
    return call(ctx, CALL_NEXT_FRAME, 0);
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

/* The party's maximum HP goes back to what it was when the battle began. */
static void restore_party_max_hp(GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_stats_max_hp(g, member, battle_party_max_hp_kept(g, member));
}

/* $C1:8443: the battle's last step, into the field again ($C1:0006): the
 * random position kept for the next battle, and each member's carried byte
 * back into its stat block. */
static LoopGoesOn leave_battle(GameState *g)
{
    set_battle_random_kept(g, battle_random_position(g));
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_stats_carried(g, member, battle_member_carried_weapon(g, member));
    TIMED_AS(g, 0xFDAAB0, battle_note_beaten_enemies(g));
    return LOOP_LEFT;
}

/* $C1:8423: a battle that is lost, or run from, or a demo's end: the party's
 * statuses cleared, the party revived, its stats kept. */
static LoopGoesOn leave_without_rewards(GameState *g)
{
    restore_party_max_hp(g);
    clear_party_statuses(g);
    stand_party_up(g);
    keep_party_records(g);
    return leave_battle(g);
}

/* The six items a victory can give. */
enum { ITEMS_WON = 6 };

/* $C1:83BB: a battle that is won: unless it is a demo, the rewards -- the
 * experience turned to gold where the party wears what does that, the gold
 * given, the experience and the tech points shared out (battle/rewards.h),
 * their messages, the party revived, its stats kept, the items it chose and
 * didn't use put back, and the items won. */
static LoopGoesOn leave_with_rewards(GameState *g, LoopCalls call, void *ctx)
{
    restore_party_max_hp(g);
    if (battle_is_demo(g))
        return leave_without_rewards(g);
    uint8_t held = battle_rewards_held(g);
    if (held == 0) {
        if (!call(ctx, CALL_EXPERIENCE_TO_GOLD, 0) || !call(ctx, CALL_GIVE_GOLD, 0))
            return LOOP_STOPPED;
        set_battle_reward_flags(g, battle_reward_flags(g) | REWARD_GOLD);
        if (!call(ctx, CALL_SHARE_EXPERIENCE, 0))
            return LOOP_STOPPED;
    }
    if (!call(ctx, CALL_SHOW_THE_REWARDS, held))
        return LOOP_STOPPED;
    stand_party_up(g);
    keep_party_records(g);
    if (!call(ctx, CALL_PUT_ITEMS_BACK_AFTER_WINNING, 0))
        return LOOP_STOPPED;
    for (unsigned item = 0; item < ITEMS_WON; item++) {
        ORIGINAL_EFFECT(set_battle_low_scratch(g, 0x10, (uint8_t)item));
        uint8_t won = battle_items_won(g, item);
        if (won == 0)
            continue;
        if (!call(ctx, CALL_GIVE_AN_ITEM, won))
            return LOOP_STOPPED;
        set_battle_reward_flags(g, battle_reward_flags(g) | REWARD_ITEMS);
    }
    ORIGINAL_EFFECT(set_battle_low_scratch(g, 0x10, ITEMS_WON));
    return leave_battle(g);
}

/* The party is all out: its defeat, over eight frames and the defeat's step,
 * and away without rewards. */
enum { DEFEAT_FRAMES = 8 };

static LoopGoesOn lose(GameState *g, LoopCalls call, void *ctx)
{
    clear_party_statuses(g);
    take_enemies_away(g);
    battle_ends(g, BATTLE_DEFEATED);
    if (!call(ctx, CALL_STAND_DOWN_DEFEATED, 0))
        return LOOP_STOPPED;
    set_battle_defeat_showing(g, true);
    for (unsigned frame = 0; frame < DEFEAT_FRAMES; frame++) {
        ORIGINAL_EFFECT(set_battle_low_scratch(g, 0, (uint8_t)frame));
        if (!call(ctx, CALL_DEFEAT_FRAME, 0))
            return LOOP_STOPPED;
    }
    ORIGINAL_EFFECT(set_battle_low_scratch(g, 0, DEFEAT_FRAMES));
    set_battle_defeat_showing(g, false);
    if (!call(ctx, CALL_DEFEAT, 0))
        return LOOP_STOPPED;
    return leave_without_rewards(g);
}

/* Every enemy is beaten: the victory, and away with the rewards. */
enum { VICTORY_MARK = 0xFF };

static LoopGoesOn win(GameState *g, LoopCalls call, void *ctx)
{
    take_enemies_away(g);
    battle_ends(g, BATTLE_WON_OR_RAN);
    set_battle_won(g, VICTORY_MARK);
    clear_party_statuses(g);
    if (!call(ctx, CALL_VICTORY, 0))
        return LOOP_STOPPED;
    return leave_with_rewards(g, call, ctx);
}

/* The party runs away: the running away's marks and message, the party
 * standing down, the running away's action, and away without rewards. */
static LoopGoesOn run_away(GameState *g, LoopCalls call, void *ctx)
{
    battle_ends(g, BATTLE_WON_OR_RAN);
    TIMED_AS(g, 0xFDAA98, battle_count_enemies_out(g));
    if (!call(ctx, CALL_RAN_AWAY_MESSAGE, 0))
        return LOOP_STOPPED;
    clear_party_statuses(g);
    if (!call(ctx, CALL_STAND_DOWN_RUNNING, 0) || !call(ctx, CALL_RUNNING_AWAY, 0))
        return LOOP_STOPPED;
    stand_party_up(g);
    keep_party_records(g);
    if (!call(ctx, CALL_PUT_ITEMS_BACK_AFTER_RUNNING, 0))
        return LOOP_STOPPED;
    take_enemies_away(g);
    return leave_without_rewards(g);
}

/* The party's low HP noted, and every combatant's statuses followed
 * (battle/combatants.c). */
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
static bool cancel_unable_members(GameState *g, LoopCalls call, void *ctx)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_member_updating(g, (uint8_t)member);
        if (battle_combatant_id(g, member) == COMBATANT_NONE)
            continue;
        if (!call(ctx, CALL_CANCEL_IN_THE_PASS, (uint8_t)member))
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
static LoopGoesOn run_pass(GameState *g, LoopCalls call, void *ctx)
{
    TIMED_AS(g, 0xFDACFD, battle_clear_damage_table(g));
    if (!call(ctx, CALL_UPDATE_IN_THE_PASS, 0))
        return LOOP_STOPPED;
    if (battle_party_defeated(g))
        return lose(g, call, ctx);
    if (enemies_beaten(g))
        return win(g, call, ctx);
    follow_combatants(g);
    if (battle_timers_held(g))
        return cancel_unable_members(g, call, ctx) ? LOOP_TO_THE_REST : LOOP_STOPPED;
    if (!battle_is_demo(g) && battle_run_counter(g) != 0)
        return run_away(g, call, ctx);
    if (battle_action_running(g) == 0)
        return LOOP_TO_THE_TIMERS;
    return call(ctx, CALL_TURN_IN_THE_PASS, 0) ? LOOP_TO_THE_REST : LOOP_STOPPED;
}

// $C1:8345
/* The end of a pass: back to the loop's head, or a demo battle's end. (A
 * debugging path the ROM can turn on, off in the released game, is the
 * original's: the entry's guard.) */
static LoopGoesOn end_pass(GameState *g, LoopCalls call, void *ctx)
{
    if (!battle_is_demo(g))
        return LOOP_TO_THE_HEAD;
    TIMED_AS(g, 0xC1B3D2, battle_keep_standing_enemies(g));
    return leave_with_rewards(g, call, ctx);
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
static bool enemy_turn(GameState *g, uint8_t slot, LoopCalls call, void *ctx)
{
    set_battle_enemy_acting(g, (uint8_t)(slot - BATTLE_FIRST_ENEMY));
    if (!call(ctx, CALL_ENEMY_TURN, 0))
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

static bool member_turn(GameState *g, uint8_t member, MemberTurn *turn, LoopCalls call, void *ctx)
{
    if (!call(ctx, CALL_MEMBER_STATUS_TURN, member))
        return false;
    leave_member_for_callees(g, member);
    if (battle_member_acts_for_itself(g)) {
        *turn = ACTED_FOR_ITSELF;
        set_battle_acting_for_itself(g, member);
        return call(ctx, CALL_COMBINATIONS_ACTING_FOR_ITSELF, 0) && call(ctx, CALL_ACT_FOR_ITSELF, 0);
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
    return call(ctx, CALL_OPEN_THE_MENU, 0);
}

/* The next party member whose chosen action is carried out, from the queue of
 * them; none is $FF. */
enum { NOBODY = 0xFF };

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

/* Carry out the member's action, if its record still says it chose one (its
 * chosen bit as when it was noted ready): the action, then both forgotten. */
static bool carry_out(GameState *g, uint8_t member, bool *carried, LoopCalls call, void *ctx)
{
    *carried = ((member_action(g, member) ^ battle_member_ready(g, member)) & MEMBER_ACTION_CHOSEN) != 0;
    if (!*carried)
        return true;
    set_battle_actor(g, member);
    if (!call(ctx, CALL_CARRY_OUT, member))
        return false;
    set_battle_member_ready(g, member, MEMBER_NO_ACTION);
    set_member_action(g, member, MEMBER_NO_ACTION);
    return true;
}

// $C1:883D
/* Every member in a slot: its combination techs checked, and its action
 * dropped if its status keeps it from acting. It is a subroutine of the
 * turn's. */
static bool check_members(GameState *g, LoopCalls call, void *ctx)
{
    if (!call(ctx, CALL_CHECKS_BEGIN, 0))
        return false;
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_member_updating(g, (uint8_t)member);
        if (battle_combatant_id(g, member) == COMBATANT_NONE)
            continue;
        leave_member_for_callees(g, (uint8_t)member);
        if (!call(ctx, CALL_COMBINATIONS_IN_THE_CHECKS, (uint8_t)member)
            || !call(ctx, CALL_CANCEL_IN_THE_CHECKS, (uint8_t)member))
            return false;
    }
    set_battle_member_updating(g, BATTLE_PARTY_SIZE);
    return call(ctx, CALL_CHECKS_END, 0);
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

/* The queue: the party's chosen actions carried out one at a time. */
static bool run_queue(GameState *g, bool note_ready, TurnGoesOn *next, LoopCalls call, void *ctx)
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
    if (!carry_out(g, member, &carried, call, ctx))
        return false;
    /* the third member's goes on to the members' checks, the others' past them */
    if (member != BATTLE_PARTY_SIZE - 1 || !carried)
        *next = SKIP_CHECKS;
    return true;
}

// $C1:8461 (to $C1:8626)
/* The turn of the combatant whose turn timer ran out (on the passes the timers
 * are looked at): an enemy's, or a party member's, then the party's queued
 * actions. */
static bool give_turn(GameState *g, TurnGoesOn *next, LoopCalls call, void *ctx)
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
            return enemy_turn(g, slot, call, ctx);
        if (!((battle_member_ready(g, 0) | battle_member_ready(g, 1) | battle_member_ready(g, 2)) & MEMBER_ACTION_CHOSEN)) {
            *next = TO_THE_END;
            return true;
        }
        next_spare_member(g);
        return run_queue(g, true, next, call, ctx);
    }
    if (!battle_timer_run_out(g, BATTLE_TIMER_TURN, slot))
        return true;
    MemberTurn turn;
    if (!member_turn(g, slot, &turn, call, ctx))
        return false;
    if (turn == ACTED_FOR_ITSELF)
        return true;
    return run_queue(g, turn != DEMO_PICKED, next, call, ctx);
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
static bool carry_out_reaction(GameState *g, LoopCalls call, void *ctx)
{
    uint8_t reacting = battle_enemy_acting(g);
    if (!call(ctx, CALL_REACTION, reacting))
        return false;
    set_battle_enemy_acting(g, reacting);
    return call(ctx, CALL_UPDATE_AFTER_A_REACTION, 0);
}

// $C1:8671 (to $C1:8809)
/* One enemy's reactions to the action: its script's reaction section looked
 * through for the actor (its foe while it reacts), and a block whose
 * conditions hold carried out at once, until one that holds and leaves the
 * outcome held, or the section's end. The reaction's place among the action's
 * targets (`entry`) indexes the per-enemy cells the original keeps it in. */
static bool react(GameState *g, unsigned entry, uint8_t target, LoopCalls call, void *ctx)
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
            /* the turn is a machine entry still: it takes the reaction's
             * actions from the cells, and its outcome and how far it read the
             * script come back through them (docs/TODO.md) */
            enemy_hand_back_choosing(g, &c);
            if (!carry_out_reaction(g, call, ctx))
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

/* A status that keeps the actor's script from its reactions, read wherever the
 * actor's cell points. */
static bool actor_silenced(const GameState *g, uint8_t actor)
{
    if (battle_slot_is_party(actor))
        return false;
    uint32_t status = battle_names_combatant(actor) ? battle_stats_status(g, actor) : battle_stats_status_unchecked(g, actor);
    return (status & STATUS_CONFUSED) != 0;
}

/* Each enemy the action hit, in the order of the action's targets, reacts;
 * between them the actor's chosen action is forgotten again, and the party's
 * combination techs are looked at after each. */
static bool enemies_react(GameState *g, LoopCalls call, void *ctx)
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
        if (target >= BATTLE_FIRST_ENEMY && target != NOBODY && !react(g, entry, target, call, ctx))
            return false;
        if (!call(ctx, CALL_COMBINATIONS_AFTER_A_REACTION, 0))
            return false;
    }
    set_battle_reaction_entry(g, BATTLE_ENEMIES);
    return true;
}

/* What an action draws after it (none in a demo): the party's counters when
 * it hit no enemy; else the reactions of the enemies it hit, unless a status
 * keeps the enemy that acted from them. */
static bool follow_up(GameState *g, uint8_t actor, LoopCalls call, void *ctx)
{
    if (battle_is_demo(g))
        return true;
    if (!battle_action_hit_enemy(g))
        return call(ctx, CALL_PARTY_COUNTERS, 0);
    return actor_silenced(g, actor) || enemies_react(g, call, ctx);
}

// $C1:8626 (to the end)
/* After the turn: every combatant updated, the action's targets and actor
 * kept, those knocked out seen to; then, where the action hit an enemy (and
 * isn't a demo's, nor an enemy's that a status silences), each enemy it hit
 * reacts, or else the party members counter; then the beaten enemies
 * finished, the enemies' scripts started again (unless the party is
 * beaten), and the action's targets and numbers cleared. */
static bool after_action(GameState *g, bool skip_to_end, LoopCalls call, void *ctx)
{
    if (!skip_to_end) {
        if (!call(ctx, CALL_UPDATE_AFTER_THE_ACTION, 0))
            return false;
        for (unsigned i = 0; i < BATTLE_COMBATANTS; i++)
            set_battle_reaction_targets(g, i, battle_action_targets(g, i));
        set_battle_last_actor(g, battle_actor(g));
        if (!call(ctx, CALL_SEE_TO_THE_KNOCKED_OUT, 0))
            return false;
        uint8_t actor = battle_last_actor(g);
        if (!follow_up(g, actor, call, ctx))
            return false;
    }
    if (!call(ctx, CALL_FINISH_THE_BEATEN, 0))
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

/* The turn's handler: the turn itself on the passes that look at timers;
 * then, while no party member has an action waiting, the turn no longer due;
 * then what follows every action. */
static bool take_turns(GameState *g, LoopCalls call, void *ctx)
{
    set_battle_action_hit_enemy(g, false);
    set_battle_action_hit_party(g, false);
    TurnGoesOn next = CHECK_MEMBERS;
    if (battle_loop_count(g) & 0x01) {
        if (!give_turn(g, &next, call, ctx))
            return false;
        if (next == CHECK_MEMBERS && !check_members(g, call, ctx))
            return false;
        if (next != TO_THE_END && !any_member_ready(g))
            set_battle_timer_kinds_due(g, battle_timer_kinds_due(g) & (uint16_t)~1u);
    }
    return after_action(g, next == TO_THE_END, call, ctx);
}

// $C1:AC5E
/* The enemies the action beat are counted as beaten, and (but in a demo) the
 * battle shows them beaten (battle step 6); the action no longer beat any. */
static bool finish_beaten_enemies(GameState *g, LoopCalls call, void *ctx)
{
    if (battle_enemy_beaten_any(g)) {
        for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
            if (battle_enemy_beaten(g, enemy))
                set_battle_enemy_beaten_counted(g, enemy, true);
        if (!battle_is_demo(g) && !call(ctx, CALL_ENEMIES_BEATEN, 0))
            return false;
    }
    set_battle_enemy_beaten_any(g, false);
    return true;
}

/* The guards' state halves: the turn is given to the combatant the timers'
 * order names at the turn's place, as every battle has it; the pass's end has
 * a debugging path the ROM turns on with its last byte, off in the released
 * game, which reads the controller's register. */
static bool turn_names_a_combatant(const GameState *g) { return battle_names_combatant(combatant_whose_turn(g)); }
static bool debugging_switch_on(const GameState *g) { return battle_debugging_switch(g) != 0; }

/* ---- As C ------------------------------------------------------------------------------------ */

typedef struct {
    GameState *game;
    Platform *pf;
} BattleLoopContext;

/* The routines the calls stand for, as the program goes no further at them
 * where their guards turn the state away or they are only the machine's. */
enum {
    START_FIGHT = 0xC1FA8B, UPDATE_COMBATANTS = 0xC1B223, CANCEL_IF_UNABLE = 0xC1B3F9, GIVE_TURN = 0xC18461,
    PARTY_STANDS_DOWN = 0xC1B7F2, EXPERIENCE_TO_GOLD = 0xFDB201, GIVE_GOLD = 0xC1D0A2, SHARE_EXPERIENCE = 0xC1F93E,
    SHOW_REWARDS = 0xFDAD17, PUT_ITEMS_BACK = 0xC1B4E9, GIVE_ITEM = 0xC1D005, SHOW_MESSAGE = 0xCD0021,
    START_ACTION = 0xC1895B, ENEMY_TURN = 0xC18CF9, MEMBER_TURN = 0xFDAB30, CHECK_COMBINATIONS = 0xC1B575,
    MEMBER_ACTS_FOR_ITSELF = 0xC1BCE1, CARRY_OUT_ACTION = 0xC1B967, AFTER_ACTION = 0xC1B762,
    PARTY_COUNTERS = 0xFDAC6E, FINISH_BEATEN = 0xC1AC5E, LOOP_START = 0xC18000, PASS_END = 0xC18345,
    STARTING_WITH_THE_ENEMIES = 0xC18030,
};

/* The running away's message and its action. */
enum { RAN_AWAY_MESSAGE = 8, RUN_AWAY_ACTION = 0x79 };

static bool take_the_step(BattleLoopContext *c, BattleStep step) { return battle_step_taken(c->game, c->pf, step); }

static bool give_the_turn(BattleLoopContext *c);

static bool make_the_loop_s_call(void *ctx, LoopCall call, uint8_t arg)
{
    BattleLoopContext *c = ctx;
    GameState *g = c->game;
    (void)arg;
    switch (call) {
    case CALL_START_THE_FIGHT:
        if (!setup_party_is_real(g))
            return battle_needs_the_machine(g, START_FIGHT, "a fight's start with a party the port doesn't take");
        TIMED_AS(g, START_FIGHT, battle_start_fight(g));
        return true;
    case CALL_SET_UP: return take_the_step(c, BATTLE_STEP_SET_UP);
    case CALL_NEXT_FRAME:
    case CALL_DEFEAT_FRAME: return take_the_step(c, BATTLE_STEP_NEXT_FRAME);
    case CALL_DEFEAT: return take_the_step(c, BATTLE_STEP_DEFEAT);
    case CALL_VICTORY: return take_the_step(c, BATTLE_STEP_VICTORY);
    case CALL_OPEN_THE_MENU: return take_the_step(c, BATTLE_STEP_OPEN_MENU);
    case CALL_ENEMIES_BEATEN: return take_the_step(c, BATTLE_STEP_ENEMIES_BEATEN);
    case CALL_TURN_IN_THE_PASS: return give_the_turn(c);
    case CALL_CHECKS_BEGIN:
    case CALL_CHECKS_END: return true;
    case CALL_FINISH_THE_BEATEN: {
        bool done = false;
        TIMED_AS(g, FINISH_BEATEN, done = finish_beaten_enemies(g, make_the_loop_s_call, ctx));
        return done;
    }
    case CALL_UPDATE_IN_THE_PASS:
    case CALL_UPDATE_AFTER_THE_ACTION:
    case CALL_UPDATE_AFTER_A_REACTION: {
        bool done = false;
        TIMED_AS(g, UPDATE_COMBATANTS, done = battle_update_all_combatants(g));
        return done;
    }
    case CALL_CANCEL_IN_THE_PASS:
    case CALL_CANCEL_IN_THE_CHECKS: {
        bool done = false;
        TIMED_AS(g, CANCEL_IF_UNABLE, done = battle_cancel_if_unable(g, c->pf, arg));
        return done;
    }
    case CALL_STAND_DOWN_DEFEATED:
    case CALL_STAND_DOWN_RUNNING: {
        bool done = false;
        TIMED_AS(g, PARTY_STANDS_DOWN, done = battle_party_stands_down(g, c->pf));
        return done;
    }
    case CALL_MEMBER_STATUS_TURN: {
        bool done = false;
        TIMED_AS(g, MEMBER_TURN, done = battle_member_status_turn(g, c->pf, arg));
        return done;
    }
    case CALL_SEE_TO_THE_KNOCKED_OUT: {
        bool done = false;
        TIMED_AS(g, AFTER_ACTION, done = battle_see_to_knocked_out_enemies(g, c->pf));
        return done;
    }
    case CALL_PARTY_COUNTERS: {
        bool done = false;
        TIMED_AS(g, PARTY_COUNTERS, done = battle_party_counters(g, c->pf));
        return done;
    }
    case CALL_COMBINATIONS_ACTING_FOR_ITSELF:
    case CALL_COMBINATIONS_IN_THE_CHECKS:
    case CALL_COMBINATIONS_AFTER_A_REACTION: {
        bool done = false;
        TIMED_AS(g, CHECK_COMBINATIONS, done = battle_update_combo_members(g, c->pf));
        return done;
    }
    case CALL_ENEMY_TURN:
    case CALL_REACTION: {
        bool done = false;
        TIMED_AS(g, ENEMY_TURN, done = battle_enemy_takes_turn(g, c->pf));
        return done;
    }
    case CALL_CARRY_OUT: {
        bool done = false;
        TIMED_AS(g, CARRY_OUT_ACTION, done = battle_carry_out_member_action(g, c->pf));
        return done;
    }
    case CALL_ACT_FOR_ITSELF: {
        bool done = false;
        TIMED_AS(g, MEMBER_ACTS_FOR_ITSELF, done = battle_end_member_turn(g));
        return done;
    }
    case CALL_EXPERIENCE_TO_GOLD: TIMED_AS(g, EXPERIENCE_TO_GOLD, battle_turn_experience_to_gold(g)); return true;
    case CALL_GIVE_GOLD: TIMED_AS(g, GIVE_GOLD, (void)battle_give_gold(g, battle_gold_won(g))); return true;
    case CALL_SHARE_EXPERIENCE:
        if (!battle_experience_can_be_shared(g))
            return battle_needs_the_machine(g, SHARE_EXPERIENCE, "the experience shared with records the port doesn't take");
        TIMED_AS(g, SHARE_EXPERIENCE, battle_share_experience(g));
        return true;
    case CALL_SHOW_THE_REWARDS: {
        bool done = false;
        TIMED_AS(g, SHOW_REWARDS, done = battle_show_the_rewards(g, c->pf));
        return done;
    }
    case CALL_PUT_ITEMS_BACK_AFTER_WINNING:
    case CALL_PUT_ITEMS_BACK_AFTER_RUNNING: TIMED_AS(g, PUT_ITEMS_BACK, battle_put_items_back(g)); return true;
    case CALL_GIVE_AN_ITEM: TIMED_AS(g, GIVE_ITEM, (void)battle_inventory_add(g, arg)); return true;
    case CALL_RAN_AWAY_MESSAGE: {
        bool done = false;
        TIMED_AS(g, SHOW_MESSAGE, done = battle_show_a_battle_message(g, c->pf, RAN_AWAY_MESSAGE));
        return done;
    }
    case CALL_RUNNING_AWAY: {
        bool done = false;
        TIMED_AS(g, START_ACTION, done = battle_show_status_number(g, c->pf, RUN_AWAY_ACTION));
        return done;
    }
    default: return false;
    }
}

static bool give_the_turn(BattleLoopContext *c)
{
    if (!turn_names_a_combatant(c->game))
        return battle_needs_the_machine(c->game, GIVE_TURN, "a turn whose place names no combatant");
    bool done = false;
    TIMED_AS(c->game, GIVE_TURN, done = take_turns(c->game, make_the_loop_s_call, c));
    return done;
}

bool battle_turn_given(GameState *g, Platform *pf)
{
    BattleLoopContext c = { g, pf };
    return give_the_turn(&c);
}

bool battle_main_loop_runs(GameState *g, Platform *pf)
{
    BattleLoopContext c = { g, pf };
    LoopGoesOn on = start_battle(g, make_the_loop_s_call, &c);
    if (on == LOOP_NOT_OURS)
        return battle_needs_the_machine(g, STARTING_WITH_THE_ENEMIES, "a battle with enemies the port doesn't list");
    while (on == LOOP_TO_THE_HEAD) {
        if (!next_frame(g, make_the_loop_s_call, &c))
            return false;
        on = run_pass(g, make_the_loop_s_call, &c);
        if (on == LOOP_TO_THE_TIMERS) {
            TIMED_AS(g, BATTLE_TICK_TIMERS_ENTRY, battle_count_timers_down(g));
            on = battle_timers_seen_to(g, pf) ? LOOP_TO_THE_REST : LOOP_STOPPED;
        }
        if (on != LOOP_TO_THE_REST)
            break;
        if (debugging_switch_on(g))
            return battle_needs_the_machine(g, PASS_END, "a pass's end with the debugging switch on");
        TIMED_AS(g, PASS_END, on = end_pass(g, make_the_loop_s_call, &c));
    }
    return on == LOOP_LEFT;
}

/* ---- On the machine ------------------------------------------------------------------------ */

/* The original's code runs as the battle's does: 8-bit A, 16-bit index
 * registers, binary. Most of the loop's callees read nothing of the registers
 * but what each call gives; some read the carry or the overflow the loop's
 * code before them leaves, or A (all of it, where they take its high byte
 * along), and those calls pass them as the original has them. */
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_FLAGS,
    READS_CARRY = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_V,
    READS_OVERFLOW = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C,
    READS_A = DC_X | DC_Y | DC_FLAGS,
    READS_A_AND_OVERFLOW = DC_X | DC_Y | DC_N | DC_Z | DC_C,
    READS_A_HIGH = DC_AL | DC_X | DC_Y | DC_FLAGS,
    READS_Y_AND_OVERFLOW = DC_A | DC_X | DC_N | DC_Z | DC_C,
};

static bool step(Snes *s, uint32_t site, BattleStep number)
{
    /* the overflow flag the step saves with the flags across its frame waits
     * is read by nothing after. The static result, V dead into $C1:0003, is
     * the proof at every site; the run exercises the menu's ($C1:856D), and
     * the other five, which the library reaches fewer than 100 times, rest on
     * the static result alone (dc-evidence:
     * tools/v_readers.py C1:0003: V dead, its tables' indexes at most $FE (A's
     * high byte known clear, on the battle's direct page $0000); ct_dev
     * --poison-set V at each site over the library, calls with V inverted:
     * C1802D 20, C18151 32, C18161 4, C18188 11, C1856D 285,
     * C1AC7E 4; 0 differing; a site under 100 rests on the static result) */
    return battle_take_step(s, site, number, READS_A);
}

/* Which members can join a combination tech (battle/menu_window.c), run as
 * the loop's JSR at `site` calls it. */
static bool check_combinations(Snes *s, uint32_t site)
{
    return battle_run(s, site, CHECK_COMBINATIONS, battle_update_combo_members_entry);
}

/* Every combatant's update, run as the loop's JSR at `site` calls it. */
static bool update_combatants(Snes *s, uint32_t site)
{
    return battle_run(s, site, UPDATE_COMBATANTS, battle_update_all_combatants_entry);
}

/* A member's action dropped where its status keeps it from acting, with the
 * member in X and A. */
static bool cancel_if_unable(Snes *s, uint32_t site, uint8_t member)
{
    s->cpu.x = member;
    s->cpu.a = member;
    return battle_run(s, site, CANCEL_IF_UNABLE, battle_cancel_if_unable_entry);
}

/* The member's action carried out, from the call the queue makes for it. */
static const uint16_t carry_out_calls[BATTLE_PARTY_SIZE] = { 0x85CE, 0x85E7, 0x8600 };

/* The members' checks are a subroutine of the turn's, called from $C1:860A,
 * whose return is on the stack under its calls. */
enum { CHECKS_CALL_LAST_BYTE = 0x860C };


static bool call_on_the_machine(void *ctx, LoopCall call, uint8_t arg)
{
    Snes *s = ctx;
    GameState *g = state_of(s);
    switch (call) {
    case CALL_START_THE_FIGHT:
        /* the fight's start (battle/fight_start.c), run as C */
        if (!battle_run(s, 0xC18012, START_FIGHT, battle_start_fight_entry))
            return false;
        m_sep(s, CPU_FLAG_M);
        return true;
    case CALL_SET_UP: return step(s, 0xC1802D, BATTLE_STEP_SET_UP);
    case CALL_NEXT_FRAME:
        s->cpu.a = BATTLE_STEP_NEXT_FRAME;
        m_set_nz8(s, BATTLE_STEP_NEXT_FRAME);
        /* the frame's work leaves the overflow to nothing that reads it */
        return battle_take_step(s, 0xC18129, BATTLE_STEP_NEXT_FRAME, 0);
    case CALL_UPDATE_IN_THE_PASS: return update_combatants(s, 0xC18130);
    case CALL_CANCEL_IN_THE_PASS: return cancel_if_unable(s, 0xC181DB, arg);
    case CALL_TURN_IN_THE_PASS: return battle_take_turn(s, 0xC1822B);
    case CALL_STAND_DOWN_DEFEATED: return battle_run(s, 0xC18143, PARTY_STANDS_DOWN, battle_party_stands_down_entry);
    case CALL_DEFEAT_FRAME: return step(s, 0xC18151, BATTLE_STEP_NEXT_FRAME);
    case CALL_DEFEAT: return step(s, 0xC18161, BATTLE_STEP_DEFEAT);
    case CALL_VICTORY:
        /* the victory's step takes A's high byte as the clearing leaves it, clear */
        s->cpu.a = 0;
        return step(s, 0xC18188, BATTLE_STEP_VICTORY);
    case CALL_EXPERIENCE_TO_GOLD:
        return battle_run_long(s, 0xC183E0, EXPERIENCE_TO_GOLD, battle_turn_experience_to_gold_entry);
    case CALL_GIVE_GOLD:
        s->cpu.y = battle_gold_won(g);
        return battle_call(s, 0xC183E7, GIVE_GOLD, DC_A | DC_X | DC_FLAGS);
    case CALL_SHARE_EXPERIENCE:
        /* the flags in A as the original calls it, which it leaves with what
         * the messages take along */
        s->cpu.a = battle_reward_flags(g);
        return battle_run(s, 0xC183F2, SHARE_EXPERIENCE, battle_share_experience_entry);
    case CALL_SHOW_THE_REWARDS:
        /* the messages (battle/rewards_messages.c) take A's high byte clear,
         * as the code before leaves it, and the rewards held where they are */
        if (arg != 0)
            s->cpu.a = arg;
        return battle_run_long(s, 0xC183F5, SHOW_REWARDS, battle_show_the_rewards_entry);
    case CALL_PUT_ITEMS_BACK_AFTER_WINNING: return battle_run(s, 0xC183FF, PUT_ITEMS_BACK, battle_put_items_back_entry);
    case CALL_GIVE_AN_ITEM:
        s->cpu.y = arg;
        return battle_call(s, 0xC1840E, GIVE_ITEM, DC_A | DC_X | DC_FLAGS);
    case CALL_RAN_AWAY_MESSAGE:
        /* the message in A, its high byte clear as the counting leaves it */
        s->cpu.a = RAN_AWAY_MESSAGE;
        return battle_run_long(s, 0xC18203, SHOW_MESSAGE, battle_show_a_battle_message_entry);
    case CALL_STAND_DOWN_RUNNING: return battle_run(s, 0xC1820A, PARTY_STANDS_DOWN, battle_party_stands_down_entry);
    case CALL_RUNNING_AWAY:
        m_set_a8(s, RUN_AWAY_ACTION);
        return battle_call(s, 0xC1820F, START_ACTION, READS_A_AND_OVERFLOW);
    case CALL_PUT_ITEMS_BACK_AFTER_RUNNING: return battle_run(s, 0xC18218, PUT_ITEMS_BACK, battle_put_items_back_entry);
    case CALL_ENEMY_TURN: return battle_run(s, 0xC184D6, ENEMY_TURN, battle_enemy_takes_turn_entry);
    case CALL_MEMBER_STATUS_TURN:
        /* the turn starts from A clear, as the original's code before it
         * leaves it; its attack leaves the high byte so for the combination
         * check after */
        s->cpu.a = 0;
        s->cpu.y = arg;
        s->cpu.x = (uint16_t)(arg * BATTLE_STAT_BLOCK_BYTES);
        return battle_run_long(s, 0xC18526, MEMBER_TURN, battle_member_status_turn_entry);
    case CALL_COMBINATIONS_ACTING_FOR_ITSELF: return check_combinations(s, 0xC18538);
    case CALL_ACT_FOR_ITSELF: return battle_call(s, 0xC1853B, MEMBER_ACTS_FOR_ITSELF, READS_A_HIGH);
    case CALL_OPEN_THE_MENU: return step(s, 0xC1856D, BATTLE_STEP_OPEN_MENU);
    case CALL_CARRY_OUT: return battle_call(s, 0xC10000u | carry_out_calls[arg], CARRY_OUT_ACTION, READS_NOTHING);
    case CALL_CHECKS_BEGIN: m_push16(s, CHECKS_CALL_LAST_BYTE); return true;
    case CALL_COMBINATIONS_IN_THE_CHECKS:
        s->cpu.x = arg;
        m_set_a8(s, arg);
        return check_combinations(s, 0xC1884B);
    case CALL_CANCEL_IN_THE_CHECKS: return cancel_if_unable(s, 0xC18853, arg);
    case CALL_CHECKS_END: m_pull16(s); return true;
    case CALL_UPDATE_AFTER_THE_ACTION: return update_combatants(s, 0xC18626);
    case CALL_SEE_TO_THE_KNOCKED_OUT: return battle_run(s, 0xC1863D, AFTER_ACTION, battle_see_to_knocked_out_enemies_entry);
    case CALL_PARTY_COUNTERS: return battle_run_long(s, 0xC18650, PARTY_COUNTERS, battle_party_counters_entry);
    case CALL_REACTION:
        /* the original keeps the reacting enemy on the stack under the call,
         * which is the frame the turn's calls are made over */
        m_push8(s, arg);
        if (!battle_run(s, 0xC18780, ENEMY_TURN, battle_enemy_takes_turn_entry))
            return false;
        m_pull8(s);
        return true;
    case CALL_UPDATE_AFTER_A_REACTION: return update_combatants(s, 0xC18787);
    case CALL_COMBINATIONS_AFTER_A_REACTION: return check_combinations(s, 0xC18809);
    case CALL_FINISH_THE_BEATEN: return battle_run(s, 0xC1881C, FINISH_BEATEN, battle_finish_beaten_enemies);
    case CALL_ENEMIES_BEATEN: return step(s, 0xC1AC7E, BATTLE_STEP_ENEMIES_BEATEN);
    default: return false;
    }
}

/* Where a stretch goes on on the machine: the loop's head, which starts from
 * A and the flags cleared; the pass's body after the frame's step; the timers
 * (in 16-bit A, as the original's test of the running action leaves it); the
 * rest of the pass ($C1:8345); and the battle's last step into the field
 * ($C1:0006), which takes A's high byte as the noting leaves it, clear. */
enum { LOOP_HEAD = 0xC18120, PASS_BODY = 0xC1812C, TIMERS = 0xC18231, REST_OF_PASS = PASS_END,
       RETURN_TO_THE_FIELD = 0xC10006 };
enum { LOOP_HEAD_DEAD = DC_A | DC_X | DC_Y | DC_FLAGS, PASS_STOP_DEAD = DC_A | DC_X | DC_Y | DC_FLAGS };

static void go_on_on_the_machine(Snes *s, LoopGoesOn on)
{
    switch (on) {
    case LOOP_LEFT:
        s->cpu.a &= 0x00FF;
        m_stop_at(s, RETURN_TO_THE_FIELD, DC_X | DC_Y | DC_FLAGS | DC_AL);
        return;
    case LOOP_TO_THE_HEAD: m_stop_at(s, LOOP_HEAD, LOOP_HEAD_DEAD); return;
    case LOOP_TO_THE_TIMERS:
        m_rep(s, CPU_FLAG_M);
        m_stop_at(s, TIMERS, PASS_STOP_DEAD);
        return;
    case LOOP_TO_THE_REST: m_stop_at(s, REST_OF_PASS, PASS_STOP_DEAD); return;
    case LOOP_NOT_OURS: m_stop_at(s, STARTING_WITH_THE_ENEMIES, 0); return;
    default: return;
    }
}

/* ---- The entries ------------------------------------------------------------------------ */

/* The turn is the port's where the turn's place names a combatant, as every
 * battle has it (its guard); otherwise the original's, which reads Y and the
 * overflow as the caller leaves them. */
bool battle_take_turn(Snes *s, uint32_t site)
{
    if (battle_turn_names_combatant(s))
        return battle_run(s, site, GIVE_TURN, battle_give_turn);
    return battle_call(s, site, GIVE_TURN, READS_Y_AND_OVERFLOW);
}

void battle_finish_beaten_enemies(Snes *s)
{
    if (finish_beaten_enemies(state_of(s), call_on_the_machine, s))
        m_rts(s);
}

void battle_give_turn(Snes *s)
{
    if (take_turns(state_of(s), call_on_the_machine, s))
        m_rts(s);
}

void battle_main_loop_start(Snes *s) { go_on_on_the_machine(s, start_battle(state_of(s), call_on_the_machine, s)); }

void battle_main_loop(Snes *s)
{
    if (next_frame(state_of(s), call_on_the_machine, s))
        m_stop_at(s, PASS_BODY, DC_V);
}

void battle_clear_the_turn_work_entry(Snes *s)
{
    battle_clear_the_turn_work(state_of(s));
    m_rtl(s);
}

void battle_main_loop_body(Snes *s) { go_on_on_the_machine(s, run_pass(state_of(s), call_on_the_machine, s)); }

void battle_main_loop_tail(Snes *s) { go_on_on_the_machine(s, end_pass(state_of(s), call_on_the_machine, s)); }

bool battle_turn_names_combatant(Snes *s)
{
    vars_peek(true);
    bool named = turn_names_a_combatant(state_of(s));
    vars_peek(false);
    return battle_on_page(s) && named;
}

bool battle_pass_ends_plainly(Snes *s)
{
    vars_peek(true);
    bool on = debugging_switch_on(state_of(s));
    vars_peek(false);
    return battle_on_page(s) && !on;
}
