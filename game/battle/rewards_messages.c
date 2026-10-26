/* Battle: the rewards' messages, as a won battle ends (battle/main_loop.c).
 *
 * Once the rewards are given (battle/rewards.h), the battle tells the player
 * what they were, a message at a time, each held until it has been read
 * (battle/message.c): the experience shared out, the tech points, the gold,
 * each item given; then for each member still in the fight, every level it
 * gained, every single tech it learned, the double techs the party learned
 * and its triple tech. The rewards' notes say what there is to tell; a
 * message's figures and names go where the menus' text reads them
 * (menu_text_arguments), and an item where the battle's messages look for one.
 *
 * The messages wait for frames, so the telling is a bridge round each: the
 * message's port, run as the original's call makes it. The telling keeps its
 * place in a record of its own (Telling). The original keeps the same place
 * in its cells (whose turn it is, how many levels each member has left to
 * tell, which single tech is next, how many double techs are told), which
 * its code reads again if a message hands back to it, so the place is left
 * there before each message and once the telling is over. */
#include "battle/rewards.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/message.h"
#include "battle/program.h"
#include "charge.h"
#include "battle/rewards_work.h"
#include "original_effects.h"
#include "vars.h"

/* The battle's own messages that tell the rewards, by their number among
 * them (battle/message.c's strings). */
typedef enum {
    TELL_EXPERIENCE,
    TELL_TECH_POINTS,
    TELL_GOLD,
    TELL_ITEM,
    TELL_LEVEL,
    TELL_SINGLE_TECH,
    TELL_DOUBLE_TECH,
    TELL_TRIPLE_TECH,
} RewardMessage;

/* Where the telling has got to. */
typedef struct {
    bool at_the_members;                     /* past the battle's own rewards */
    uint8_t member;                          /* then whose rewards are told; BATTLE_PARTY_SIZE once all are */
    uint8_t levels_left[BATTLE_PARTY_SIZE];  /* each member's levels still to tell */
    bool at_the_single_techs;                /* a member's single techs reached */
    uint8_t single_tech;                     /* then the place of the next in the list */
    uint8_t double_techs_told;
} Telling;

/* For the original's code and shadow mode: the place, in the cells the
 * original keeps it in (those of a part not reached yet keep what they had). */
static void leave_the_place(GameState *g, const Telling *t)
{
    set_battle_rewards_double_techs_listed(g, t->double_techs_told);
    if (!t->at_the_members)
        return;
    set_battle_rewards_member(g, t->member);
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_rewards_levels_gained(g, member, t->levels_left[member]);
    if (t->at_the_single_techs)
        set_battle_rewards_tech_at(g, t->single_tech);
}

/* Each message is shown and waited for until it has been read (a routine of
 * its own, battle/message.c), the item's with the item's place in the list,
 * which the original keeps on the stack over it. False where it went no
 * further. */
typedef bool (*TellCalls)(void *ctx, RewardMessage message, unsigned item);

static bool tell(GameState *g, RewardMessage message, const Telling *t, TellCalls call, void *ctx)
{
    ORIGINAL_EFFECT(leave_the_place(g, t));
    return call(ctx, message, 0);
}

static bool tell_an_item(GameState *g, unsigned item, const Telling *t, TellCalls call, void *ctx)
{
    ORIGINAL_EFFECT(leave_the_place(g, t));
    return call(ctx, TELL_ITEM, item);
}

/* A figure the message shows, three bytes long. */
static void tell_the_figure(GameState *g, uint32_t figure)
{
    for (unsigned i = 0; i < 3; i++)
        set_menu_text_arguments(g, i, (uint8_t)(figure >> (8 * i)));
}

/* ---- The battle's rewards ------------------------------------------------------------------------- */

static bool rewarded(const GameState *g, uint8_t reward) { return (battle_reward_flags(g) & reward) != 0; }

/* The experience, the tech points and the gold, each where there was some
 * shared out or given. The tech points are a three-byte sum, of which the
 * original asks only the low word whether there are any: a sum that is a
 * whole multiple of 65,536 goes untold. Then each item given. */
static bool tell_the_battles_rewards(GameState *g, const Telling *t, TellCalls call, void *ctx)
{
    if (rewarded(g, REWARD_EXPERIENCE) && battle_experience_won(g) != 0) {
        tell_the_figure(g, battle_experience_won(g));
        if (!tell(g, TELL_EXPERIENCE, t, call, ctx))
            return false;
    }
    if (rewarded(g, REWARD_TECH_POINTS) && battle_tech_points_won_low(g) != 0) {
        tell_the_figure(g, battle_tech_points_won_low(g) | (uint32_t)battle_tech_points_won_high(g) << 16);
        if (!tell(g, TELL_TECH_POINTS, t, call, ctx))
            return false;
    }
    if (rewarded(g, REWARD_GOLD) && battle_gold_won(g) != 0) {
        tell_the_figure(g, battle_gold_won(g));
        if (!tell(g, TELL_GOLD, t, call, ctx))
            return false;
    }
    if (!rewarded(g, REWARD_ITEMS))
        return true;
    for (unsigned item = 0; item < BATTLE_ITEMS_WON_COUNT; item++) {
        uint8_t won = battle_items_won(g, item);
        if (won == 0)
            continue;
        set_battle_message_item(g, won);
        if (!tell_an_item(g, item, t, call, ctx))
            return false;
    }
    return true;
}

/* ---- A member's rewards --------------------------------------------------------------------------- */

/* A member no longer in the fight is told nothing. */
enum { OUT_OF_THE_FIGHT = 0xFF };

/* Every level the member gained, one message each, counting the levels left
 * down to none. A member noted with none left is told 256 times: the original
 * counts down before it asks. */
static bool tell_the_levels(GameState *g, Telling *t, uint8_t character, TellCalls call, void *ctx)
{
    uint8_t *left = &t->levels_left[t->member];
    do {
        set_menu_text_arguments(g, 0, character);
        if (!tell(g, TELL_LEVEL, t, call, ctx))
            return false;
    } while (--*left != 0);
    return true;
}

/* Every single tech the member learned, from its eight places in the list
 * until one names none. */
static bool tell_the_single_techs(GameState *g, Telling *t, uint8_t character, TellCalls call, void *ctx)
{
    t->at_the_single_techs = true;
    t->single_tech = (uint8_t)(t->member * PLACES_A_MEMBER);
    do {
        uint8_t tech = battle_rewards_techs(g, t->single_tech);
        if (tech == NO_TECH)
            return true;
        set_menu_text_arguments(g, 0, character);
        set_menu_text_arguments(g, 1, tech);
        if (!tell(g, TELL_SINGLE_TECH, t, call, ctx))
            return false;
    } while (++t->single_tech % PLACES_A_MEMBER != 0);
    return true;
}

/* The double techs the party learned, from where the telling got to: the
 * first member noted with them tells them all, a later one none. */
static bool tell_the_double_techs(GameState *g, Telling *t, TellCalls call, void *ctx)
{
    for (;; t->double_techs_told++) {
        uint8_t tech = battle_rewards_double_techs(g, t->double_techs_told);
        if (tech == NO_TECH)
            return true;
        set_menu_text_arguments(g, 0, tech);
        if (!tell(g, TELL_DOUBLE_TECH, t, call, ctx))
            return false;
    }
}

static bool tell_a_members_rewards(GameState *g, Telling *t, TellCalls call, void *ctx)
{
    uint8_t member = t->member;
    uint8_t character = battle_combatant_fighting(g, member);
    if (character == OUT_OF_THE_FIGHT)
        return true;
    uint8_t techs = battle_rewards_techs_noted(g, member);
    if ((battle_rewards_levels_noted(g, member) & NOTED_LEVEL_GAINED) && !tell_the_levels(g, t, character, call, ctx))
        return false;
    if ((techs & NOTED_SINGLE_TECHS) && !tell_the_single_techs(g, t, character, call, ctx))
        return false;
    if ((techs & NOTED_DOUBLE_TECHS) && !tell_the_double_techs(g, t, call, ctx))
        return false;
    if (techs & NOTED_TRIPLE_TECH) {
        set_menu_text_arguments(g, 0, battle_rewards_triple_tech(g));
        if (!tell(g, TELL_TRIPLE_TECH, t, call, ctx))
            return false;
    }
    return true;
}

// $FD:AD17
static bool tell_the_rewards(GameState *g, TellCalls call, void *ctx)
{
    Telling t = { .double_techs_told = 0 };
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        t.levels_left[member] = battle_rewards_levels_gained(g, member);
    if (!tell_the_battles_rewards(g, &t, call, ctx))
        return false;
    t.at_the_members = true;
    for (t.member = 0; t.member < BATTLE_PARTY_SIZE; t.member++)
        if (!tell_a_members_rewards(g, &t, call, ctx))
            return false;
    ORIGINAL_EFFECT(leave_the_place(g, &t));
    return true;
}

/* ---- The entry ------------------------------------------------------------------------------------- */

/* The members' count's last compare leaves the carry set, which the main
 * loop's code after the call reads. */
/* On the machine each from the original's call of it, the message in A,
 * its high byte clear, as every call of the original has it (the guard). The
 * message reads nothing of the registers but which it is. */
enum { SHOW_A_BATTLE_MESSAGE = 0xCD0021, THE_MESSAGE_READS_A = DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

static const uint32_t tell_sites[] = { 0xFDAD2E, 0xFDAD4B, 0xFDAD63, 0xFDAD7C, 0xFDADAE, 0xFDADF4, 0xFDAE20, 0xFDAE3F };

static bool tell_on_the_machine(void *ctx, RewardMessage message, unsigned item)
{
    Snes *s = ctx;
    s->cpu.a = (uint16_t)message;
    if (message == TELL_ITEM)
        return battle_run_long_dc_under(s, 0xFDAD7C, SHOW_A_BATTLE_MESSAGE, THE_MESSAGE_READS_A,
                                        battle_show_a_battle_message_entry,
                                        battle_keeping_word((uint16_t)item, BATTLE_CARRY_AS_IT_IS));
    return battle_run_long_dc(s, tell_sites[message], SHOW_A_BATTLE_MESSAGE, THE_MESSAGE_READS_A,
                              battle_show_a_battle_message_entry);
}

void battle_show_the_rewards_entry(Snes *s)
{
    if (!tell_the_rewards(state_of(s), tell_on_the_machine, s))
        return;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rtl(s);
}

/* The guard: on the battle's page with A's high byte clear, as the main loop
 * calls it, and a member noted with double techs finds the list's end within
 * it (the original reads on past it into the cells after). */
static bool rewards_fit(const GameState *g)
{
    bool double_techs = false, ended = false;
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        double_techs |= battle_combatant_fighting(g, member) != OUT_OF_THE_FIGHT
                        && (battle_rewards_techs_noted(g, member) & NOTED_DOUBLE_TECHS);
    for (unsigned i = 0; i < BATTLE_REWARDS_DOUBLE_TECHS_COUNT; i++)
        ended |= battle_rewards_double_techs(g, i) == NO_TECH;
    return !double_techs || ended;
}

bool battle_rewards_can_be_shown(Snes *s)
{
    if (!battle_on_page(s) || (s->cpu.a & 0xFF00) != 0)
        return false;
    vars_peek(true);
    bool fits = rewards_fit(state_of(s));
    vars_peek(false);
    return fits;
}

/* ---- As C ------------------------------------------------------------------------------------ */

typedef struct {
    GameState *game;
    Platform *pf;
} TellAsC;

static bool tell_a_reward(void *ctx, RewardMessage message, unsigned item)
{
    (void)item;
    TellAsC *c = ctx;
    bool done = false;
    TIMED_AS(c->game, SHOW_A_BATTLE_MESSAGE, done = battle_show_a_battle_message(c->game, c->pf, (uint8_t)message));
    return done;
}

bool battle_show_the_rewards(GameState *g, Platform *pf)
{
    if (!rewards_fit(g))
        return battle_needs_the_machine(g, 0xFDAD17, "a double techs' list with no end");
    TellAsC c = { g, pf };
    return tell_the_rewards(g, tell_a_reward, &c);
}
