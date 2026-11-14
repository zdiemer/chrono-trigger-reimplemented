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
 * The poses wait for frames: the waits, the frame work's tail and the menu
 * window are calls out of them (PoseCall), made where the original makes
 * them. The poses' tables are kept in
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
enum { ROW_CELL = 0x84 };

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
    set_battle_frame_phase(g, NO_PHASE);
    set_battle_queued_uploads_on(g, 0);
    return true;
}

/* ---- The poses' frames -------------------------------------------------------------------- */

/* The poses' waits, each where the original makes it: the battle's wait for
 * the next frame ($C1:007E) and the frame work's tail ($CD:0009,
 * battle/frame.c), for each of the poses' frames, the victory's and the
 * defeat's. */
enum { FRAME_TAIL = 0xCD0009, MENU_WINDOW = 0xC10C2D, STRIKE_THE_POSES = 0xC135DD };

static bool finish_the_frame(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, FRAME_TAIL, done = battle_frame_wait_finished(g, pf));
    return done;
}

// $C1:35DD
/* The poses as a whole: loaded, then 64 frames of the battle's, each the
 * frame waited for, the frame work's tail, and the poses' step. */
static bool strike_the_poses(GameState *g, Platform *pf)
{
    load_the_poses(g);
    for (uint8_t left = POSES_FRAMES; left > 0; left--) {
        battle_wait_for_frame(g, pf);
        if (!finish_the_frame(g, pf))
            return false;
        TIMED_AS(g, 0xC1369A, (void)step_the_poses(g));
    }
    set_battle_queued_uploads_on(g, 0);
    set_battle_frame_phase(g, NO_PHASE);
    return true;
}

/* The poses, a routine of their own. */
static bool the_poses_struck(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, STRIKE_THE_POSES, done = strike_the_poses(g, pf));
    return done;
}

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

// $C1:356D
/* The victory: the fight's leftovers seen off -- the markers held back, the
 * enemies' drift stopped, each member's look taken off -- each member's menu
 * taken away a frame apart, and the menu window drawn as nobody's
 * (battle/menu_window.c); then the party's victory poses, unless the battle
 * was set up without them. */
static bool see_the_fight_off(GameState *g, Platform *pf)
{
    if (!finish_the_frame(g, pf))
        return false;
    hold_the_markers(g);
    for (unsigned enemy = 0; enemy < BATTLE_ENEMY_DRIFTING_COUNT; enemy++)
        set_battle_enemy_drifting(g, enemy, 0);
    for (unsigned slot = BATTLE_PARTY_SIZE; slot-- > 0;)
        battle_take_off_the_look(g, slot);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_step_member(g, member);
        TIMED_AS(g, 0xC11BAA, battle_call_off_the_menu(g, member));
        battle_wait_for_frame(g, pf);
    }
    set_battle_menu_member(g, MENU_NONE);
    bool drawn = false;
    TIMED_AS(g, MENU_WINDOW, drawn = battle_menu_window_drawn(g, pf));
    return drawn;
}

static bool win(GameState *g, Platform *pf)
{
    if (!see_the_fight_off(g, pf))
        return false;
    if (battle_setup_flags(g) & BATTLE_SETUP_NO_VICTORY_POSE)
        return true;
    battle_give_the_party_poses(g, PARTY_POSE_VICTORY);
    return the_poses_struck(g, pf);
}

// $C1:354D
/* The defeat: the markers held back, a frame waited, and the party's defeat
 * poses, all three of them. */
static bool lose(GameState *g, Platform *pf)
{
    if (!finish_the_frame(g, pf))
        return false;
    hold_the_markers(g);
    battle_wait_for_frame(g, pf);
    battle_give_the_party_poses(g, PARTY_POSE_DEFEAT);
    return the_poses_struck(g, pf);
}

/* What the poses take: the graphics' object a battle slot, as the
 * animations keep its high byte clear, and the members' graphics ones the
 * builders know; the victory's looks as battle_pose_looks_fit keeps them. */
static bool members_graphics_known(GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (battle_slot_present(g, member) && !battle_object_graphics_known(g, member))
            return false;
    return true;
}

static bool poses_fit(GameState *g) { return (battle_graphics_object(g) >> 8) == 0 && members_graphics_known(g); }

/* ---- The poses, the victory and the defeat ------------------------------------------------- */

bool battle_party_strikes_its_poses(GameState *g, Platform *pf)
{
    if (!poses_fit(g))
        return battle_contract_broken(g, 0xC135DD, "poses on graphics the builders don't know");
    return the_poses_struck(g, pf);
}

bool battle_wins_the_fight(GameState *g, Platform *pf)
{
    if (!poses_fit(g) || !battle_pose_looks_fit(g))
        return battle_contract_broken(g, 0xC1356D, "a victory with looks or graphics the port doesn't take");
    return win(g, pf);
}

bool battle_loses_the_fight(GameState *g, Platform *pf)
{
    if (!poses_fit(g))
        return battle_contract_broken(g, 0xC1354D, "a defeat with graphics the port doesn't take");
    return lose(g, pf);
}

