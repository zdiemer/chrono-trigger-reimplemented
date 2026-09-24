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
 * A turn (the handler of the turn timer, battle_run_queued_scripts) gives the
 * combatant whose timer ran out its turn: an enemy picks its action from its
 * script, a party member's command menu opens (or, in a demo battle or under a
 * status that decides for it, its action is picked for it). The party's
 * chosen actions are carried out one at a time in the order they were
 * chosen. After an action the enemies it hit react, each running its script's
 * reactions.
 *
 * Nearly all of this is still the original's code, which the loop calls: the
 * battle steps (the routine at $C1:0003 with the step in A, which waits for
 * frames), the scripts' conditions and actions, the rewards. What is ported is
 * the loop's own logic; the calls are bridges (battle/bridge.h). */
#include "battle/main_loop.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/timers.h"
#include "original_effects.h"
#include "rom.h"
#include "vars.h"

/* ---- The original's routines the loop calls ------------------------------------------------ */

/* A battle step: what the battle shows and waits for, by number, through the
 * routine at $C1:0003 (its table at $C1:0051). */
enum {
    STEP_SET_UP = 0,        /* set the battle up */
    STEP_OPEN_MENU = 1,     /* a party member's turn: its command menu opens */
    STEP_NEXT_FRAME = 3,    /* the frame's work, and a wait for the next */
    STEP_VICTORY = 8,       /* the party's victory */
    STEP_DEFEAT = 9,        /* the party's defeat */
};
enum { BATTLE_STEP = 0xC10003 };

/* The loop's other callees, named for what they do (as far as that is
 * worked out; docs/TODO.md). */
enum {
    CLEAR_TURN_STATE = 0xFDA982,     /* clear the turns' state */
    SET_UP_STATS = 0xC1FA8B,         /* set the combatants' stats up for the battle */
    RANDOM_NUMBER = 0xC1AF22,        /* a random number up to A, from the battle's table */
    START_ENEMY_SCRIPTS = 0xC18C09,  /* start the enemies' scripts */
    CLEAR_DAMAGE = 0xFDACFD,         /* the action's damage table cleared (battle/damage.c) */
    UPDATE_COMBATANTS = 0xC1B223,    /* every combatant's update (battle/combatants.c) */
    CLEAR_PARTY_STATUSES = 0xC1EA9D, /* the party's statuses cleared, at the battle's end */
    CLEAR_ENEMY_MARKS = 0xC1B3BB,    /* the enemies' battle marks cleared, at the end */
    PARTY_STANDS_DOWN = 0xC1B7F2,    /* each member's step 2 (its menu closed) */
    REVIVE_PARTY = 0xC1EAE8,         /* the party revived and its stat blocks cleaned, at the end */
    SAVE_PARTY_STATS = 0xC1B442,     /* the party's stats kept for after the battle */
    NOTE_BEATEN_ENEMIES = 0xFDAAB0,  /* the enemies beaten, noted for the story */
    SHARE_EXPERIENCE = 0xFDB201,     /* the experience and tech points shared out */
    GIVE_GOLD = 0xC1D0A2,            /* the gold (Y) given */
    LEVEL_UP = 0xC1F93E,             /* the members' level-ups */
    SHOW_REWARDS = 0xFDAD17,         /* the rewards' messages */
    UPDATE_TECHS = 0xC1B4E9,         /* the members' techs learnt */
    GIVE_ITEM = 0xC1D005,            /* an item (Y) given */
    MARK_ENEMIES_BEATEN = 0xC1B3D2,  /* the enemies still standing marked, at a demo's end */
    CANCEL_IF_UNABLE = 0xC1B3F9,     /* a member's action dropped if its status keeps it from acting */
    RUN_AWAY_MARKS = 0xFDAA98,       /* the party's stat blocks marked for running away */
    SHOW_MESSAGE = 0xCD0021,         /* a battle message */
    START_ACTION = 0xC1895B,         /* an action started (the running away, $79) */
    ENEMY_TURN = 0xC18CF9,           /* an enemy's turn: its script picks its action */
    SPEED_UP_TURN = 0xC1BD6F,        /* a hasted combatant's turn wait halved */
    MEMBER_TURN = 0xFDAB30,          /* a party member's turn: what its statuses decide */
    CHECK_COMBINATIONS = 0xC1B575,   /* which members can join a combination tech (battle/menu_window.c) */
    MEMBER_ACTS_FOR_ITSELF = 0xC1BCE1, /* a member whose status picks its action */
    DEMO_PARTY_TURN = 0xC1B70E,      /* a demo battle's party picks its actions */
    DEMO_NEXT_TURN = 0xC1B725,       /* a demo battle's turns reset */
    CARRY_OUT_ACTION = 0xC1B967,     /* a member's chosen action carried out */
    AFTER_ACTION = 0xC1B762,         /* the action's targets that were knocked out */
    PARTY_COUNTERS = 0xFDAC6E,       /* the party members' counterattacks */
    FIND_SCRIPT = 0xC1B488,          /* an enemy's script found, by its id (A) */
    FINISH_BEATEN = 0xC1AC5E,        /* the enemies beaten by the action finished */
    CLEAR_TARGETS = 0xC1AC46,        /* the action's targets cleared */
    CLEAR_NUMBERS = 0xFDACEE,        /* the numbers shown over the combatants cleared */
};

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
    READS_ALL_OF_A = DC_X | DC_Y | DC_N | DC_Z,
    READS_Y = DC_A | DC_X | DC_FLAGS,
    READS_Y_AND_OVERFLOW = DC_A | DC_X | DC_N | DC_Z | DC_C,
};

/* The member a step is about, in the cell the steps read it from (on the
 * battle's page, the one the objects' drawing keeps the effect's sprite
 * attributes in). */
static void set_step_member(GameState *g, uint8_t member) { set_battle_effect_sprite_attributes(g, member); }

static bool step(Snes *s, uint32_t site, uint8_t number)
{
    m_set_a8(s, number);
    return battle_call(s, site, BATTLE_STEP, READS_A_AND_OVERFLOW);
}

/* ---- The battle's start ------------------------------------------------------------------ */

/* Where the battle's random numbers are drawn from its table: kept between
 * battles, and on the battle's page while it runs. */
static void take_random_position(GameState *g)
{
    uint8_t kept = battle_random_kept(g);
    set_battle_random_position(g, kept);
    set_battle_random_at_start(g, kept);
}

/* A demo battle is set up as a normal one: its bit of the setup flags is kept
 * apart until then. */
enum { SETUP_DEMO = 0x20 };

static void set_demo_aside(GameState *g)
{
    uint8_t flags = battle_setup_flags(g);
    set_battle_demo_kept(g, flags & SETUP_DEMO);
    set_battle_setup_flags(g, flags & (uint8_t)~SETUP_DEMO);
}

/* The draw's entries are a battle slot plus one; a place drawn is emptied. */
enum { DRAWN = 0xFF };

/* The combatants to be drawn into the timers' order: the party members in
 * the fight, by the slot each character fights in, then the enemies, last to
 * first. */
static void list_combatants_for_the_order(GameState *g)
{
    for (unsigned i = 0; i < BATTLE_TIMER_ORDER_COUNT; i++) {
        set_battle_timer_order(g, i, DRAWN);
        set_battle_foes(g, i, DRAWN);
    }
    set_battle_timer_kinds_due(g, 0);
    unsigned listed = 0;
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t character = battle_combatant_fighting(g, member);
        if (character & 0x80)
            continue;
        set_battle_foes(g, listed++, (uint8_t)(battle_character_slot(g, character) + 1));
    }
    for (uint8_t slot = (uint8_t)(battle_enemy_count(g) + BATTLE_FIRST_ENEMY); slot != BATTLE_FIRST_ENEMY; slot--)
        set_battle_foes(g, listed++, slot);
    ORIGINAL_EFFECT(set_battle_low_scratch(g, 0, BATTLE_PARTY_SIZE));
    ORIGINAL_EFFECT(set_battle_low_scratch(g, 1, 0));
}

/* A random number up to `limit`, through the original's routine (its X, the
 * lowest number, 0). */
static bool draw_random(Snes *s, uint32_t site, uint8_t limit, uint8_t *drawn)
{
    s->cpu.x = 0;
    m_set_a8(s, limit);
    if (!battle_call(s, site, RANDOM_NUMBER, DC_Y | DC_FLAGS | DC_AH))
        return false;
    *drawn = (uint8_t)s->cpu.a;
    return true;
}

/* Draw the combatants into the timers' order at random, each once: a place
 * already drawn is drawn again. */
static bool draw_timer_order(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t length = battle_timer_order_length(g);
    unsigned placed = 0;
    do {
        uint8_t drawn;
        m_push16(s, (uint16_t)placed);
        if (!draw_random(s, 0xC1807B, length, &drawn))
            return false;
        m_pull16(s);
        uint8_t entry = battle_foes(g, drawn);
        if (entry == DRAWN)
            continue;
        set_battle_timer_order(g, placed++, (uint8_t)(entry - 1));
        set_battle_foes(g, drawn, DRAWN);
    } while (placed < length);
    return true;
}

/* Then the enemies' own order, drawn the same way from their ids; it ends when
 * every enemy with an id is placed. */
static bool draw_enemy_order(Snes *s)
{
    GameState *g = state_of(s);
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        set_battle_enemy_order(g, enemy, DRAWN);
        set_battle_foes(g, enemy, battle_combatant_id(g, BATTLE_FIRST_ENEMY + enemy));
    }
    unsigned placed = 0;
    for (;;) {
        uint8_t drawn;
        if (!draw_random(s, 0xC180B1, BATTLE_ENEMIES, &drawn))
            return false;
        if (battle_foes(g, drawn) == DRAWN)
            continue;
        set_battle_foes(g, drawn, DRAWN);
        set_battle_enemy_order(g, placed++, drawn);
        unsigned left = 0;
        while (battle_foes(g, left) == DRAWN && ++left < battle_enemy_count(g))
            ;
        if (left >= battle_enemy_count(g)) {
            /* the enemies' scripts are started with the count placed in Y */
            s->cpu.y = (uint16_t)placed;
            return true;
        }
    }
}

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
    if (!(battle_setup_flags(g) & SETUP_DEMO))
        return;
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++)
        set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot, battle_slot_is_party(slot) ? AT_ONCE : NEVER);
}

// $C1:8000
/* Start the battle, then on to the loop's head at $C1:8120. */
static bool start_battle(Snes *s)
{
    GameState *g = state_of(s);
    if (!battle_call_long(s, 0xC18006, CLEAR_TURN_STATE, READS_NOTHING))
        return false;
    take_random_position(g);
    if (!battle_call(s, 0xC18012, SET_UP_STATS, READS_NOTHING))
        return false;
    m_sep(s, CPU_FLAG_M);
    set_demo_aside(g);
    if (!step(s, 0xC1802D, STEP_SET_UP))
        return false;
    /* a battle without enemies (none is set up so) is the original's */
    if (battle_enemy_count(g) == 0 || battle_enemy_count(g) > BATTLE_ENEMIES) {
        m_stop_at(s, 0xC18030, DC_A | DC_X | DC_Y | DC_FLAGS);
        return false;
    }
    list_combatants_for_the_order(g);
    if (!draw_timer_order(s) || !draw_enemy_order(s))
        return false;
    finish_start(g);
    if (!battle_call(s, 0xC180E8, START_ENEMY_SCRIPTS, READS_Y))
        return false;
    put_demo_back(g);
    return true;
}

/* ---- A pass of the loop ------------------------------------------------------------------ */

// $C1:8120
/* The pass's frame: the pass counted, the damage table's hit back to the
 * first, and the frame's work and wait (battle step 3). */
static void next_frame(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_loop_count(g, (uint8_t)(battle_loop_count(g) + 1));
    set_battle_damage_hit(g, 0);
    s->cpu.a = STEP_NEXT_FRAME;
    m_set_nz8(s, STEP_NEXT_FRAME);
    m_jsr(s, 0xC18129, BATTLE_STEP);
}

/* Every enemy that must be beaten is out (or none is there). */
enum { NEEDNT_BE_BEATEN = 0x40 };

static bool enemies_beaten(const GameState *g)
{
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        if (battle_combatant_fighting(g, BATTLE_FIRST_ENEMY + enemy) != COMBATANT_NONE
            && !(battle_enemy_standing(g, enemy) & NEEDNT_BE_BEATEN))
            return false;
    return true;
}

/* The battle's end, won or lost, in the story's terms: 1 for a defeat. */
enum { WON_OR_RAN = 0, DEFEATED = 1 };

/* The party's maximum HP goes back to what it was when the battle began. */
static void restore_party_max_hp(GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_stats_max_hp(g, member, battle_party_max_hp_kept(g, member));
}

/* $C1:8443: the battle's last step, into the field again ($C1:0006): the
 * random position kept for the next battle, and each member's carried byte
 * back into its stat block. */
static bool leave_battle(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_random_kept(g, battle_random_position(g));
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_stats_carried(g, member, battle_member_carried(g, member));
    if (!battle_call_long(s, 0xC1845A, NOTE_BEATEN_ENEMIES, READS_NOTHING))
        return false;
    m_stop_at(s, 0xC10006, DC_X | DC_Y | DC_FLAGS | DC_AL);
    return true;
}

/* $C1:8423: a battle that is lost, or run from, or a demo's end: the party's
 * statuses cleared, the party revived, its stats kept. */
static bool leave_without_rewards(Snes *s)
{
    restore_party_max_hp(state_of(s));
    return battle_call(s, 0xC1843A, CLEAR_PARTY_STATUSES, READS_CARRY)
           && battle_call(s, 0xC1843D, REVIVE_PARTY, READS_CARRY)
           && battle_call(s, 0xC18440, SAVE_PARTY_STATS, READS_NOTHING) && leave_battle(s);
}

/* The six items a victory can give. */
enum { ITEMS_WON = 6 };

/* $C1:83BB: a battle that is won: unless it is a demo, the rewards -- the
 * experience shared out and the gold given (where the battle gives them),
 * the level-ups, their messages, the party revived, its stats kept, its techs
 * learnt, and the items won. */
enum { REWARDS_GIVEN = 0x40, ITEMS_GIVEN = 0x20 };

static bool leave_with_rewards(Snes *s)
{
    GameState *g = state_of(s);
    restore_party_max_hp(g);
    if (battle_setup_flags(g) & SETUP_DEMO)
        return leave_without_rewards(s);
    if (battle_rewards_held(g) == 0) {
        if (!battle_call_long(s, 0xC183E0, SHARE_EXPERIENCE, READS_NOTHING))
            return false;
        s->cpu.y = battle_gold_won(g);
        if (!battle_call(s, 0xC183E7, GIVE_GOLD, DC_A | DC_X | DC_N | DC_Z))
            return false;
        set_battle_reward_flags(g, battle_reward_flags(g) | REWARDS_GIVEN);
        s->cpu.a = battle_reward_flags(g);
        if (!battle_call(s, 0xC183F2, LEVEL_UP, READS_ALL_OF_A))
            return false;
    } else {
        s->cpu.a = battle_rewards_held(g);
    }
    /* the messages take A along as the code before leaves it */
    if (!battle_call_long(s, 0xC183F5, SHOW_REWARDS, READS_ALL_OF_A)
        || !battle_call(s, 0xC183F9, REVIVE_PARTY, READS_CARRY)
        || !battle_call(s, 0xC183FC, SAVE_PARTY_STATS, READS_NOTHING)
        || !battle_call(s, 0xC183FF, UPDATE_TECHS, READS_NOTHING))
        return false;
    for (unsigned item = 0; item < ITEMS_WON; item++) {
        ORIGINAL_EFFECT(set_battle_low_scratch(g, 0x10, (uint8_t)item));
        uint8_t won = battle_items_won(g, item);
        if (won == 0)
            continue;
        s->cpu.y = won;
        if (!battle_call(s, 0xC1840E, GIVE_ITEM, DC_A | DC_X | DC_N | DC_Z))
            return false;
        set_battle_reward_flags(g, battle_reward_flags(g) | ITEMS_GIVEN);
    }
    ORIGINAL_EFFECT(set_battle_low_scratch(g, 0x10, ITEMS_WON));
    return leave_battle(s);
}

/* The party is all out: its defeat, over eight frames and the defeat's step,
 * and away without rewards. */
enum { DEFEAT_FRAMES = 8 };

static bool lose(Snes *s)
{
    GameState *g = state_of(s);
    if (!battle_call(s, 0xC18137, CLEAR_PARTY_STATUSES, READS_CARRY)
        || !battle_call(s, 0xC1813A, CLEAR_ENEMY_MARKS, READS_NOTHING))
        return false;
    set_battle_result(g, DEFEATED);
    if (!battle_call(s, 0xC18143, PARTY_STANDS_DOWN, READS_OVERFLOW))
        return false;
    set_battle_defeat_showing(g, true);
    for (unsigned frame = 0; frame < DEFEAT_FRAMES; frame++) {
        ORIGINAL_EFFECT(set_battle_low_scratch(g, 0, (uint8_t)frame));
        if (!step(s, 0xC18151, STEP_NEXT_FRAME))
            return false;
    }
    ORIGINAL_EFFECT(set_battle_low_scratch(g, 0, DEFEAT_FRAMES));
    set_battle_defeat_showing(g, false);
    return step(s, 0xC18161, STEP_DEFEAT) && leave_without_rewards(s);
}

/* Every enemy is beaten: the victory, and away with the rewards. */
enum { VICTORY_MARK = 0xFF };

static bool win(Snes *s)
{
    GameState *g = state_of(s);
    if (!battle_call(s, 0xC18176, CLEAR_ENEMY_MARKS, READS_NOTHING))
        return false;
    set_battle_result(g, WON_OR_RAN);
    set_battle_won(g, VICTORY_MARK);
    return battle_call(s, 0xC18183, CLEAR_PARTY_STATUSES, READS_CARRY) && step(s, 0xC18188, STEP_VICTORY)
           && leave_with_rewards(s);
}

/* The party runs away: the running away's marks and message, the party
 * standing down, the running away's action, and away without rewards. */
enum { RAN_AWAY_MESSAGE = 8, RUN_AWAY_ACTION = 0x79 };

static bool run_away(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_result(g, WON_OR_RAN);
    if (!battle_call_long(s, 0xC181FD, RUN_AWAY_MARKS, READS_NOTHING))
        return false;
    m_set_a8(s, RAN_AWAY_MESSAGE);
    if (!battle_call_long(s, 0xC18203, SHOW_MESSAGE, READS_A)
        || !battle_call(s, 0xC18207, CLEAR_PARTY_STATUSES, READS_CARRY)
        || !battle_call(s, 0xC1820A, PARTY_STANDS_DOWN, READS_OVERFLOW))
        return false;
    m_set_a8(s, RUN_AWAY_ACTION);
    return battle_call(s, 0xC1820F, START_ACTION, READS_A_AND_OVERFLOW) && battle_call(s, 0xC18212, REVIVE_PARTY, READS_CARRY)
           && battle_call(s, 0xC18215, SAVE_PARTY_STATS, READS_NOTHING)
           && battle_call(s, 0xC18218, UPDATE_TECHS, READS_NOTHING)
           && battle_call(s, 0xC1821B, CLEAR_ENEMY_MARKS, READS_NOTHING) && leave_without_rewards(s);
}

/* The low HP and statuses of every combatant, through their entries
 * (battle/combatants.c), as the original calls them with the combatant in X
 * and Y. */
static bool follow_combatants(Snes *s)
{
    static const uint16_t low_hp_calls[BATTLE_PARTY_SIZE] = { 0x8198, 0x81A4, 0x81B0 };
    static const uint16_t status_calls[BATTLE_PARTY_SIZE] = { 0x819B, 0x81A7, 0x81B3 };
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        s->cpu.x = (uint16_t)(slot * BATTLE_STAT_BLOCK_BYTES);
        s->cpu.y = (uint16_t)slot;
        if (battle_slot_is_party(slot) && !battle_call(s, 0xC10000u | low_hp_calls[slot], 0xC1B093, DC_A | DC_Y | DC_FLAGS))
            return false;
        uint32_t status_call = battle_slot_is_party(slot) ? 0xC10000u | status_calls[slot] : 0xC181C2;
        /* the statuses read the carry, which the step to an enemy's stat
         * block leaves clear */
        if (!battle_slot_is_party(slot))
            m_set_flag(s, CPU_FLAG_C, false);
        if (!battle_call(s, status_call, 0xC1B0B6, DC_A | DC_N | DC_V | DC_Z | DC_C))
            return false;
    }
    return true;
}

/* A party member whose status keeps it from acting has its action dropped,
 * for each member in a slot (battle_update_all_combatants's order). The
 * member's number goes in its cell. */
static bool cancel_unable_members(Snes *s, uint32_t site)
{
    GameState *g = state_of(s);
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_member_updating(g, (uint8_t)member);
        if (battle_combatant_id(g, member) == COMBATANT_NONE)
            continue;
        s->cpu.x = (uint16_t)member;
        s->cpu.a = (uint16_t)member;
        if (!battle_call(s, site, CANCEL_IF_UNABLE, DC_Y | DC_FLAGS | DC_AL))
            return false;
    }
    set_battle_member_updating(g, BATTLE_PARTY_SIZE);
    return true;
}

/* Where a pass goes on after its checks: the timers (in 16-bit A, as the
 * original's test of the running action leaves it), or the rest of the pass
 * ($C1:8345) after the turn's handler or with the timers held. */
enum { TIMERS = 0xC18231, REST_OF_PASS = 0xC18345, PASS_STOP_DEAD = DC_A | DC_X | DC_Y | DC_FLAGS };

/* The loop's head, which starts from A and the flags cleared. */
enum { LOOP_HEAD = 0xC18120, LOOP_HEAD_DEAD = DC_A | DC_X | DC_Y | DC_FLAGS };

// $C1:812C
/* A pass after its frame: the damage table cleared, the combatants updated;
 * the battle's ends; the combatants' low HP and statuses; then the timers, or
 * while they are held the members' checks, or while an action runs over
 * passes the turn's handler at once. */
static bool run_pass(Snes *s)
{
    GameState *g = state_of(s);
    if (!battle_call_long(s, 0xC1812C, CLEAR_DAMAGE, READS_NOTHING)
        || !battle_call(s, 0xC18130, UPDATE_COMBATANTS, READS_NOTHING))
        return false;
    if (battle_party_defeated(g))
        return lose(s);
    if (enemies_beaten(g))
        return win(s);
    if (!follow_combatants(s))
        return false;
    if (battle_timers_held(g)) {
        if (!cancel_unable_members(s, 0xC181DB))
            return false;
        m_stop_at(s, REST_OF_PASS, PASS_STOP_DEAD);
        return true;
    }
    if (!(battle_setup_flags(g) & SETUP_DEMO) && battle_run_counter(g) != 0)
        return run_away(s);
    if (battle_action_running(g) == 0) {
        m_rep(s, CPU_FLAG_M);
        m_stop_at(s, TIMERS, PASS_STOP_DEAD);
        return true;
    }
    if (!battle_call(s, 0xC1822B, 0xC18461, READS_Y_AND_OVERFLOW))
        return false;
    m_stop_at(s, REST_OF_PASS, PASS_STOP_DEAD);
    return true;
}

// $C1:8345
/* The end of a pass: back to the loop's head, or a demo battle's end. (A
 * debugging path the ROM can turn on, off in the released game, is the
 * original's: the entry's guard.) */
static bool end_pass(Snes *s)
{
    GameState *g = state_of(s);
    if (!(battle_setup_flags(g) & SETUP_DEMO)) {
        m_stop_at(s, LOOP_HEAD, LOOP_HEAD_DEAD);
        return true;
    }
    return battle_call(s, 0xC183B8, MARK_ENEMIES_BEATEN, READS_NOTHING) && leave_with_rewards(s);
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
    return battle_enemy_turn_held(g, enemy) == 0;
}

/* The turn wait an enemy that acted starts again from. */
static void restart_turn_wait(GameState *g, unsigned slot)
{
    set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot, battle_turn_wait(g, slot));
}

/* An enemy's turn: its script picks its action, its turn wait starts again
 * (halved while it is hasted) and its turn timer runs. */
static bool enemy_turn(Snes *s, uint8_t slot)
{
    GameState *g = state_of(s);
    set_battle_enemy_acting(g, (uint8_t)(slot - BATTLE_FIRST_ENEMY));
    s->cpu.x = slot;
    /* taking the slot's number less 3 leaves the carry set, which the enemy's
     * turn carries through to the speed-up after it */
    m_set_flag(s, CPU_FLAG_C, true);
    if (!battle_call(s, 0xC184D6, ENEMY_TURN, READS_NOTHING))
        return false;
    restart_turn_wait(g, slot);
    s->cpu.x = slot;
    if (!battle_call(s, 0xC184E1, SPEED_UP_TURN, DC_A | DC_Y | DC_N | DC_Z))
        return false;
    battle_restart_timer(g, BATTLE_TIMER_TURN, slot);
    return true;
}

/* A party member's action record: nothing yet, its menu open, then with bit 7
 * its action chosen. */
enum { NO_ACTION = 0, MENU_OPEN = 1, ACTION_CHOSEN = 0x80 };

static uint8_t member_action(const GameState *g, unsigned member) { return battle_member_actions_state(g, member); }
static void set_member_action(GameState *g, unsigned member, uint8_t v) { set_battle_member_actions_state(g, member, v); }

/* A member with its action chosen is noted ready (its record's byte kept). */
static void note_members_ready(GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (member_action(g, member) & ACTION_CHOSEN)
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

static bool member_turn(Snes *s, uint8_t member, MemberTurn *turn)
{
    GameState *g = state_of(s);
    s->cpu.y = member;
    s->cpu.x = (uint16_t)(member * BATTLE_STAT_BLOCK_BYTES);
    s->cpu.a = 0;
    if (!battle_call_long(s, 0xC18526, MEMBER_TURN, DC_N | DC_Z))
        return false;
    set_battle_low_scratch(g, 0x14, member);
    set_battle_low_scratch(g, 0x15, 0);
    if (battle_member_acts_for_itself(g)) {
        *turn = ACTED_FOR_ITSELF;
        set_battle_acting_for_itself(g, member);
        return battle_call(s, 0xC18538, CHECK_COMBINATIONS, READS_NOTHING)
               && battle_call(s, 0xC1853B, MEMBER_ACTS_FOR_ITSELF, READS_ALL_OF_A);
    }
    *turn = TO_THE_QUEUE;
    if (member_action(g, member) != NO_ACTION)
        return true;
    if (battle_setup_flags(g) & SETUP_DEMO) {
        *turn = DEMO_PICKED;
        return battle_call(s, 0xC1855C, DEMO_PARTY_TURN, READS_NOTHING);
    }
    set_member_action(g, member, MENU_OPEN);
    set_step_member(g, member);
    return step(s, 0xC1856D, STEP_OPEN_MENU);
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
static const uint16_t carry_out_calls[BATTLE_PARTY_SIZE] = { 0x85CE, 0x85E7, 0x8600 };

static bool carry_out(Snes *s, uint8_t member, bool *carried)
{
    GameState *g = state_of(s);
    *carried = ((member_action(g, member) ^ battle_member_ready(g, member)) & ACTION_CHOSEN) != 0;
    if (!*carried)
        return true;
    set_battle_actor(g, member);
    if (!battle_call(s, 0xC10000u | carry_out_calls[member], CARRY_OUT_ACTION, READS_NOTHING))
        return false;
    set_battle_member_ready(g, member, NO_ACTION);
    set_member_action(g, member, NO_ACTION);
    return true;
}

/* Every member in a slot: its combination techs checked, and its action
 * dropped if its status keeps it from acting ($C1:883D). It is a subroutine
 * of the turn's, called from $C1:860A, whose return is on the stack under its
 * calls. */
enum { CHECKS_CALL_LAST_BYTE = 0x860C };

static bool check_members(Snes *s)
{
    GameState *g = state_of(s);
    m_push16(s, CHECKS_CALL_LAST_BYTE);
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_member_updating(g, (uint8_t)member);
        if (battle_combatant_id(g, member) == COMBATANT_NONE)
            continue;
        set_battle_low_scratch(g, 0x14, (uint8_t)member);
        set_battle_low_scratch(g, 0x15, 0);
        s->cpu.x = (uint16_t)member;
        m_set_a8(s, (uint8_t)member);
        if (!battle_call(s, 0xC1884B, CHECK_COMBINATIONS, READS_NOTHING))
            return false;
        s->cpu.x = (uint16_t)member;
        s->cpu.a = (uint16_t)member;
        if (!battle_call(s, 0xC18853, CANCEL_IF_UNABLE, DC_Y | DC_FLAGS | DC_AL))
            return false;
    }
    set_battle_member_updating(g, BATTLE_PARTY_SIZE);
    m_pull16(s);
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
    set_battle_low_scratch(g, 0x14, member);
    set_battle_low_scratch(g, 0x15, 0);
}

/* The queue: the party's chosen actions carried out one at a time. */
static bool run_queue(Snes *s, bool note_ready, TurnGoesOn *next)
{
    GameState *g = state_of(s);
    if (note_ready)
        note_members_ready(g);
    if (battle_setup_flags(g) & SETUP_DEMO) {
        s->cpu.a = battle_setup_flags(g);
        if (!battle_call(s, 0xC1858F, DEMO_NEXT_TURN, READS_ALL_OF_A))
            return false;
    }
    uint8_t member = take_from_queue(g);
    *next = CHECK_MEMBERS;
    if (member == NOBODY || member >= BATTLE_PARTY_SIZE)
        return true;
    bool carried;
    if (!carry_out(s, member, &carried))
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
static bool give_turn(Snes *s, TurnGoesOn *next)
{
    GameState *g = state_of(s);
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
            return enemy_turn(s, slot);
        if (!((battle_member_ready(g, 0) | battle_member_ready(g, 1) | battle_member_ready(g, 2)) & ACTION_CHOSEN)) {
            *next = TO_THE_END;
            return true;
        }
        next_spare_member(g);
        return run_queue(s, true, next);
    }
    if (!battle_timer_run_out(g, BATTLE_TIMER_TURN, slot))
        return true;
    MemberTurn turn;
    if (!member_turn(s, slot, &turn))
        return false;
    if (turn == ACTED_FOR_ITSELF)
        return true;
    return run_queue(s, turn != DEMO_PICKED, next);
}

/* ---- After an action: the enemies' reactions ---------------------------------------------- */

/* An enemy's script, in bank CC: blocks of conditions and actions. A block's
 * conditions are four bytes each (the first the condition's kind), up to an
 * end marker, then its actions, up to another; the script ends with its own
 * marker. The reactions are the blocks after the script's turn blocks. */
enum { BLOCK_END = 0xFE, SCRIPT_END = 0xFF, CONDITION_BYTES = 4, REVIVES = 0x20 };

static uint8_t script_byte(const GameState *g, uint16_t at) { return rom_read8(g, 0xCC0000u | at); }

static uint16_t past_block_end(const GameState *g, uint16_t at)
{
    do
        at++;
    while (script_byte(g, at) != BLOCK_END);
    return at;
}

/* The statuses that keep an enemy from reacting. */
enum { STATUSES_THAT_STOP_REACTIONS = 0x8F };

/* The conditions' handlers' table in the ROM (bank C1). */
static uint16_t condition_handler(const GameState *g, uint8_t kind)
{
    return rom_read16(g, 0xC1B80Du + (uint16_t)(kind * 2));
}

/* What a block's conditions left: 0 all held (the enemy acts), 2 ... the
 * script's own outcome (battle_script_outcome). */
enum { CONDITIONS_HELD = 0, OUTCOME_TWO = 2 };

/* Test a condition through its handler; the kind goes in its cell. */
static bool test_condition(Snes *s, uint32_t site, uint16_t at)
{
    GameState *g = state_of(s);
    uint8_t kind = script_byte(g, at);
    set_battle_condition_kind(g, kind);
    /* the handlers take the kind doubled in A as well, with the flags of
     * doubling it */
    uint8_t doubled = (uint8_t)(kind << 1);
    s->cpu.x = doubled;
    s->cpu.a = doubled;
    m_set_nz8(s, doubled);
    m_set_flag(s, CPU_FLAG_C, kind & 0x80);
    return battle_call(s, site, 0xC10000u | condition_handler(g, kind), 0);
}

/* One enemy's reactions to the action: the blocks of its reactions, each
 * tested and, where its conditions hold, acted on, until one whose outcome
 * stops the script, or the script's end. */
static bool react(Snes *s, unsigned entry, uint8_t target)
{
    GameState *g = state_of(s);
    unsigned enemy = (uint8_t)(target - BATTLE_FIRST_ENEMY);
    bool gone = !combatant_in_fight(g, target);
    set_battle_enemy_turn(g, (uint8_t)enemy);
    set_battle_enemy_acting(g, (uint8_t)enemy);
    m_set_a8(s, gone ? battle_combatant_id(g, target) : battle_combatant_fighting(g, target));
    if (!battle_call(s, gone ? 0xC186A8 : 0xC186D8, FIND_SCRIPT, READS_A))
        return false;
    uint16_t at = battle_script_place(g);
    set_battle_script_restart_place(g, at);
    set_battle_block_start(g, at);
    if (gone) {
        if (script_byte(g, at) != REVIVES)
            return true;
        set_battle_combatant_fighting(g, target, battle_combatant_id(g, target));
    }
    set_battle_animation_kept(g, battle_enemy_animation(g, enemy));
    if (battle_stats_status(g, target) & STATUSES_THAT_STOP_REACTIONS)
        return true;
    for (;;) {
        set_battle_second_condition(g, false);
        set_battle_script_outcome(g, CONDITIONS_HELD);
        uint8_t foe = battle_foes(g, enemy);
        set_battle_foe_kept(g, foe);
        set_battle_foes(g, enemy, battle_last_actor(g));
        uint16_t block = battle_block_start(g);
        set_battle_script_place(g, block);
        bool second = script_byte(g, (uint16_t)(block + CONDITION_BYTES)) != BLOCK_END;
        set_battle_second_condition(g, second);
        set_battle_reaction_place_kept(g, battle_reaction_places(g, entry));
        if (!test_condition(s, 0xC1874E, block))
            return false;
        if (battle_second_condition(g)) {
            uint16_t next = (uint16_t)(battle_block_start(g) + CONDITION_BYTES);
            set_battle_script_place(g, next);
            if (!test_condition(s, 0xC18769, next))
                return false;
        }
        if (battle_script_outcome(g) == CONDITIONS_HELD) {
            set_battle_foes(g, enemy, battle_last_actor(g));
            uint8_t acting = battle_enemy_acting(g);
            m_push8(s, acting);
            if (!battle_call(s, 0xC18780, ENEMY_TURN, READS_NOTHING))
                return false;
            set_battle_enemy_acting(g, m_pull8(s));
            if (!battle_call(s, 0xC18787, UPDATE_COMBATANTS, READS_NOTHING))
                return false;
            set_battle_reaction_places(g, entry, battle_reaction_place_kept(g));
            uint8_t outcome = battle_script_outcome(g);
            set_battle_enemy_turn_held(g, entry, outcome);
            if (outcome == OUTCOME_TWO)
                set_battle_enemy_script_started(g, entry, false);
        }
        uint8_t outcome = battle_script_outcome(g);
        if (outcome == CONDITIONS_HELD)
            break;
        if (outcome != OUTCOME_TWO) {
            set_battle_second_condition(g, false);
            set_battle_script_place(g, past_block_end(g, battle_script_place(g)));
        }
        uint16_t next = (uint16_t)(past_block_end(g, battle_script_place(g)) + 1);
        set_battle_script_place(g, next);
        if (script_byte(g, next) == SCRIPT_END)
            break;
        set_battle_block_start(g, next);
        set_battle_enemy_script_blocks(g, entry, (uint8_t)(battle_enemy_script_blocks(g, entry) + 1));
        set_battle_foes(g, enemy, battle_foe_kept(g));
    }
    set_battle_enemy_animation(g, battle_enemy_acting(g), battle_animation_kept(g));
    return true;
}

enum { FORGET_LEAVES_Y = 4 };

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
    return (status & STATUS_RESTARTS_SCRIPT) != 0;
}

/* Each enemy the action hit, in the order of the action's targets, reacts;
 * between them the actor's chosen action is forgotten again, and the party's
 * combination techs are looked at after each. */
static bool enemies_react(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_enemy_turn(g, 0);
    set_battle_reaction_entry(g, 0);
    set_battle_second_condition(g, false);
    set_battle_script_outcome(g, CONDITIONS_HELD);
    for (unsigned entry = 0; entry < BATTLE_ENEMIES; entry++) {
        set_battle_reaction_entry(g, (uint8_t)entry);
        if (entry > 0) {
            forget_actors_action(g);
            /* the original forgets it with a loop whose count it leaves in
             * Y, which the conditions' handlers read */
            ORIGINAL_EFFECT(s->cpu.y = FORGET_LEAVES_Y);
        }
        uint8_t target = battle_reaction_targets(g, entry);
        if (target >= BATTLE_FIRST_ENEMY && target != NOBODY && !react(s, entry, target))
            return false;
        if (!battle_call(s, 0xC18809, CHECK_COMBINATIONS, READS_NOTHING))
            return false;
    }
    set_battle_reaction_entry(g, BATTLE_ENEMIES);
    return true;
}

/* What an action draws after it (none in a demo): the party's counters when
 * it hit no enemy; else the reactions of the enemies it hit, unless a status
 * keeps the enemy that acted from them. */
static bool follow_up(Snes *s, uint8_t actor)
{
    GameState *g = state_of(s);
    if (battle_setup_flags(g) & SETUP_DEMO)
        return true;
    if (!battle_action_hit_enemy(g))
        return battle_call_long(s, 0xC18650, PARTY_COUNTERS, READS_NOTHING);
    return actor_silenced(g, actor) || enemies_react(s);
}

// $C1:8626 (to the end)
/* After the turn: every combatant updated, the action's targets and actor
 * kept, those knocked out seen to; then, where the action hit an enemy (and
 * isn't a demo's, nor an enemy's that a status silences), each enemy it hit
 * reacts, or else the party members counter; then the beaten enemies
 * finished, the enemies' scripts started again (unless the party is
 * beaten), and the action's targets and numbers cleared. */
static bool after_action(Snes *s, bool skip_to_end)
{
    GameState *g = state_of(s);
    if (!skip_to_end) {
        if (!battle_call(s, 0xC18626, UPDATE_COMBATANTS, READS_NOTHING))
            return false;
        for (unsigned i = 0; i < BATTLE_COMBATANTS; i++)
            set_battle_reaction_targets(g, i, battle_action_targets(g, i));
        set_battle_last_actor(g, battle_actor(g));
        if (!battle_call(s, 0xC1863D, AFTER_ACTION, READS_NOTHING))
            return false;
        uint8_t actor = battle_last_actor(g);
        if (!follow_up(s, actor))
            return false;
    }
    if (!battle_call(s, 0xC1881C, FINISH_BEATEN, READS_NOTHING))
        return false;
    set_battle_action_hit_enemy(g, false);
    if (!battle_party_defeated(g) && !battle_call(s, 0xC18826, START_ENEMY_SCRIPTS, READS_Y))
        return false;
    set_battle_actor(g, battle_last_actor(g));
    forget_actors_action(g);
    if (!battle_call(s, 0xC18832, CLEAR_TARGETS, READS_NOTHING) || !battle_call_long(s, 0xC18835, CLEAR_NUMBERS, READS_NOTHING))
        return false;
    set_battle_action_hit_enemy(g, false);
    return true;
}

/* The turn's handler: the turn itself on the passes that look at timers;
 * then, while no party member has an action waiting, the turn no longer due;
 * then what follows every action. */
static bool take_turns(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_action_hit_enemy(g, false);
    set_battle_action_hit_party(g, false);
    TurnGoesOn next = CHECK_MEMBERS;
    if (battle_loop_count(g) & 0x01) {
        if (!give_turn(s, &next))
            return false;
        if (next == CHECK_MEMBERS && !check_members(s))
            return false;
        if (next != TO_THE_END && !any_member_ready(g))
            set_battle_timer_kinds_due(g, battle_timer_kinds_due(g) & (uint16_t)~1u);
    }
    return after_action(s, next == TO_THE_END);
}

/* ---- The entries ------------------------------------------------------------------------ */

void battle_give_turn(Snes *s)
{
    if (take_turns(s))
        m_rts(s);
}

void battle_main_loop_start(Snes *s)
{
    if (start_battle(s))
        m_stop_at(s, LOOP_HEAD, LOOP_HEAD_DEAD);
}

void battle_main_loop(Snes *s) { next_frame(s); }

void battle_main_loop_body(Snes *s) { run_pass(s); }

void battle_main_loop_tail(Snes *s) { end_pass(s); }

/* The pass's end has a debugging path the ROM turns on with its last byte, off
 * in the released game, which reads the controller's register: the guard
 * leaves it to the original. */
static bool peek_rom_byte(Snes *s, uint32_t at)
{
    vars_peek(true);
    uint8_t v = rom_read8(state_of(s), at);
    vars_peek(false);
    return v;
}

/* The turn is given to the combatant the timers' order names at the turn's
 * place: the guard takes the states where that is a combatant, as every
 * battle has it. */
bool battle_turn_names_combatant(Snes *s)
{
    vars_peek(true);
    bool named = battle_names_combatant(combatant_whose_turn(state_of(s)));
    vars_peek(false);
    return battle_on_page(s) && named;
}

bool battle_pass_ends_plainly(Snes *s) { return battle_on_page(s) && peek_rom_byte(s, 0xC1FFFF) == 0; }
