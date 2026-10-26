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
static bool combatants_graphics_known(const GameState *g)
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
} SetUpAsC;

enum { SET_UP_ENTRY = 0xC10023, SET_UP_STATE_ENTRY = 0xCCE2E4, DRAW_WINDOW = 0xC10299, ALL_BOUNDS = 0xC1283D,
       SET_UP_GRAPHICS_ENTRY = 0xCCED87, AFTER_THE_GRAPHICS = 0xC10031, OPEN_THE_WINDOW = 0xCD000C,
       FACES_THE_ENEMIES = 0xC10042 };

static bool set_up_as_c(void *ctx, SetUpCall call)
{
    SetUpAsC *c = ctx;
    GameState *g = c->game;
    switch (call) {
    case SET_UP_STATE:
        if (!battle_setup_inputs_fit(g))
            return battle_needs_the_machine(g, SET_UP_STATE_ENTRY, "a battle's setup from a party or enemies the port doesn't take");
        TIMED_AS(g, SET_UP_STATE_ENTRY, battle_setup_state(g));
        return true;
    case SET_UP_WINDOW:
        if (!battle_window_fits(g))
            return battle_needs_the_machine(g, DRAW_WINDOW, "a menu state the window's port doesn't take");
        TIMED_AS(g, DRAW_WINDOW, battle_redraw_window(g));
        return true;
    case SET_UP_BOUNDS: TIMED_AS(g, ALL_BOUNDS, battle_update_all_bounds(g)); return true;
    case SET_UP_GRAPHICS:
        if (!setup_objects_parts_fit(g))
            return battle_needs_the_machine(g, SET_UP_GRAPHICS_ENTRY, "a combatant's parts the port doesn't fit");
        TIMED_AS(g, SET_UP_GRAPHICS_ENTRY, battle_setup_graphics(g));
        while (battle_graphics_load_step(g) != GRAPHICS_LOADED)
            tm_wait_for_handlers(c->pf, PF_LOOP(0xCCF05B, 0xCCF062));
        return true;
    default: return false;
    }
}

static bool open_the_window_as_c(void *ctx)
{
    SetUpAsC *c = ctx;
    bool done = false;
    TIMED_AS(c->game, OPEN_THE_WINDOW, done = battle_window_opened(c->game, c->pf));
    return done;
}

bool battle_set_up_runs(GameState *g, Platform *pf)
{
    SetUpAsC c = { g, pf };
    if (!set_up(set_up_as_c, &c))
        return false;
    if (!combatants_graphics_known(g))
        return battle_needs_the_machine(g, AFTER_THE_GRAPHICS, "a combatant's graphics the builder doesn't know");
    bool done = false;
    TIMED_AS(g, AFTER_THE_GRAPHICS, done = set_up_after_the_graphics(g, open_the_window_as_c, &c));
    if (!done)
        return false;
    while (battle_window_slide_frames(g) != 0)
        tm_wait_for_handlers(pf, PF_LOOP(0xC1003C, 0xC10042));
    if (!opening_fits(g))
        return battle_needs_the_machine(g, FACES_THE_ENEMIES, "an opening search the port doesn't make");
    TIMED_AS(g, FACES_THE_ENEMIES, open_the_battle(g));
    return battle_party_strikes_its_poses(g, pf);
}

/* ---- On the machine ------------------------------------------------------------------------ */

/* Where the setup waits: for the graphics to have loaded, which the setup's
 * graphics end by jumping to (battle_wait_graphics_loaded_loop, which
 * returns to the setup), and for the window's slide (battle/wait.c), which
 * reads none of the registers but A's high byte, as the window's opening
 * leaves it. */
enum { SLIDE_WAIT = 0xC1003C, THE_WAIT_READS_A_S_HIGH_BYTE = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };
/* The window's opening sets the rest of the registers itself. */
enum { THE_OPENING_READS_A_S_HIGH_BYTE = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

static bool set_up_on_the_machine(void *ctx, SetUpCall call)
{
    Snes *s = ctx;
    switch (call) {
    case SET_UP_STATE: return battle_run_long(s, 0xC10023, SET_UP_STATE_ENTRY, battle_setup_state_entry);
    case SET_UP_WINDOW: return battle_run(s, 0xC10027, DRAW_WINDOW, battle_draw_window);
    case SET_UP_BOUNDS: return battle_run(s, 0xC1002A, ALL_BOUNDS, battle_update_all_object_bounds);
    case SET_UP_GRAPHICS: return battle_run_long(s, 0xC1002D, SET_UP_GRAPHICS_ENTRY, battle_setup_graphics_entry);
    default: return false;
    }
}

static bool open_the_window_on_the_machine(void *ctx)
{
    return battle_run_long_dc(ctx, 0xC10038, OPEN_THE_WINDOW, THE_OPENING_READS_A_S_HIGH_BYTE, battle_open_the_window);
}

void battle_set_up(Snes *s) { (void)set_up(set_up_on_the_machine, s); }

void battle_set_up_after_the_graphics(Snes *s)
{
    if (set_up_after_the_graphics(state_of(s), open_the_window_on_the_machine, s))
        m_stop_at(s, SLIDE_WAIT, THE_WAIT_READS_A_S_HIGH_BYTE);
}

/* Every combatant there's graphics known to the builder (battle/sprites.c). */
bool battle_combatants_graphics_known(Snes *s)
{
    if (!battle_on_page_a_high_clear(s))
        return false;
    vars_peek(true);
    bool known = combatants_graphics_known(state_of(s));
    vars_peek(false);
    return known;
}

// $CD:0003
void battle_start_the_frame_work_entry(Snes *s)
{
    battle_start_the_frame_work(state_of(s));
    m_set_a8(s, FIGHTING);
    m_set_nz8(s, FIGHTING);
    m_rtl(s);
}

// $CD:0000, $CD:0039
/* The battle's screen set up ($D1:F1B0) for the battle's start from the
 * field, which calls it through this bank's table of ways in. */
void battle_set_the_screen_up_for_the_field(Snes *s)
{
    if (battle_run_long(s, 0xCD0039, 0xD1F1B0, battle_set_the_screen_up))
        m_rtl(s);
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
    bool named = opening_fits(state_of(s));
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && named;
}
