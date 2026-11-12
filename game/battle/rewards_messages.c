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
 * The messages wait for frames; each is made where the original makes it. The telling keeps its place in a record of
 * its own (Telling). The original keeps the same place in its cells (whose
 * turn it is, how many levels each member has left to tell, which single
 * tech is next, how many double techs are told), so the place is left there
 * before each message and once the telling is over. */
#include "battle/rewards.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/message.h"
#include "battle/program.h"
#include "charge.h"
#include "battle/rewards_work.h"
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

/* Each message is one of the battle's own ($CD:0021), shown and waited for
 * until it has been read (a routine of its own, battle/message.c). False
 * where it went no further. */
enum { SHOW_A_BATTLE_MESSAGE = 0xCD0021 };

static bool tell(GameState *g, Platform *pf, RewardMessage message)
{
    bool done = false;
    TIMED_AS(g, SHOW_A_BATTLE_MESSAGE, done = battle_show_a_battle_message(g, pf, (uint8_t)message));
    return done;
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
static bool tell_the_battles_rewards(GameState *g, Platform *pf)
{
    if (rewarded(g, REWARD_EXPERIENCE) && battle_experience_won(g) != 0) {
        tell_the_figure(g, battle_experience_won(g));
        if (!tell(g, pf, TELL_EXPERIENCE))
            return false;
    }
    if (rewarded(g, REWARD_TECH_POINTS) && battle_tech_points_won_low(g) != 0) {
        tell_the_figure(g, battle_tech_points_won_low(g) | (uint32_t)battle_tech_points_won_high(g) << 16);
        if (!tell(g, pf, TELL_TECH_POINTS))
            return false;
    }
    if (rewarded(g, REWARD_GOLD) && battle_gold_won(g) != 0) {
        tell_the_figure(g, battle_gold_won(g));
        if (!tell(g, pf, TELL_GOLD))
            return false;
    }
    if (!rewarded(g, REWARD_ITEMS))
        return true;
    for (unsigned item = 0; item < BATTLE_ITEMS_WON_COUNT; item++) {
        uint8_t won = battle_items_won(g, item);
        if (won == 0)
            continue;
        set_battle_message_item(g, won);
        if (!tell(g, pf, TELL_ITEM))
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
static bool tell_the_levels(GameState *g, Platform *pf, Telling *t, uint8_t character)
{
    uint8_t *left = &t->levels_left[t->member];
    do {
        set_menu_text_arguments(g, 0, character);
        if (!tell(g, pf, TELL_LEVEL))
            return false;
    } while (--*left != 0);
    return true;
}

/* Every single tech the member learned, from its eight places in the list
 * until one names none. */
static bool tell_the_single_techs(GameState *g, Platform *pf, Telling *t, uint8_t character)
{
    t->at_the_single_techs = true;
    t->single_tech = (uint8_t)(t->member * PLACES_A_MEMBER);
    do {
        uint8_t tech = battle_rewards_techs(g, t->single_tech);
        if (tech == NO_TECH)
            return true;
        set_menu_text_arguments(g, 0, character);
        set_menu_text_arguments(g, 1, tech);
        if (!tell(g, pf, TELL_SINGLE_TECH))
            return false;
    } while (++t->single_tech % PLACES_A_MEMBER != 0);
    return true;
}

/* The double techs the party learned, from where the telling got to: the
 * first member noted with them tells them all, a later one none. */
static bool tell_the_double_techs(GameState *g, Platform *pf, Telling *t)
{
    for (;; t->double_techs_told++) {
        uint8_t tech = battle_rewards_double_techs(g, t->double_techs_told);
        if (tech == NO_TECH)
            return true;
        set_menu_text_arguments(g, 0, tech);
        if (!tell(g, pf, TELL_DOUBLE_TECH))
            return false;
    }
}

static bool tell_a_members_rewards(GameState *g, Platform *pf, Telling *t)
{
    uint8_t member = t->member;
    uint8_t character = battle_combatant_fighting(g, member);
    if (character == OUT_OF_THE_FIGHT)
        return true;
    uint8_t techs = battle_rewards_techs_noted(g, member);
    if ((battle_rewards_levels_noted(g, member) & NOTED_LEVEL_GAINED) && !tell_the_levels(g, pf, t, character))
        return false;
    if ((techs & NOTED_SINGLE_TECHS) && !tell_the_single_techs(g, pf, t, character))
        return false;
    if ((techs & NOTED_DOUBLE_TECHS) && !tell_the_double_techs(g, pf, t))
        return false;
    if (techs & NOTED_TRIPLE_TECH) {
        set_menu_text_arguments(g, 0, battle_rewards_triple_tech(g));
        if (!tell(g, pf, TELL_TRIPLE_TECH))
            return false;
    }
    return true;
}

// $FD:AD17
static bool tell_the_rewards(GameState *g, Platform *pf)
{
    Telling t = { .double_techs_told = 0 };
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        t.levels_left[member] = battle_rewards_levels_gained(g, member);
    if (!tell_the_battles_rewards(g, pf))
        return false;
    t.at_the_members = true;
    for (t.member = 0; t.member < BATTLE_PARTY_SIZE; t.member++)
        if (!tell_a_members_rewards(g, pf, &t))
            return false;
    return true;
}

/* ---- The rewards told ------------------------------------------------------------------------------ */

/* What the rewards take: a member noted with double techs finds the list's
 * end within it (the original reads on past it into the cells after). */
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

bool battle_show_the_rewards(GameState *g, Platform *pf)
{
    if (!rewards_fit(g))
        return battle_contract_broken(g, 0xFDAD17, "a double techs' list with no end");
    return tell_the_rewards(g, pf);
}
