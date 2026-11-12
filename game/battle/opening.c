/* Battle: the battle's opening, its first step.
 *
 * The setup sets the battle's state up from the field's party and the
 * enemies (battle/setup.c), draws the menu's window and works the objects'
 * bounds out, and readies the graphics, which the vblank handler then loads a
 * step a frame while the setup waits. Then each combatant's graphics are built
 * for the frame it starts on, the frame's work begins, and the window opens,
 * sliding up over some frames while the setup waits again.
 *
 * It ends with the party turning to face the enemies and striking its opening
 * pose: each member faces the enemy nearest it, and the members taking part
 * take the opening's animation, those sitting the fight out none. The poses
 * themselves, a few frames each member steps through over the next frames,
 * are the same the victory and the defeat end with (battle/party_poses.c),
 * which the setup goes on into. */
#include "battle/animation.h"
#include "battle/animation_data.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/positions.h"
#include "battle/program.h"
#include "battle/steps.h"
#include "battle/window.h"
#include "platform/timing.h"
#include "battle/screen.h"
#include "battle/setup.h"
#include "charge.h"
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
}

// $C1:34DB
/* The party faces the enemies, and takes the opening's poses. */
static void open_the_battle(GameState *g)
{
    face_the_enemies(g);
    battle_give_the_party_poses(g, PARTY_POSE_OPENING);
}

/* ---- The setup -------------------------------------------------------------------------- */

/* The members' animations start a frame apart, the first the next frame. */
static void stagger_the_party_s_animations(GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_party_frame_delay(g, member, (uint8_t)(member + 1));
}

// $C1:34A7
/* Each combatant there's graphics built for the frame it starts on, the
 * upload sending them as the cells it reads say. */
static void build_the_combatants_graphics(GameState *g)
{
    for (uint8_t slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        if (!battle_slot_present(g, slot))
            continue;
        uint8_t frame = setup_start_frame(g, slot);
        set_battle_graphics_object(g, (uint16_t)((battle_graphics_object(g) & 0xFF00) | slot));
        set_battle_graphics_frame(g, frame);
        TIMED_AS(g, 0xC11C55, battle_build_frame_graphics(g, slot, frame));
    }
    stagger_the_party_s_animations(g);
}

/* The battle's frame work (its vblank's) starts with the first mode, the
 * fight's ($CD:0003). */
enum { FIGHTING = 1 };

void battle_start_the_frame_work(GameState *g) { set_battle_mode(g, FIGHTING); }

/* The setup's calls: the battle's state (battle/setup.c), the window drawn
 * (battle/window.c), the objects' bounds (battle/objects.c), the objects'
 * graphics (battle/setup.c), after which it waits for the vblank handler to
 * load them; then the window opened (battle/closing.c), after which it waits
 * for the window's slide; then the poses (battle/party_poses.c). The program
 * makes them as C, and on the machine they are the setup's entries, each
 * going on from the wait it stops at. */
typedef enum { SET_UP_STATE, SET_UP_WINDOW, SET_UP_BOUNDS, SET_UP_GRAPHICS, SET_UP_CALLS } SetUpCall;

// $C1:0023
/* The setup, up to the wait for the graphics to load. */
static bool set_up(bool (*call)(void *ctx, SetUpCall call), void *ctx)
{
    for (SetUpCall c = SET_UP_STATE; c < SET_UP_CALLS; c++)
        if (!call(ctx, c))
            return false;
    return true;
}

// $C1:0031
/* The setup once the graphics have loaded, up to the wait for the window's
 * slide. */
static bool set_up_after_the_graphics(GameState *g, bool (*open_the_window)(void *ctx), void *ctx)
{
    build_the_combatants_graphics(g);
    TIMED_AS(g, 0xCD0003, battle_start_the_frame_work(g));
    return open_the_window(ctx);
}

/* The graphics' load, a step a frame in the vblank handler, ends at its
 * fourth. */
enum { GRAPHICS_LOADED = 4 };

/* The guards' state halves: the graphics built on combatants the builder
 * knows; the opening's search's other combatants, and the target it last
 * found, which it keeps where it finds nobody, combatants. */
static bool combatants_graphics_known(GameState *g)
{
    bool known = true;
    for (uint8_t slot = 0; slot < BATTLE_COMBATANTS && known; slot++)
        known = !battle_slot_present(g, slot) || battle_object_graphics_known(g, slot);
    return known;
}

static bool opening_fits(const GameState *g)
{
    return battle_names_combatant(battle_search_other(g)) && battle_names_combatant(battle_search_third(g))
           && battle_names_combatant(battle_target_found(g));
}

/* ---- As C ------------------------------------------------------------------------------------ */

typedef struct {
    GameState *game;
    Platform *pf;
} BattleSetUpContext;

enum {
    SET_UP_STATE_ENTRY = 0xCCE2E4,
    DRAW_WINDOW = 0xC10299,
    ALL_BOUNDS = 0xC1283D,
    SET_UP_GRAPHICS_ENTRY = 0xCCED87,
    AFTER_THE_GRAPHICS = 0xC10031,
    OPEN_THE_WINDOW = 0xCD000C,
    FACES_THE_ENEMIES = 0xC10042,
};

static bool set_up_a_part(void *ctx, SetUpCall call)
{
    BattleSetUpContext *c = ctx;
    GameState *g = c->game;
    switch (call) {
    case SET_UP_STATE:
        if (!battle_setup_inputs_fit(g))
            return battle_contract_broken(g, SET_UP_STATE_ENTRY, "a battle's setup from a party or enemies the port doesn't take");
        TIMED_AS(g, SET_UP_STATE_ENTRY, battle_setup_state(g));
        return true;
    case SET_UP_WINDOW:
        if (!battle_window_fits(g))
            return battle_contract_broken(g, DRAW_WINDOW, "a menu state the window's port doesn't take");
        TIMED_AS(g, DRAW_WINDOW, battle_redraw_window(g));
        return true;
    case SET_UP_BOUNDS: TIMED_AS(g, ALL_BOUNDS, battle_update_all_bounds(g)); return true;
    case SET_UP_GRAPHICS:
        if (!setup_objects_parts_fit(g))
            return battle_contract_broken(g, SET_UP_GRAPHICS_ENTRY, "a combatant's parts the port doesn't fit");
        TIMED_AS(g, SET_UP_GRAPHICS_ENTRY, battle_setup_graphics(g));
        while (battle_graphics_load_step(g) != GRAPHICS_LOADED)
            tm_wait_for_handlers(c->pf, PF_LOOP(0xCCF05B, 0xCCF062));
        return true;
    default: return false;
    }
}

static bool slide_the_window_open(void *ctx)
{
    BattleSetUpContext *c = ctx;
    bool done = false;
    TIMED_AS(c->game, OPEN_THE_WINDOW, done = battle_window_opened(c->game, c->pf));
    return done;
}

bool battle_set_up_runs(GameState *g, Platform *pf)
{
    BattleSetUpContext c = { g, pf };
    if (!set_up(set_up_a_part, &c))
        return false;
    if (!combatants_graphics_known(g))
        return battle_contract_broken(g, AFTER_THE_GRAPHICS, "a combatant's graphics the builder doesn't know");
    bool done = false;
    TIMED_AS(g, AFTER_THE_GRAPHICS, done = set_up_after_the_graphics(g, slide_the_window_open, &c));
    if (!done)
        return false;
    while (battle_window_slide_frames(g) != 0)
        tm_wait_for_handlers(pf, PF_LOOP(0xC1003C, 0xC10042));
    if (!opening_fits(g))
        return battle_contract_broken(g, FACES_THE_ENEMIES, "an opening search the port doesn't make");
    TIMED_AS(g, FACES_THE_ENEMIES, open_the_battle(g));
    return battle_party_strikes_its_poses(g, pf);
}
