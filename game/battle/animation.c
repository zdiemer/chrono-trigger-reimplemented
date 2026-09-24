/* Battle: how the combatants animate (battle/animation.h).
 *
 * A combatant shows an animation, a list of frames each shown for its own
 * time. A party member's frames and times are read from its frame and delay
 * lists in the ROM, the part for its facing, with every frame it takes: the
 * animation is whatever its pose gives it (combatant_choose_pose, later in
 * this file: knocked out, asleep, weak with low HP, or standing ready), or
 * the party turning to run while L and R are held. An enemy's
 * frames, at most four, are loaded from its lists whenever the animation its
 * script gives it or its facing changes (the enemies' loaded frame lists,
 * battle_enemy_frame_graphics and _delays).
 *
 * Once a frame the battle steps three combatants' animations: the party's on
 * one frame in four, one group of enemies' on each of the other three
 * (battle_frame_phase). Each combatant counts its frame's time down, and when
 * it is up takes its next frame, whose sprite graphics are then built
 * (battle/sprites.c). Building is slow, so after one object's graphics have
 * been built in a frame the next combatants due mostly hold their frame for a
 * pass, and their group's order starts with the first of them next time.
 *
 * As they animate, the party members turn to face their nearest enemy, one
 * member at a time, and each enemy's animation may run an effect beside it
 * (the enemies' per-animation effects, at the end of this file), whose
 * handlers are still the original's. */
#include "battle/animation.h"

#include "battle/animation_data.h"
#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "original_effects.h"
#include "vars.h"

/* A frame list's times are kept in fifths of a step of the animations, and
 * a frame shows for at least one step. */
enum { TIME_UNITS_PER_STEP = 5 };

/* A frame list restarted at its next step: the frame before its first. */
enum { BEFORE_THE_FIRST_FRAME = 0xFF };

/* ---- The groups the combatants animate in ------------------------------------------------ */

/* Which combatants a pass steps: the party, or a group of enemies. Their
 * number within their side (a party member, or enemy 0-7) is what the order
 * holds, with NO_ONE for the empty third place of the last group. */
typedef enum { THE_PARTY, ENEMIES_0_TO_2, ENEMIES_3_TO_5, ENEMIES_6_AND_7, GROUPS } AnimationGroup;
enum { GROUP_PLACES = 3, NO_ONE = 0xFF, NO_ONE_BIT = 0x80 };

static const struct {
    uint8_t first_slot, size;
} groups[GROUPS] = { { 0, 3 }, { 3, 3 }, { 6, 3 }, { 9, 2 } };

static unsigned enemy_slot(unsigned enemy) { return BATTLE_FIRST_ENEMY + enemy; }

/* Hold the combatant's frame for a pass, while another object's graphics are
 * uploaded: its group's next order starts with it. */
static void hold_frame(GameState *g, unsigned slot)
{
    set_battle_anim_held(g, slot, (uint8_t)(battle_anim_held(g, slot) + 1));
}

static void leave_order(GameState *g, const uint8_t order[GROUP_PLACES])
{
    for (unsigned place = 0; place < GROUP_PLACES; place++)
        set_battle_anim_order(g, place, order[place]);
}

/* The order the group animates in this pass: round the group from the first
 * of it that held its frame last pass, whose hold is then over, or from its
 * first. */
static void choose_order(GameState *g, AnimationGroup group, uint8_t order[GROUP_PLACES])
{
    unsigned first = groups[group].first_slot, size = groups[group].size;
    unsigned start = 0;
    for (unsigned i = 0; i < size; i++) {
        if (battle_anim_held(g, first + i)) {
            set_battle_anim_held(g, first + i, 0);
            start = i;
            break;
        }
    }
    unsigned side_first = group == THE_PARTY ? 0 : BATTLE_FIRST_ENEMY;
    for (unsigned place = 0; place < GROUP_PLACES; place++)
        order[place] = place < size ? (uint8_t)(first - side_first + (start + place) % size) : (uint8_t)NO_ONE;
    ORIGINAL_EFFECT(leave_order(g, order));
}

/* One object's graphics built this frame: the upload has room for no more,
 * but for the enemies of the smallest graphics kind in slot 4 or from slot 6
 * on. Two built, nobody else's frame is taken. */
enum { ONE_BUILD = 1, SMALLEST_KIND = 0 };

static bool member_waits_for_upload(const GameState *g) { return battle_graphics_built(g) == ONE_BUILD; }

static bool enemy_waits_for_upload(const GameState *g, unsigned slot)
{
    uint8_t built = battle_graphics_built(g);
    if (built == 0)
        return false;
    if (built != ONE_BUILD || battle_object_kind(g, slot) != SMALLEST_KIND)
        return true;
    return !(slot == 4 || slot >= 6);
}

/* ---- A party member's frames -------------------------------------------------------------- */

/* The original's cells the party's step and its pose choice leave. */
enum { LEFT_STANCE = 0x04, LEFT_FACING_PART = 0x02, LEFT_PLACE = 0x12, LEFT_TURNED = 0x00, LEFT_TURN_HOLDER = 0x01 };
static void leave_byte(GameState *g, unsigned cell, uint8_t v) { set_battle_work(g, cell, v); }
static void leave_word(GameState *g, unsigned cell, uint16_t v)
{
    set_battle_work(g, cell, (uint8_t)v);
    set_battle_work(g, cell + 1, (uint8_t)(v >> 8));
}

/* The combatant whose pose is being chosen, and the enemy whose animation
 * effect runs: the original's routines the steps call (the pose's setup, the
 * effects' handlers) take them from their cells. */
enum { POSING_CELL = 0x94 - 0x80, EFFECT_ENEMY_CELL = 0x9C - 0x80 };
static void give_posing(GameState *g, unsigned slot) { leave_word(g, POSING_CELL, (uint16_t)slot); }
static unsigned posing(const GameState *g)
{
    return (unsigned)(battle_work(g, POSING_CELL) | battle_work(g, POSING_CELL + 1) << 8);
}
static void give_effect_enemy(GameState *g, unsigned enemy) { leave_word(g, EFFECT_ENEMY_CELL, (uint16_t)enemy); }

/* A member's frame lists are in bank $E4, four entries an animation, and
 * each facing's part of them a stride after the last. */
enum { PARTY_LISTS_BANK = 0xE4, FRAMES_PER_ANIMATION = 4 };

static uint8_t party_list_byte(const GameState *g, uint16_t at)
{
    return battle_frame_list_byte(g, battle_at(PARTY_LISTS_BANK, at));
}

static uint16_t facing_stride(const GameState *g, unsigned slot)
{
    return (uint16_t)(battle_facing_stride_low(g, slot) | battle_facing_stride_high(g, slot) << 8);
}

/* The member's step keeps the places of its lists in the animation
 * scheduler's first entry (battle_scheduled_frame_lists), whatever member it
 * is: a scheduled animation of the first combatant finds them there. */
static void keep_member_lists(GameState *g, uint16_t frames_at, uint16_t times_at)
{
    set_battle_scheduled_frame_lists(g, 0, frames_at);
    set_battle_scheduled_delay_lists(g, 0, times_at);
}

// $C1:2E4C
/* Take the member's next frame of an animation: its time comes from the
 * delay list, the list starting over where a time is 0, and its graphics
 * from the frame list, in its facing's part. The frame is what the member
 * shows next. */
static uint8_t take_next_party_frame(GameState *g, unsigned member, uint8_t animation)
{
    uint16_t place = (uint16_t)(animation * FRAMES_PER_ANIMATION);
    uint16_t frames_at = (uint16_t)(battle_object_frame_lists(g, member) + place);
    uint16_t times_at = (uint16_t)(battle_object_delay_lists(g, member) + place);
    keep_member_lists(g, frames_at, times_at);
    uint16_t facing_part = (uint16_t)battle_multiply_long(g, battle_object_facing(g, member), facing_stride(g, member));

    uint8_t frame = (uint8_t)(battle_party_frame(g, member) + 1);
    uint8_t time = party_list_byte(g, (uint16_t)(times_at + frame));
    if (time == 0) {
        frame = 0;
        time = party_list_byte(g, times_at);
    }
    set_battle_party_frame(g, member, frame);
    uint8_t steps = (uint8_t)battle_divide_by(g, time, TIME_UNITS_PER_STEP).quotient;
    set_battle_party_frame_delay(g, member, steps != 0 ? steps : 1);

    uint8_t graphics = party_list_byte(g, (uint16_t)(frames_at + facing_part + frame));
    battle_show_object_frame(g, member, graphics);
    ORIGINAL_EFFECT(leave_word(g, LEFT_FACING_PART, facing_part));
    return graphics;
}

/* While L and R are held, a member standing ready turns to run, from the
 * start of that animation. */
static uint8_t with_running(GameState *g, unsigned member, uint8_t animation)
{
    if (animation != ANIMATION_BATTLE_STANCE || battle_lr_held(g) == 0)
        return animation;
    ORIGINAL_EFFECT(set_battle_pose_animation(g, ANIMATION_RUNNING));
    set_battle_party_frame(g, member, BEFORE_THE_FIRST_FRAME);
    return ANIMATION_RUNNING;
}

/* A member's graphics are built for its new frame, except while it stands
 * ready in the animation it showed last, facing as it was, and isn't the
 * member choosing a command. */
static bool member_graphics_change(GameState *g, unsigned member, uint8_t animation, bool turned)
{
    bool new_animation = animation != battle_object_pose_shown(g, member);
    if (new_animation)
        set_battle_object_pose_shown(g, member, animation);
    return new_animation || animation != ANIMATION_BATTLE_STANCE || turned || battle_menu_member(g) == member;
}

/* ---- Facing the nearest enemy -------------------------------------------------------------- */

/* The turn to face goes round the party: the member holding it turns each
 * time it animates, and a member without it takes it once the holder has
 * turned, with TURN_TAKEN until it turns in its turn. */
enum { TURN_TAKEN = 0x80 };

static bool members_turn_to_face(GameState *g, unsigned member)
{
    uint8_t turn = battle_facing_turn(g);
    uint8_t holder = turn & (uint8_t)~TURN_TAKEN;
    ORIGINAL_EFFECT(leave_byte(g, LEFT_TURNED, 0));
    ORIGINAL_EFFECT(leave_byte(g, LEFT_TURN_HOLDER, holder));
    if (holder != member) {
        if (!(turn & TURN_TAKEN))
            set_battle_facing_turn(g, (uint8_t)(member | TURN_TAKEN));
        return false;
    }
    set_battle_facing_turn(g, holder);
    return true;
}

/* A member knocked out, or that gets no turns, stays facing as it is. */
static bool member_can_turn(const GameState *g, unsigned member)
{
    return !combatant_knocked_out(g, member) && !(combatant_statuses(g, member) & STATUS_NO_TURNS);
}

/* The search $C1:2986 makes for the member's nearest enemy. */
enum { NEAREST_ENEMY = 2 };

static void ask_for_nearest_enemy(GameState *g, unsigned member)
{
    set_battle_target_search(g, NEAREST_ENEMY);
    set_battle_target_searcher(g, (uint8_t)member);
}

/* The member faces the enemy found, and the direction's code takes the two
 * points it is between. */
static void aim_at_enemy_found(GameState *g, unsigned member)
{
    uint8_t enemy = battle_target_found(g);
    set_battle_member_facing_target(g, member, enemy);
    set_battle_direction_points(g, 0, battle_object_x(g, battle_combatant_object(member)));
    set_battle_direction_points(g, 1, battle_object_y(g, battle_combatant_object(member)));
    set_battle_direction_points(g, 2, battle_object_x(g, battle_combatant_object(enemy)));
    set_battle_direction_points(g, 3, battle_object_y(g, battle_combatant_object(enemy)));
    ORIGINAL_EFFECT(leave_byte(g, LEFT_TURNED, 0));
}

/* Face the way the direction found gives; true if the member turned. */
static bool face_direction(GameState *g, unsigned member, uint16_t direction)
{
    uint8_t facing = battle_facing_of_direction(g, direction);
    if (facing == battle_object_facing(g, member))
        return false;
    set_battle_object_facing(g, member, facing);
    ORIGINAL_EFFECT(leave_byte(g, LEFT_TURNED, 1));
    return true;
}

/* ---- An enemy's frames ------------------------------------------------------------------ */

/* An enemy's loaded frame list: eight entries an enemy, of which its
 * animation shows at most four. */
enum { ENEMY_LIST_ENTRIES = 8, ENEMY_FRAMES = 4 };

static uint8_t enemy_list_start(unsigned enemy) { return (uint8_t)(enemy * ENEMY_LIST_ENTRIES); }

/* Which part of the enemy's lists an animation's frames start at: resting
 * animations one part, the stopped one another, and the acting ones the
 * first. */
enum { RESTING = 0, ACTING = 1 };
static uint8_t animation_list_offset(uint8_t part)
{
    static const uint8_t offsets[] = { 12, 4, 24 };
    return offsets[part < 2 ? part : 2];
}

static bool enemy_list_stale(const GameState *g, unsigned enemy)
{
    unsigned slot = enemy_slot(enemy);
    return battle_enemy_animation_loaded(g, enemy) != battle_enemy_animation(g, enemy)
           || battle_object_facing(g, slot) != battle_object_facing_loaded(g, slot);
}

/* What the original leaves of a frame list it loaded: the list pointers,
 * the part and where the frames were copied from and to. */
enum { LEFT_FRAMES_POINTER = 6, LEFT_TIMES_POINTER = 9, LEFT_LIST_PART = 0x06, LEFT_LIST_START = 0x02,
       LEFT_LIST_END = 0x04, LEFT_COUNT = 0x00 };
static void leave_loaded_list(GameState *g, uint32_t frames, uint32_t times, uint8_t part_offset, uint16_t start,
                              uint8_t list_start)
{
    for (unsigned b = 0; b < 3; b++) {
        set_battle_data_pointers(g, LEFT_FRAMES_POINTER + b, (uint8_t)(frames >> 8 * b));
        set_battle_data_pointers(g, LEFT_TIMES_POINTER + b, (uint8_t)(times >> 8 * b));
    }
    leave_byte(g, LEFT_LIST_PART, part_offset);
    leave_word(g, LEFT_LIST_START, start);
    leave_word(g, LEFT_LIST_END, (uint16_t)(list_start + ENEMY_LIST_ENTRIES));
    leave_byte(g, LEFT_COUNT, 0);
}

// $C1:3364
/* Load the enemy's frame list for its animation and facing: the frames'
 * graphics from its facing's part, and their times, which don't differ by
 * facing. An acting animation is the enemy's own again: it no longer takes
 * its turn whatever animation it shows (battle_enemy_acts_anyway). */
static void load_enemy_frame_list(GameState *g, unsigned enemy)
{
    unsigned slot = enemy_slot(enemy);
    uint8_t animation = battle_enemy_animation(g, enemy);
    set_battle_enemy_animation_loaded(g, enemy, animation);
    set_battle_object_facing_loaded(g, slot, battle_object_facing(g, slot));
    uint32_t frames = battle_object_frame_lists(g, slot), times = battle_object_delay_lists(g, slot);
    uint16_t facing_part = (uint16_t)battle_multiply_long(g, battle_object_facing(g, slot), facing_stride(g, slot));
    uint8_t part = battle_enemy_animation_part(g, animation);
    if (part == ACTING)
        set_battle_enemy_acts_anyway(g, enemy, 0);
    uint8_t offset = animation_list_offset(part);
    uint16_t start = (uint16_t)(offset + facing_part);
    uint8_t list_start = enemy_list_start(enemy);
    for (unsigned i = 0; i < ENEMY_LIST_ENTRIES; i++)
        set_battle_enemy_frame_graphics(g, list_start + i, battle_frame_list_byte(g, frames + start + i));
    for (unsigned i = 0; i < ENEMY_LIST_ENTRIES; i++)
        set_battle_enemy_frame_delays(g, list_start + i,
                                      battle_enemy_frame_time(g, battle_frame_list_byte(g, times + offset + i)));
    ORIGINAL_EFFECT(leave_loaded_list(g, frames, times, offset, start, list_start));
}

/* The frame the enemy's loaded list has at a place: that one, or the first
 * where the list has ended there (its time is 0). */
static uint8_t frame_or_first(GameState *g, unsigned enemy, uint8_t frame)
{
    ORIGINAL_EFFECT(leave_byte(g, LEFT_COUNT, enemy_list_start(enemy)));
    return battle_enemy_frame_delays(g, (uint8_t)(enemy_list_start(enemy) + frame)) != 0 ? frame : 0;
}

static uint8_t list_place(unsigned enemy, uint8_t frame) { return (uint8_t)(enemy_list_start(enemy) + frame); }

// $C1:3407
/* The enemy's frame is up: show it. */
static void show_enemy_frame(GameState *g, unsigned enemy)
{
    uint8_t frame = frame_or_first(g, enemy, battle_enemy_frame(g, enemy));
    set_battle_enemy_frame(g, enemy, frame);
    battle_show_object_frame(g, enemy_slot(enemy), battle_enemy_frame_graphics(g, list_place(enemy, frame)));
}

/* The enemy waits a pass to show its frame: its time is up again then. */
static void hold_enemy_frame(GameState *g, unsigned enemy)
{
    hold_frame(g, enemy_slot(enemy));
    set_battle_enemy_frame_delay(g, enemy, (uint8_t)(battle_enemy_frame_delay(g, enemy) + 1));
}

/* Move on to the enemy's next frame and take its time. */
static void next_enemy_frame(GameState *g, unsigned enemy)
{
    uint8_t frame = (uint8_t)(battle_enemy_frame(g, enemy) + 1);
    if (frame == ENEMY_FRAMES)
        frame = 0;
    frame = frame_or_first(g, enemy, frame);
    set_battle_enemy_frame(g, enemy, frame);
    set_battle_enemy_frame_delay(g, enemy, battle_enemy_frame_delays(g, list_place(enemy, frame)));
}

/* Enemies whose graphics kind is this or larger have their frames built some
 * other way. */
enum { FIRST_KIND_NOT_BUILT = 3 };

static bool enemy_frame_built(const GameState *g, unsigned enemy)
{
    return battle_object_kind(g, enemy_slot(enemy)) < FIRST_KIND_NOT_BUILT;
}

/* The original's cells the enemies' step leaves: the enemy it is at and its
 * place in the order. */
static void leave_enemy(GameState *g, unsigned enemy) { give_effect_enemy(g, enemy); }

/* The frame phase picks the group: the party at 0, enemy groups at 1-3. The
 * original counts the phase down to the group while it steps it, and back up
 * after unless it has reached the phase the battle's loop saved (which the
 * loop puts back itself). */
enum { PHASES = 4 };

static uint8_t phase_after_enemies(const GameState *g, uint8_t phase)
{
    uint8_t counted_down = (uint8_t)(phase - 1);
    if (counted_down == battle_frame_phase_saved(g) || counted_down >= PHASES - 1)
        return counted_down;
    return phase;
}

/* ---- A combatant's pose ------------------------------------------------------------------ */

/* The byte of the combatant's state a status pose tests, by its offset from
 * the condition byte in the stat block: the condition itself, its four
 * status bytes, or the two statuses it keeps. */
enum { TESTS_CONDITION = 0, TESTS_STATUS = 1, STATUS_BYTES = 4, TESTS_KEPT = 8, KEPT_BYTES = 2 };

static uint8_t tested_byte(const GameState *g, unsigned slot, uint8_t which)
{
    if (which == TESTS_CONDITION)
        return battle_stats_condition(g, slot);
    if (which - TESTS_STATUS < STATUS_BYTES)
        return (uint8_t)(battle_stats_status(g, slot) >> 8 * (which - TESTS_STATUS));
    if (which - TESTS_KEPT < KEPT_BYTES)
        return (uint8_t)(battle_stats_status_kept(g, slot) >> 8 * (which - TESTS_KEPT));
    return 0;
}

/* A pose with its top bit set is none; an animation of STANCE is the
 * combatant's stance. The knocked-out pose is the table's first, which a
 * member falling takes too. */
enum { NO_POSE = 0xFF, POSE_NONE_BIT = 0x80, WEAK_POSE = 0x80, STANCE = 0xFF, KNOCKED_OUT_ENTRY = 0 };

/* Start the combatant's frame list over at its next step. Only the party
 * has its frame counted there: for an enemy the original's write lands past
 * the party's table, and for the last three on the frame delays of enemies
 * 0-2 (battle_party_frame's unchecked accessors). */
static void restart_frame_list(GameState *g, unsigned slot)
{
    if (battle_slot_is_party(slot))
        set_battle_party_frame(g, slot, BEFORE_THE_FIRST_FRAME);
    else
        set_battle_party_frame_unchecked(g, slot, BEFORE_THE_FIRST_FRAME);
}

/* Give the combatant a pose; a new one starts its frame list over. */
static void take_pose(GameState *g, unsigned slot, uint8_t pose)
{
    set_battle_object_pose(g, slot, pose);
    if (pose != battle_object_pose_last(g, slot))
        restart_frame_list(g, slot);
}

static void leave_pose_cells(GameState *g, unsigned slot)
{
    set_battle_posing_stat_block(g, (uint16_t)(slot * BATTLE_STAT_BLOCK_BYTES));
    leave_word(g, LEFT_TURNED, 0);
}

// $C1:2F97
/* The combatant's pose is the first of the status poses whose statuses it
 * has (the first, knocked out, also when it is falling), with its animation;
 * a pose whose animation is the stance is passed over in fights with flags.
 * With none, a party member with low HP takes its weak pose, unless it sits
 * the fight out; otherwise the combatant has no pose and its stance. A
 * combatant knocked out or falling is marked down, and its frames start over.
 *
 * The weak pose starts a member's frames over at every step: the original
 * tests the pose the member had against the weak animation's number, which
 * no pose has. */
uint8_t combatant_choose_pose(GameState *g, unsigned slot)
{
    uint8_t stance = battle_member_sitting_out(g, slot) ? ANIMATION_FIELD_STANCE : ANIMATION_BATTLE_STANCE;
    uint8_t animation = stance;
    set_battle_object_pose(g, slot, NO_POSE);
    ORIGINAL_EFFECT(leave_pose_cells(g, slot));
    bool falling = battle_member_falling(g, slot) != 0;
    if (falling || combatant_knocked_out(g, slot)) {
        set_battle_combatant_down(g, slot, 1);
        restart_frame_list(g, slot);
    }
    for (uint16_t entry = 0; !battle_status_pose_ends(g, entry); entry++) {
        BattleStatusPose pose = battle_status_pose(g, entry);
        bool has = (tested_byte(g, slot, pose.status_byte) & pose.bits) != 0;
        if (!has && !(entry == KNOCKED_OUT_ENTRY && falling))
            continue;
        if (pose.animation == STANCE && battle_fight_flags(g))
            continue;
        animation = pose.animation == STANCE ? (uint8_t)ANIMATION_BATTLE_STANCE : pose.animation;
        if (!(pose.pose & POSE_NONE_BIT))
            take_pose(g, slot, pose.pose);
        ORIGINAL_EFFECT(leave_byte(g, LEFT_STANCE, animation));
        ORIGINAL_EFFECT(set_battle_pose_animation(g, animation));
        return animation;
    }
    ORIGINAL_EFFECT(leave_byte(g, LEFT_STANCE, stance));
    if (combatant_low_hp_noted(g, slot) && !battle_member_sitting_out(g, slot)) {
        set_battle_object_pose(g, slot, WEAK_POSE);
        if (battle_object_pose_last(g, slot) != ANIMATION_WEAK && battle_slot_is_party(slot))
            restart_frame_list(g, slot);
        animation = ANIMATION_WEAK;
    }
    ORIGINAL_EFFECT(set_battle_pose_animation(g, animation));
    return animation;
}

/* ---- The enemies' animation effects ------------------------------------------------------- */

/* An enemy's animation can run an effect beside it (following its link,
 * moving on a path), every few frames: its handler, chosen by the
 * animation's effect ($CC:FBAB), is still the original's. The resting
 * animation has none, nor an animation with its top bit set. */
enum { NO_EFFECT_ANIMATION = 0, NO_EFFECT_BIT = 0x80 };

static bool effect_due(GameState *g, unsigned enemy)
{
    uint8_t left = (uint8_t)(battle_enemy_effect_delay(g, enemy) - 1);
    set_battle_enemy_effect_delay(g, enemy, left);
    if (left != 0)
        return false;
    set_battle_enemy_effect_delay(g, enemy, battle_enemy_effect_period(g, enemy));
    return true;
}

/* The effect notes the enemy's link as it goes. */
static void note_link(GameState *g, unsigned enemy)
{
    set_battle_enemy_link_seen(g, enemy, battle_enemy_link(g, enemy));
}

/* ---- The bridges and the entries ------------------------------------------------------- */

/* The steps run beside the machine: the party's calls the pose's setup
 * ($C1:308C) and, as a member faces, the search for its nearest enemy
 * ($C1:2986, through $C1:0045) and the direction to it ($C1:0222), which are
 * still the original's; and the pose's choice and the frames' graphics, run
 * as C here, are timed as their own routines (NATIVE_TIME_AS). */
typedef enum { FACING_KEPT, TURNED, STOPPED } Facing;

enum { FIND_TARGET = 5 };

// $C1:2F22
/* The member faces its nearest enemy, when it has the turn to. The search
 * and the direction run inside the original's subroutine, so its return
 * address is on the stack under their calls. */
static Facing face_nearest_enemy(Snes *s, unsigned member)
{
    GameState *g = state_of(s);
    if (!members_turn_to_face(g, member) || !member_can_turn(g, member))
        return FACING_KEPT;
    ask_for_nearest_enemy(g, member);
    m_push16(s, 0x2E2D);
    s->cpu.a = FIND_TARGET;
    /* The pose's choice leaves the overflow clear, and the search can read it. */
    m_set_flag(s, CPU_FLAG_V, false);
    if (!battle_call(s, 0xC12F60, 0xC10045, DC_X | DC_Y | DC_N | DC_Z | DC_C))
        return STOPPED;
    aim_at_enemy_found(g, member);
    if (!battle_call(s, 0xC12F82, 0xC10222, DC_A | DC_X | DC_Y | DC_FLAGS))
        return STOPPED;
    m_pull16(s);
    return face_direction(g, member, s->cpu.a) ? TURNED : FACING_KEPT;
}

// $C1:2E28
/* The member's frame is up: it takes the pose its statuses give it, faces
 * its nearest enemy in its turn, and takes its next frame, whose graphics are
 * built if they change. */
static bool next_party_frame(Snes *s, unsigned member)
{
    GameState *g = state_of(s);
    uint8_t animation = 0;
    NATIVE_TIME_AS(s, 0xC12F97, animation = combatant_choose_pose(g, member));
    /* The pose's choice hands the animation back in A, whose high byte the
     * pose's setup reads. */
    s->cpu.a = animation;
    Facing facing = face_nearest_enemy(s, member);
    if (facing == STOPPED)
        return false;
    if (!battle_call(s, 0xC12E2E, 0xC1308C, DC_AL | DC_X | DC_Y | DC_FLAGS))
        return false;
    animation = with_running(g, member, animation);
    uint8_t frame = take_next_party_frame(g, member, animation);
    if (member_graphics_change(g, member, animation, facing == TURNED) && battle_object_builds_graphics(g, member))
        NATIVE_TIME_AS(s, 0xC11C55, battle_build_frame_graphics(g, member, frame));
    return true;
}

// $C1:2D9F
/* Step the party's animations: each member present whose frame's time is
 * up takes its next frame, unless it holds its frame for the upload. */
static bool step_party(Snes *s)
{
    GameState *g = state_of(s);
    ORIGINAL_EFFECT(leave_byte(g, LEFT_STANCE, ANIMATION_BATTLE_STANCE));
    uint8_t order[GROUP_PLACES];
    choose_order(g, THE_PARTY, order);
    for (unsigned place = 0; place < GROUP_PLACES; place++) {
        unsigned member = order[place];
        ORIGINAL_EFFECT(leave_word(g, LEFT_PLACE, (uint16_t)place));
        give_posing(g, member);
        if (!battle_slot_present(g, member))
            continue;
        if (member_waits_for_upload(g)) {
            hold_frame(g, member);
            continue;
        }
        uint8_t left = (uint8_t)(battle_party_frame_delay(g, member) - 1);
        set_battle_party_frame_delay(g, member, left);
        if (left == 0 && !next_party_frame(s, member))
            return false;
    }
    ORIGINAL_EFFECT(leave_word(g, LEFT_PLACE, GROUP_PLACES));
    return true;
}

// $C1:3234
/* Step a group of enemies' animations: each enemy present reloads its frame
 * list if its animation or facing changed, and when its frame's time is up
 * shows its frame, unless it holds it for the upload, and moves on to the
 * next. */
static void step_enemy_group(Snes *s, AnimationGroup group)
{
    GameState *g = state_of(s);
    uint8_t order[GROUP_PLACES];
    choose_order(g, group, order);
    for (unsigned place = 0; place < GROUP_PLACES; place++) {
        unsigned enemy = order[place];
        if (enemy & NO_ONE_BIT)
            continue;
        ORIGINAL_EFFECT(leave_enemy(g, enemy));
        if (!battle_slot_present(g, enemy_slot(enemy)))
            continue;
        if (enemy_list_stale(g, enemy))
            load_enemy_frame_list(g, enemy);
        uint8_t left = (uint8_t)(battle_enemy_frame_delay(g, enemy) - 1);
        set_battle_enemy_frame_delay(g, enemy, left);
        if (left != 0)
            continue;
        show_enemy_frame(g, enemy);
        if (enemy_waits_for_upload(g, enemy_slot(enemy))) {
            hold_enemy_frame(g, enemy);
            continue;
        }
        if (enemy_frame_built(g, enemy))
            NATIVE_TIME_AS(s, 0xC11C55, battle_build_frame_graphics(g, enemy_slot(enemy), battle_graphics_frame(g)));
        next_enemy_frame(g, enemy);
    }
    ORIGINAL_EFFECT(set_battle_anim_order_place(g, GROUP_PLACES));
}

void battle_step_combatant_animations(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_graphics_built(g, 0);
    uint8_t phase = battle_frame_phase(g);
    if (phase == THE_PARTY) {
        if (step_party(s))
            m_rts(s);
        return;
    }
    ORIGINAL_EFFECT(set_battle_anim_order_place(g, 0));
    if (phase < GROUPS) {
        step_enemy_group(s, (AnimationGroup)phase);
        set_battle_frame_phase(g, phase_after_enemies(g, phase));
    }
    m_rts(s);
}

/* The enemies' groups take the order's places as they come, which only a
 * clear A's high byte keeps to the order (the original indexes with the whole
 * of A). */
bool battle_steps_animations(Snes *s) { return battle_on_page_a_high_clear(s); }

/* The combatant comes in its cell; the animation goes back in A. */
void battle_choose_status_pose(Snes *s)
{
    s->cpu.a = combatant_choose_pose(state_of(s), posing(state_of(s)));
    m_rts(s);
}

bool battle_poses_combatant(Snes *s)
{
    vars_peek(true);
    bool named = battle_names_combatant(posing(state_of(s)));
    vars_peek(false);
    return battle_on_page(s) && named;
}

/* The handlers, as the original's table at $C1:3760 names them, by effect. */
static const uint16_t effect_handlers[] = { 0x3772, 0x379A, 0x38BF, 0x3959, 0x3A3D, 0x3BC2, 0x3E33, 0x3F5A, 0x4021 };
enum { EFFECTS = sizeof effect_handlers / sizeof effect_handlers[0] };

/* Run the enemy's effect's handler: inside the original's subroutine, whose
 * return address is on the stack under the call. An effect past the table's
 * rows goes where the ROM's table does. */
static bool run_effect(Snes *s, unsigned enemy)
{
    GameState *g = state_of(s);
    uint8_t animation = battle_enemy_animation_loaded(g, enemy);
    set_battle_effect_kind(g, animation);
    uint8_t effect = battle_enemy_animation_effect(g, animation);
    uint16_t handler =
        effect < EFFECTS ? effect_handlers[effect] : m_jump_table(s, 0xC1, 0x3760, (uint8_t)(effect * 2));
    m_push16(s, 0x3731);
    if (!battle_call(s, 0xC1375C, 0xC10000u | handler, DC_AL | DC_X | DC_Y | DC_FLAGS))
        return false;
    m_pull16(s);
    return true;
}

// $C1:3714
/* Run the enemies' animation effects that are due, unless they stand still
 * for the battle menu. The handlers find the enemy in their cell. */
void battle_step_enemy_effects(Snes *s)
{
    GameState *g = state_of(s);
    if (!battle_timers_held(g)) {
        s->cpu.a = 0;
        for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
            give_effect_enemy(g, enemy);
            uint8_t animation = battle_enemy_animation_loaded(g, enemy);
            if (animation == NO_EFFECT_ANIMATION || !effect_due(g, enemy))
                continue;
            if (!battle_slot_present(g, enemy_slot(enemy)))
                continue;
            note_link(g, enemy);
            if (animation & NO_EFFECT_BIT)
                continue;
            if (!run_effect(s, enemy))
                return;
        }
        ORIGINAL_EFFECT(leave_enemy(g, BATTLE_ENEMIES));
    }
    m_rts(s);
}
