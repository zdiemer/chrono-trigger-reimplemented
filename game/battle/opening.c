/* Battle: the party at the battle's opening.
 *
 * The battle's setup (its step 0) ends with the party turning to face the
 * enemies and striking its opening pose: each member faces the enemy nearest
 * it, and the members taking part take the opening's animation, those sitting
 * the fight out none. The poses themselves, a few frames each member steps
 * through over the next frames, are the same the victory and the defeat end
 * with (battle/party_poses.c), which the setup goes on into. */
#include "battle/animation.h"
#include "battle/animation_data.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/positions.h"
#include "charge.h"
#include "original_effects.h"
#include "vars.h"

/* The member's nearest enemy, found as the battlefield's searches find it,
 * asked the way the original asks it: the question and the searcher in their
 * cells. */
static uint8_t nearest_enemy(GameState *g, uint8_t member)
{
    set_battle_target_searcher(g, member);
    TIMED_AS(g, 0xC12986,
             battle_search(g, SEARCH_NEAREST_OTHER_ENEMY, member, battle_search_other(g), battle_search_third(g)));
    return battle_target_found(g);
}

static BattlePoint standing_at(const GameState *g, uint8_t slot)
{
    return (BattlePoint){ battle_object_x(g, battle_combatant_object(slot)), battle_object_y(g, battle_combatant_object(slot)) };
}

/* The original counts the members down in a cell, which it leaves past the
 * first. */
enum { LEFT_MEMBER = 0x94 - 0x80 };
static void leave_member_count(GameState *g)
{
    set_battle_work(g, LEFT_MEMBER, 0xFF);
    set_battle_work(g, LEFT_MEMBER + 1, 0);
}

// $C1:350F
/* Each member, the last first, turns to face the enemy nearest it. */
static void face_the_enemies(GameState *g)
{
    set_battle_target_search(g, SEARCH_NEAREST_OTHER_ENEMY);
    for (uint8_t member = BATTLE_PARTY_SIZE; member-- > 0;) {
        BattlePoint from = standing_at(g, member);
        BattlePoint to = standing_at(g, nearest_enemy(g, member));
        battle_keep_direction_points(g, from, to);
        uint8_t direction = 0;
        TIMED_AS(g, 0xC10222, direction = battle_direction(g, from, to));
        set_battle_object_facing(g, member, battle_facing_of_direction(g, direction));
    }
    ORIGINAL_EFFECT(leave_member_count(g));
}

// $C1:34DB
/* The party faces the enemies, and takes the opening's poses. */
static void open_the_battle(GameState *g)
{
    face_the_enemies(g);
    battle_give_the_party_poses(g, PARTY_POSE_OPENING);
}

/* ---- The entry --------------------------------------------------------------------------- */

/* From the setup's wait for the window's slide ($C1:0042, which jumps here),
 * and on to the poses (battle/party_poses.c), which return to the setup. */
void battle_party_faces_the_enemies(Snes *s)
{
    open_the_battle(state_of(s));
    if (battle_strike_the_poses_as_their_own(s))
        m_rts(s);
}

/* On the battle's page with A's high byte clear, as the setup comes; the
 * search's other combatants, and the target it last found, which it keeps
 * where it finds nobody, are combatants. */
bool battle_opening_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool named = battle_names_combatant(battle_search_other(g)) && battle_names_combatant(battle_search_third(g))
                 && battle_names_combatant(battle_target_found(g));
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && named;
}
