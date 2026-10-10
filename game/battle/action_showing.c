/* Battle: an action shown.
 *
 * Once an action is worked out, the battle shows it (the battle's step 4,
 * which the turns ask for, battle.h). The objects are put as a showing
 * starts them; the action's subjects are listed for the animation scripts'
 * channels (the actor, the two others taking part, the targets), and its
 * animation is loaded (battle/action_animation.c). Then the animation plays,
 * a frame at a time -- the scheduler's step, the frame step with the menu's
 * turn (battle/frame_step.c), the effects' and the palettes' animations, the
 * places the scripts follow, the battlefield's grid and the combatants'
 * boxes, and the scripts' own step -- until the scripts signal they are done.
 * The effects' sounds are cut off and the screen's scripts let finish. Then
 * the effects stop, the enemies drift on from where the action left them,
 * every combatant takes its pose afresh, and a party member the damage shown
 * takes to nothing falls at once, before its HP says so (the damage is
 * applied after, battle/damage.c).
 *
 * The showing waits for the next frame between its steps (the original's
 * wait at $C1:007E), and runs the steps that call the original's code as the
 * original's calls make them (battle_run): so the showing is a bridge, its
 * steps C where they are. While it runs it keeps the frame's phase on the
 * stack, where the original keeps it across its calls. */
#include "battle/action_animation_data.h"
#include "battle/animation.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/effect_parts.h"
#include "battle/entries.h"
#include "battle/script_language.h"
#include "charge.h"
#include "original_effects.h"
#include "platform/audio.h"
#include "vars.h"

/* ---- Before the animation --------------------------------------------------------------------- */

/* A pose none has, which every combatant's last is set to so that it takes
 * its pose afresh. */
enum { POSE_NONE_HAS = 0x55 };

// $CC:F102
static void forget_poses(GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++)
        set_battle_object_pose_last(g, slot, POSE_NONE_HAS);
}

/* The subjects' places: the actor's channel, the two others', and the
 * targets', the first then the others, each list ending in LIST_ENDS. */
enum { ACTOR_PLACE = 0, OTHERS_PLACE = 1, TARGETS_PLACE = 3, PLACES_WITH_A_START = 3 };

/* A member acting alone has the other two members as its others: the next
 * one round the party, then the one after (the ROM's table at $CC:FBC8). */
static uint8_t member_round_from(uint8_t member, unsigned steps)
{
    return (uint8_t)((member + steps) % BATTLE_PARTY_SIZE);
}

/* A list runs past the subjects' table where the action has more than six
 * other targets, onto the battle's cells after it, as the original's does;
 * the scripts read it there (script_listed). */
static void list_subject(GameState *g, unsigned place, uint8_t slot)
{
    if (place < BATTLE_SCRIPT_SUBJECTS_COUNT)
        set_battle_script_subjects(g, place, slot);
    else
        set_battle_script_subjects_unchecked(g, place, slot);
}

/* A target listed: in the subjects' list, and in the showing's own
 * (battle_showing_targets), each list ended after it. */
enum { END_OF_LIST = 0xFF };

static void list_target(GameState *g, unsigned n, uint8_t slot)
{
    list_subject(g, TARGETS_PLACE + n, slot);
    list_subject(g, TARGETS_PLACE + n + 1, END_OF_LIST);
    set_battle_showing_targets(g, n, slot);
    set_battle_showing_targets(g, n + 1, END_OF_LIST);
}

/* The first target's bit is the action's first set one, slot 0 the top bit;
 * an action with none has slot 0 first. */
static uint8_t first_target(uint16_t bits)
{
    if (bits == 0)
        return 0;
    uint8_t slot = 0;
    while (!(bits & (0x8000u >> slot)))
        slot++;
    return slot;
}

/* The original walks the target bits out of the first work cells, the other
 * targets' last. */
static void leave_the_bits_walked(GameState *g, uint16_t others)
{
    uint16_t left = (uint16_t)(others << BATTLE_COMBATANTS);
    set_battle_work(g, 0, (uint8_t)left);
    set_battle_work(g, 1, (uint8_t)(left >> 8));
}

// $CC:F156
/* The showing's subjects for the scripts' channels: the actor; the two
 * others taking part in its tech, or for a member acting alone the other two
 * members; the first target and the others in slot order. The first three
 * channels' scripts start from where their subjects stand. */
static void list_the_showing(GameState *g)
{
    uint8_t actor = battle_action_actor(g);
    list_subject(g, ACTOR_PLACE, actor);
    for (unsigned other = 0; other < 2; other++) {
        uint8_t slot = battle_action_further_targets(g, other);
        if (battle_slot_is_party(actor) && (slot & LIST_ENDS))
            slot = member_round_from(actor, other + 1);
        list_subject(g, OTHERS_PLACE + other, slot);
    }
    list_target(g, 0, first_target(battle_action_target_bits(g)));
    uint16_t others = battle_action_other_targets(g);
    unsigned listed = 1;
    for (uint8_t slot = 0; slot < BATTLE_COMBATANTS; slot++)
        if (others & (0x8000u >> slot))
            list_target(g, listed++, slot);
    ORIGINAL_EFFECT(leave_the_bits_walked(g, others));
    for (unsigned place = 0; place < PLACES_WITH_A_START; place++) {
        uint8_t slot;
        if (!script_listed(g, place, &slot))
            break;
        set_battle_script_start_x(g, place, combatant_x(g, slot));
        set_battle_script_start_y(g, place, combatant_y(g, slot));
    }
}

/* ---- After the animation ---------------------------------------------------------------------- */

/* The effects stop animating: none animates, shows a frame or waits. */
static void stop_the_effects(GameState *g)
{
    for (unsigned effect = 0; effect < BATTLE_EFFECT_ANIMATING_COUNT; effect++) {
        set_battle_effect_animating(g, effect, 0);
        set_battle_effect_frame_time(g, effect, 0);
        set_battle_effect_frame(g, effect, 0);
        set_battle_effect_shown_wait(g, effect, 0);
        set_battle_effect_wait(g, effect, 0);
    }
}

/* An enemy the action animated drifts on from where it stands now. */
static void enemies_drift_from_where_they_stand(GameState *g)
{
    uint8_t slot;
    for (unsigned place = 0; script_listed(g, place, &slot); place++) {
        if (battle_slot_is_party(slot))
            continue;
        set_battle_enemy_drift_from_x(g, slot - BATTLE_FIRST_ENEMY, combatant_x(g, slot));
        set_battle_enemy_drift_from_y(g, slot - BATTLE_FIRST_ENEMY, combatant_y(g, slot));
    }
}

/* The first number shown over a party member (set 0's) is its damage, and
 * the original tests its kind and the byte after it as one word. */
static bool damage_shown_takes_all(const GameState *g, unsigned member)
{
    bool damage = battle_number_records_kind(g, member) == NUMBER_DAMAGE && battle_number_records_last_byte(g, member) == 0;
    uint16_t hp = battle_stats_hp(g, member), shown = battle_number_records_value(g, member);
    return damage && hp != 0 && shown != 0 && shown >= hp;
}

/* The original counts the members in its first work cell, and each one
 * falling in a cell of its own after it. */
static void leave_the_fallen_counted(GameState *g)
{
    set_battle_work(g, 0, BATTLE_PARTY_SIZE);
    set_battle_work(g, 1, 0);
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_work(g, 2 + member, battle_member_falling(g, member));
    set_battle_work(g, 2 + BATTLE_PARTY_SIZE, 0);
}

// $C1:41BE
/* A party member falls where the damage shown over it takes all the HP it
 * has. */
static void find_the_members_falling(GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_member_falling(g, member, damage_shown_takes_all(g, member));
    ORIGINAL_EFFECT(leave_the_fallen_counted(g));
}

/* A party member among the action's targets. */
static bool a_member_targeted(const GameState *g)
{
    for (unsigned n = 0; n < BATTLE_SHOWING_TARGETS_COUNT; n++) {
        uint8_t slot = battle_showing_targets(g, n);
        if (slot & LIST_ENDS)
            return false;
        if (battle_slot_is_party(slot))
            return true;
    }
    return false;
}

/* A member falling shows the frame of its fall for the way it faces, built
 * at once. */
static void show_the_fall(GameState *g, unsigned member)
{
    set_battle_member_falling(g, member, 0);
    uint8_t frame = battle_character_falling_frame(g, party_members(g, member), battle_object_facing(g, member));
    battle_show_object_frame(g, member, frame);
    if (battle_object_builds_graphics(g, member))
        TIMED_AS(g, 0xC11C4A, battle_build_frame_graphics(g, member, frame));
}

/* The fall is sent to VRAM the frame after next. */
enum { SEND_THE_FALL_AFTER_FRAMES = 2 };

/* ---- The showing ------------------------------------------------------------------------------ */

/* The developers' switches force the action's flags. */
static void apply_the_animation_switch(GameState *g)
{
    switch (battle_animation_switch(g)) {
    case ANIMATIONS_AS_THEY_ARE:
        break;
    case ANIMATIONS_ALL_CRITICAL:
        set_battle_action_flags(g, ACTION_CRITICAL_HIT);
        set_battle_action_extra(g, 0);
        break;
    case ANIMATIONS_ALL_SECOND:
        set_battle_action_extra(g, ACTION_MISSED);
        set_battle_action_flags(g, 0);
        break;
    case ANIMATIONS_ALL_PLAIN:
        set_battle_action_flags(g, 0);
        set_battle_action_extra(g, 0);
        break;
    }
}

/* Wait for the next frame ($C1:007E), from the call at `site`: the wait reads
 * none of the registers but A's high byte, which it takes along. */
enum { FRAME_WAIT_READS = DC_AL | DC_X | DC_Y | DC_FLAGS };

static bool wait_for_the_next_frame(Snes *s, uint32_t site) { return battle_call(s, site, 0xC1007E, FRAME_WAIT_READS); }

static bool frame_step(Snes *s, uint32_t site) { return battle_run(s, site, 0xC110FA, battle_frame_step); }

/* The sound effects are cut off: the driver starts an effect of none on
 * their voices. The driver's command routine reads none of the registers. */
enum { NO_SOUND_EFFECT = 0xFF };
#define DRIVER_IGNORES (DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C)

static bool cut_off_the_sounds(Snes *s)
{
    au_command(m_platform(s), PF_CALL_DC(0xC140DD, 0xC70004, DRIVER_IGNORES),
               (AudioCommand){ .command = AU_START_EFFECT, .p1 = NO_SOUND_EFFECT, .count = 1 });
    return battle_back(s, 0xC140DD + BATTLE_JSL_SIZE, BATTLE_PAGE, BATTLE_DATA_BANK);
}

/* A frame of the animation playing. */
static bool play_a_frame(Snes *s)
{
    GameState *g = state_of(s);
    if (!wait_for_the_next_frame(s, 0xC140B0))
        return false;
    TIMED_AS(g, 0xC1423A, battle_take_due_animation_step(g));
    if (!frame_step(s, 0xC140B6))
        return false;
    TIMED_AS(g, 0xCCF278, battle_animate_effect_frames(g));
    TIMED_AS(g, 0xCCF1E7, battle_step_palette_animations(g));
    if (!battle_run(s, 0xC140C1, 0xC14212, battle_update_animation_positions))
        return false;
    TIMED_AS(g, 0xCCF110, battle_mark_combatants_on_grid(g));
    TIMED_AS(g, 0xC1283D, battle_update_all_bounds(g));
    return battle_run(s, 0xC140CB, 0xC14BBE, battle_run_scripts);
}

/* Every combatant takes its pose afresh: the poses' round
 * (battle/pose_looks.c) goes once round all of them from the first, the
 * combatant it would have looked at next kept meanwhile. */
static void take_poses_afresh(GameState *g)
{
    uint8_t next = battle_next_enemy_checked(g);
    set_battle_next_enemy_checked(g, 0);
    set_battle_poses_round_done(g, 0);
    while (!battle_poses_round_done(g))
        TIMED_AS(g, 0xC1305C, battle_go_round_the_poses(g));
    set_battle_next_enemy_checked(g, next);
}

/* A party member the damage shown takes to nothing falls now, and the
 * showing waits for its fall to be sent. */
static bool members_fall(Snes *s)
{
    GameState *g = state_of(s);
    if (!a_member_targeted(g))
        return true;
    TIMED_AS(g, 0xC141BE, find_the_members_falling(g));
    if (!wait_for_the_next_frame(s, 0xC1414B))
        return false;
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (battle_member_falling(g, member))
            show_the_fall(g, member);
    set_battle_reupload_countdown(g, SEND_THE_FALL_AFTER_FRAMES);
    return wait_for_the_next_frame(s, 0xC141B4) && wait_for_the_next_frame(s, 0xC141B7);
}

// $C1:4058
static bool show_the_action(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_orbits_held(g, (uint8_t)(battle_orbits_held(g) + 1));
    TIMED_AS(g, 0xCCF06B, battle_reset_objects(g));
    if (!wait_for_the_next_frame(s, 0xC1405F))
        return false;
    set_battle_queued_uploads_on(g, 0);
    apply_the_animation_switch(g);
    uint8_t phase = battle_frame_phase(g);
    m_push8(s, phase);
    ORIGINAL_EFFECT(set_battle_animation_second_set(g, battle_action_extra(g) & ACTION_MISSED));
    TIMED_AS(g, 0xCCF102, forget_poses(g));
    if (!wait_for_the_next_frame(s, 0xC140A0) || !frame_step(s, 0xC140A3))
        return false;
    set_battle_effects_running(g, (uint8_t)(battle_effects_running(g) + 1));
    TIMED_AS(g, 0xCCF156, list_the_showing(g));
    if (!battle_run(s, 0xC140AD, 0xC14310, battle_load_action_animation))
        return false;
    while (true) {
        if (!play_a_frame(s))
            return false;
        if (battle_script_signal(g))
            break;
    }
    if (!cut_off_the_sounds(s))
        return false;
    while (true) {
        if (!wait_for_the_next_frame(s, 0xC140E1) || !frame_step(s, 0xC140E4))
            return false;
        if (!battle_screen_scripts_running(g))
            break;
    }
    m_pull8(s);
    set_battle_frame_phase(g, phase);
    set_battle_color_rotation_on(g, 0);
    stop_the_effects(g);
    enemies_drift_from_where_they_stand(g);
    if (!wait_for_the_next_frame(s, 0xC14116))
        return false;
    TIMED_AS(g, 0xCCF06B, battle_reset_objects(g));
    take_poses_afresh(g);
    set_battle_graphics_upload_request(g, 0);
    if (!members_fall(s))
        return false;
    set_battle_effects_running(g, 0);
    return true;
}

/* The poses' round ends on its last count, which leaves the carry set, and
 * nothing after it touches the carry; the battle's steps hand it back. */
void battle_show_action(Snes *s)
{
    if (!show_the_action(s))
        return;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* ---- The other entries ---------------------------------------------------------------------- */

/* It leaves the pose it set in A, its high byte clear. */
void battle_forget_poses_entry(Snes *s)
{
    forget_poses(state_of(s));
    s->cpu.a = POSE_NONE_HAS;
    m_rtl(s);
}

void battle_list_the_showing_entry(Snes *s)
{
    list_the_showing(state_of(s));
    m_rtl(s);
}

/* The members' count leaves A's high byte clear and the carry of its last
 * compare, set. */
void battle_find_the_members_falling_entry(Snes *s)
{
    find_the_members_falling(state_of(s));
    s->cpu.a = 0;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

/* The guard: the actor is a combatant (the action's subjects index the
 * objects' places by it), on the battle's page with A's high byte clear,
 * which the showing's loaders index with. */
bool battle_show_action_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool fits = battle_names_combatant(battle_action_actor(g));
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && fits;
}
