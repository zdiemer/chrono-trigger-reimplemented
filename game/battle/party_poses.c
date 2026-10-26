/* Battle: the party's poses as a battle opens, and as it ends.
 *
 * As a battle opens, and as it ends in a victory or a defeat, the party
 * strikes a pose: for 64 frames each member steps through a short animation
 * of its own -- the opening's, the victory's or the defeat's -- from its
 * frame and delay lists, in its facing's part, a step every other frame, its
 * first frame a step later than the member before it. A member sitting the
 * fight out takes none. At the opening and the victory a member knocked out
 * lies where it fell; at the defeat all three pose. On the frames between
 * its steps the battle's queued uploads send what the step built.
 *
 * The opening comes to the poses from the party turning to face the enemies
 * (opening.c). The victory (the battle's step 8) first sees the fight's
 * leftovers off -- the markers, the enemies' drift, every member's look --
 * takes each member's menu away a frame apart, and draws the menu window as
 * nobody's; a battle can be set up to have the party strike no pose as it
 * wins. The defeat (the battle's step 9) holds the markers back and waits a
 * frame.
 *
 * The poses wait for frames, so they are a bridge: the waits, the frame
 * work's tail and the menu window are the original's calls as it makes them
 * (battle_wait_frame, battle_run), the rest C. The poses' tables are kept in
 * cells the script channels and the numbers use at other times
 * (battle_party_pose_*). */
#include "battle/animation.h"
#include "battle/animation_data.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/choosing.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/frame.h"
#include "battle/program.h"
#include "battle/steps.h"
#include "battle/wait.h"
#include "battle/window.h"
#include "charge.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The poses ---------------------------------------------------------------------------- */

/* A member's pose is eight frames, taken from its frame and delay lists
 * (battle_party_list_byte); its times are halved, the poses stepping every
 * other frame. The poses run for 64 frames. */
enum { POSE_FRAMES = 8, POSES_FRAMES = 64 };

/* A member's place in the poses before its first frame. */
enum { BEFORE_THE_FIRST_FRAME = 0xFF };

/* While the poses step, the frame's phase is the party's (the uploads and
 * the graphics builders go by it); between, none. */
enum { NO_PHASE = 0, PARTY_PHASE = 1 };

static unsigned pose_at(unsigned member, unsigned frame) { return member * POSE_FRAMES + frame; }

/* What loading the poses leaves in the work cells: the member it counted to,
 * and, where any member is there, the copy's count run out and the last
 * one's row of the tables. */
enum { MEMBER_CELL = 0x80, COPY_CELL = 0x82, ROW_CELL = 0x84 };

static void leave_the_loading(GameState *g)
{
    battle_leave_work16(g, MEMBER_CELL, BATTLE_PARTY_SIZE);
    for (unsigned member = BATTLE_PARTY_SIZE; member-- > 0;)
        if (battle_slot_present(g, member)) {
            battle_leave_work8(g, COPY_CELL, 0);
            battle_leave_work16(g, ROW_CELL, (uint16_t)pose_at(member, 0));
            return;
        }
}

// $C1:35DD
/* Each member's pose, from the animation the battle gave it
 * (battle_party_scene_animation) in its lists; the members' first frames a
 * step apart. The last member's lists stay in the animation scheduler's
 * first entry, as a member's frame step keeps them. */
static void load_the_poses(GameState *g)
{
    for (unsigned at = 0; at < BATTLE_PARTY_POSE_DELAYS_COUNT; at++)
        set_battle_party_pose_delays(g, at, 0);
    bool any = false;
    uint16_t frames_at = 0, times_at = 0;
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        if (!battle_slot_present(g, member))
            continue;
        uint16_t facing_part = battle_facing_part(g, member);
        uint16_t animation = battle_party_scene_animation(g, member);
        times_at = (uint16_t)(animation + battle_object_delay_lists(g, member));
        frames_at = (uint16_t)(animation + battle_object_frame_lists(g, member) + facing_part);
        any = true;
        for (unsigned frame = 0; frame < POSE_FRAMES; frame++) {
            uint8_t time = battle_party_list_byte(g, (uint16_t)(times_at + frame));
            set_battle_party_pose_delays(g, pose_at(member, frame), (uint8_t)(time / 2));
            set_battle_party_pose_frames(g, pose_at(member, frame), battle_party_list_byte(g, (uint16_t)(frames_at + frame)));
        }
    }
    if (any)
        battle_keep_member_lists(g, frames_at, times_at);
    ORIGINAL_EFFECT(leave_the_loading(g));
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_party_pose_countdown(g, member, (uint8_t)(member + 1));
        set_battle_party_pose_frame(g, member, BEFORE_THE_FIRST_FRAME);
    }
    set_battle_queued_uploads_on(g, 0);
    set_battle_frame_phase(g, NO_PHASE);
}

/* Whether the member takes its pose: one sitting the fight out doesn't, nor,
 * unless all three pose, one knocked out (its stat block noted for it as
 * the menu notes one). */
static bool takes_its_pose(GameState *g, uint8_t member)
{
    if (!battle_party_scene_poses_all(g)) {
        TIMED_AS(g, 0xCFFD9E, battle_note_menu_stat_block(g, member));
        if (combatant_knocked_out(g, member))
            return false;
    }
    return battle_slot_present(g, member) != 0;
}

/* The member's frame's time run out, it shows its next, and holds its last;
 * a time of none holds a frame too. Its graphics are built at once. */
static void step_a_member(GameState *g, uint8_t member)
{
    uint8_t left = (uint8_t)(battle_party_pose_countdown(g, member) - 1);
    set_battle_party_pose_countdown(g, member, left);
    if (left)
        return;
    uint8_t frame = (uint8_t)(battle_party_pose_frame(g, member) + 1);
    set_battle_party_pose_frame(g, member, frame);
    if (frame == POSE_FRAMES)
        return;
    /* the original finds the frame's place in the tables through a work cell */
    ORIGINAL_EFFECT(battle_leave_work8(g, MEMBER_CELL, frame));
    uint8_t time = battle_party_pose_delays(g, pose_at(member, frame));
    set_battle_party_pose_countdown(g, member, time);
    if (time == 0)
        return;
    uint8_t graphics = battle_party_pose_frames(g, pose_at(member, frame));
    battle_show_object_frame(g, member, graphics);
    if (battle_object_builds_graphics(g, member))
        TIMED_AS(g, 0xC11C4A, battle_build_frame_graphics(g, member, graphics));
}

// $C1:369A
/* A frame of the poses: every other frame, with the uploads held, each
 * member taking its pose steps on; on the frames between, the uploads send
 * what the step built. True where the poses stepped. */
static bool step_the_poses(GameState *g)
{
    if (!battle_queued_uploads_on(g)) {
        set_battle_queued_uploads_on(g, 1);
        return false;
    }
    set_battle_queued_uploads_on(g, 0);
    set_battle_frame_phase(g, PARTY_PHASE);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (takes_its_pose(g, member))
            step_a_member(g, member);
    ORIGINAL_EFFECT(set_battle_party_pose_member(g, BATTLE_PARTY_SIZE));
    set_battle_frame_phase(g, NO_PHASE);
    set_battle_queued_uploads_on(g, 0);
    return true;
}

/* A member's frame steps on from its eight only where its time runs out
 * there, which, a member holding its last frame for 255 steps, never comes
 * in the poses' 32. */
static bool poses_keep_to_their_frames(const GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t frame = battle_party_pose_frame(g, member);
        if (frame != BEFORE_THE_FIRST_FRAME && frame > POSE_FRAMES)
            return false;
        if (frame == POSE_FRAMES && battle_party_pose_countdown(g, member) == 1)
            return false;
    }
    return true;
}

/* ---- The poses' frames -------------------------------------------------------------------- */

/* The poses' calls, each where the original makes it: the battle's wait for
 * the next frame ($C1:007E) and the frame work's tail ($CD:0009,
 * battle/frame.c), for each of the poses' frames, the victory's and the
 * defeat's; and the menu window drawn as nobody's (battle/menu_window.c). */
typedef enum {
    POSES_FRAME, POSES_TAIL,
    VICTORY_TAIL, VICTORY_FIRST_MENU_GONE, VICTORY_SECOND_MENU_GONE, VICTORY_THIRD_MENU_GONE, VICTORY_WINDOW,
    DEFEAT_TAIL, DEFEAT_FRAME,
    POSES_STRUCK,   /* the poses themselves, a routine of their own */
} PoseCall;
typedef bool (*PoseCalls)(void *ctx, PoseCall call);

/* The original counts the poses' frames down in a cell over the script
 * channels' (battle_party_poses_frames_left), which shadow mode compares at
 * each frame's wait; no channel runs during the poses, and nothing else
 * reads it. */
static void leave_the_frames_left(GameState *g, uint8_t left) { set_battle_party_poses_frames_left(g, left); }

// $C1:35DD
/* The poses as a whole: loaded, then 64 frames of the battle's, each the
 * frame waited for, the frame work's tail, and the poses' step. */
static bool strike_the_poses(GameState *g, PoseCalls call, void *ctx)
{
    load_the_poses(g);
    for (uint8_t left = POSES_FRAMES; left > 0; left--) {
        ORIGINAL_EFFECT(leave_the_frames_left(g, left));
        if (!call(ctx, POSES_FRAME) || !call(ctx, POSES_TAIL))
            return false;
        TIMED_AS(g, 0xC1369A, (void)step_the_poses(g));
    }
    ORIGINAL_EFFECT(leave_the_frames_left(g, 0));
    set_battle_queued_uploads_on(g, 0);
    set_battle_frame_phase(g, NO_PHASE);
    return true;
}

/* ---- The battle's close ------------------------------------------------------------------- */

/* ---- Which poses ---------------------------------------------------------------------------- */

/* Where each pose's animation is in a member's lists, and none for a member
 * sitting the fight out. */
enum { OPENING_ANIMATION = 0x30, VICTORY_ANIMATION = 0x28, DEFEAT_ANIMATION = 0x20, NO_ANIMATION = 0 };

static uint16_t animation_of(PartyPose pose)
{
    return pose == PARTY_POSE_OPENING ? OPENING_ANIMATION : pose == PARTY_POSE_VICTORY ? VICTORY_ANIMATION : DEFEAT_ANIMATION;
}

void battle_give_the_party_poses(GameState *g, PartyPose pose)
{
    bool all_pose = pose == PARTY_POSE_DEFEAT;
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        bool sits_out = !all_pose && battle_member_sitting_out(g, member);
        set_battle_party_scene_animation(g, member, sits_out ? NO_ANIMATION : animation_of(pose));
    }
    set_battle_party_scene_opening(g, pose == PARTY_POSE_OPENING);
    set_battle_party_scene_poses_all(g, all_pose);
}

static void hold_the_markers(GameState *g) { set_battle_markers_held(g, (uint8_t)(battle_markers_held(g) + 1)); }

/* What taking the looks off leaves in the work cells: the slot it counted
 * down from, gone below the first. */
enum { LOOK_SLOT_CELL = 0x94 };

// $C1:356D
/* The victory: the fight's leftovers seen off -- the markers held back, the
 * enemies' drift stopped, each member's look taken off -- each member's menu
 * taken away a frame apart, and the menu window drawn as nobody's; then the
 * party's victory poses, unless the battle was set up without them. */
static bool see_the_fight_off(GameState *g, PoseCalls call, void *ctx)
{
    if (!call(ctx, VICTORY_TAIL))
        return false;
    hold_the_markers(g);
    for (unsigned enemy = 0; enemy < BATTLE_ENEMY_DRIFTING_COUNT; enemy++)
        set_battle_enemy_drifting(g, enemy, 0);
    for (unsigned slot = BATTLE_PARTY_SIZE; slot-- > 0;)
        battle_take_off_the_look(g, slot);
    ORIGINAL_EFFECT(battle_leave_work16(g, LOOK_SLOT_CELL, 0x00FF));
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_step_member(g, member);
        TIMED_AS(g, 0xC11BAA, battle_call_off_the_menu(g, member));
        if (!call(ctx, (PoseCall)(VICTORY_FIRST_MENU_GONE + member)))
            return false;
    }
    set_battle_menu_member(g, MENU_NONE);
    return call(ctx, VICTORY_WINDOW);
}

static bool win(GameState *g, PoseCalls call, void *ctx)
{
    if (!see_the_fight_off(g, call, ctx))
        return false;
    if (battle_setup_flags(g) & BATTLE_SETUP_NO_VICTORY_POSE)
        return true;
    battle_give_the_party_poses(g, PARTY_POSE_VICTORY);
    return call(ctx, POSES_STRUCK);
}

// $C1:354D
/* The defeat: the markers held back, a frame waited, and the party's defeat
 * poses, all three of them. */
static bool lose(GameState *g, PoseCalls call, void *ctx)
{
    if (!call(ctx, DEFEAT_TAIL))
        return false;
    hold_the_markers(g);
    if (!call(ctx, DEFEAT_FRAME))
        return false;
    battle_give_the_party_poses(g, PARTY_POSE_DEFEAT);
    return call(ctx, POSES_STRUCK);
}

/* The guards' state halves: the graphics' object a battle slot, as the
 * animations keep its high byte clear, and the members' graphics ones the
 * builders know; the victory's looks as the looks' guard keeps them. */
static bool members_graphics_known(const GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (battle_slot_present(g, member) && !battle_object_graphics_known(g, member))
            return false;
    return true;
}

static bool poses_fit(const GameState *g) { return (battle_graphics_object(g) >> 8) == 0 && members_graphics_known(g); }

/* ---- As C ------------------------------------------------------------------------------------ */

typedef struct {
    GameState *game;
    Platform *pf;
} PosesAsC;

enum { FRAME_WAIT = 0xC1007E, FRAME_TAIL = 0xCD0009, MENU_WINDOW = 0xC10C2D, STRIKE_THE_POSES = 0xC135DD };

static bool pose_call_as_c(void *ctx, PoseCall call)
{
    PosesAsC *c = ctx;
    GameState *g = c->game;
    bool done = false;
    switch (call) {
    case POSES_TAIL:
    case VICTORY_TAIL:
    case DEFEAT_TAIL: TIMED_AS(g, FRAME_TAIL, done = battle_frame_wait_finished(g, c->pf)); return done;
    case VICTORY_WINDOW:
        TIMED_AS(g, MENU_WINDOW, done = battle_menu_window_drawn(g, c->pf));
        return done;
    case POSES_STRUCK: TIMED_AS(g, STRIKE_THE_POSES, done = strike_the_poses(g, pose_call_as_c, ctx)); return done;
    default: TIMED_AS(g, FRAME_WAIT, done = battle_wait_for_frame(g, c->pf)); return done;
    }
}

bool battle_party_strikes_its_poses(GameState *g, Platform *pf)
{
    if (!poses_fit(g))
        return battle_needs_the_machine(g, 0xC135DD, "poses on graphics the builders don't know");
    PosesAsC c = { g, pf };
    return pose_call_as_c(&c, POSES_STRUCK);
}

bool battle_wins_the_fight(GameState *g, Platform *pf)
{
    if (!poses_fit(g) || !battle_pose_looks_fit(g))
        return battle_needs_the_machine(g, 0xC1356D, "a victory with looks or graphics the port doesn't take");
    PosesAsC c = { g, pf };
    return win(g, pose_call_as_c, &c);
}

bool battle_loses_the_fight(GameState *g, Platform *pf)
{
    if (!poses_fit(g))
        return battle_needs_the_machine(g, 0xC1354D, "a defeat with graphics the port doesn't take");
    PosesAsC c = { g, pf };
    return lose(g, pose_call_as_c, &c);
}

/* ---- On the machine ------------------------------------------------------------------------ */

/* Wait for the next frame ($C1:007E): the wait reads none of the registers
 * but A's high byte, which it takes along, clear where the loading or the
 * calling off leaves it so. The looks' colours are found by the whole of A
 * with each member's palette, so where the victory's tail leaves A's high
 * byte set, which the game's doesn't, the original goes on from after it. */
enum { FRAME_WAIT_READS = DC_AL | DC_X | DC_Y | DC_FLAGS, AFTER_THE_TAIL = 0xC13571 };

static bool wait_for_the_next_frame(Snes *s, uint32_t site)
{
    return battle_wait_frame(s, site, FRAME_WAIT, FRAME_WAIT_READS);
}

static bool finish_the_frame(Snes *s, uint32_t site)
{
    return battle_run_long(s, site, FRAME_TAIL, battle_frame_wait_tail);
}

static const uint32_t MENU_TAKEN_AWAY_WAITS[BATTLE_PARTY_SIZE] = { 0xC1358E, 0xC13596, 0xC1359E };

static bool pose_call_on_the_machine(void *ctx, PoseCall call)
{
    Snes *s = ctx;
    switch (call) {
    case POSES_FRAME: return wait_for_the_next_frame(s, 0xC13686);
    case POSES_TAIL: return finish_the_frame(s, 0xC13689);
    case VICTORY_TAIL:
        if (!finish_the_frame(s, 0xC1356D))
            return false;
        if (s->cpu.a & 0xFF00) {
            m_stop_at(s, AFTER_THE_TAIL, 0);
            return false;
        }
        return true;
    case VICTORY_FIRST_MENU_GONE:
    case VICTORY_SECOND_MENU_GONE:
    case VICTORY_THIRD_MENU_GONE:
        s->cpu.a &= 0x00FF;
        return wait_for_the_next_frame(s, MENU_TAKEN_AWAY_WAITS[call - VICTORY_FIRST_MENU_GONE]);
    case VICTORY_WINDOW: return battle_run(s, 0xC135A6, MENU_WINDOW, battle_draw_menu_window);
    case DEFEAT_TAIL: return finish_the_frame(s, 0xC1354D);
    case DEFEAT_FRAME: return wait_for_the_next_frame(s, 0xC13554);
    case POSES_STRUCK: return battle_strike_the_poses_as_their_own(s);
    default: return false;
    }
}

/* The poses' loading leaves A's high byte clear, which the waits take along;
 * the last step leaves the carry of its last count, set, which the steps'
 * askers read. */
bool battle_strike_the_poses(Snes *s)
{
    s->cpu.a &= 0x00FF;
    if (!strike_the_poses(state_of(s), pose_call_on_the_machine, s))
        return false;
    m_set_flag(s, CPU_FLAG_C, true);
    return true;
}

bool battle_strike_the_poses_as_their_own(Snes *s)
{
    bool posed;
    NATIVE_TIME_AS_SIBLING(s, 0xC135DD, posed = battle_strike_the_poses(s));
    return posed;
}

/* ---- The entries -------------------------------------------------------------------------- */

// $C1:356D
void battle_victory_step(Snes *s)
{
    if (win(state_of(s), pose_call_on_the_machine, s))
        m_rts(s);
}

// $C1:354D
void battle_defeat_step(Snes *s)
{
    if (lose(state_of(s), pose_call_on_the_machine, s))
        m_rts(s);
}

// $C1:35DD
void battle_strike_the_poses_entry(Snes *s)
{
    if (battle_strike_the_poses(s))
        m_rts(s);
}

// $C1:369A
/* A frame that steps the poses leaves A's high byte clear and the carry of
 * its last count, set; one that sends leaves them as they came. */
void battle_step_the_poses_entry(Snes *s)
{
    if (step_the_poses(state_of(s))) {
        s->cpu.a &= 0x00FF;
        m_set_flag(s, CPU_FLAG_C, true);
    }
    m_rts(s);
}

/* The guards: on the battle's page, and their state halves; a step of the
 * poses with the members' frames as the poses keep them. */
bool battle_poses_ready(Snes *s)
{
    vars_peek(true);
    bool ok = poses_fit(state_of(s));
    vars_peek(false);
    return battle_on_page(s) && ok;
}

/* The victory takes the members' looks off: their palettes' places within
 * the sprites' colours, as the looks' guard keeps them. */
bool battle_victory_ready(Snes *s)
{
    vars_peek(true);
    bool ok = battle_pose_looks_fit(state_of(s));
    vars_peek(false);
    return battle_poses_ready(s) && ok;
}

bool battle_poses_step_ready(Snes *s)
{
    vars_peek(true);
    bool ok = poses_keep_to_their_frames(state_of(s));
    vars_peek(false);
    return battle_poses_ready(s) && ok;
}
