/* Battle: the animation scripts' commands for how their subjects look -- the
 * animation a combatant or an effect plays, the speed its paths run at, and
 * how it is drawn: hidden or shown, drawn on top, with a shadow, with a trail
 * of afterimages, its sprite lifted from where it stands, its attribute bits.
 *
 * A command acts on what its channel animates (battle/script_language.h):
 * the channel's combatant, the action's other targets, or, on an effect
 * channel, the channel's effect, which moves as its anchor's mover. Most of
 * the drawing commands have nothing to do on an effect channel.
 *
 * Playing an animation starts a combatant's frame lists at the animation's
 * entry, or an effect's at its animation's first record; the frames are then
 * taken as the battle steps the animations (battle/animation.c, and the
 * effects' step at $CC:F2A9). The waiting forms play the animation once and
 * hold the channel until it is over, keeping a mark on the combatant or the
 * effect while they wait, so the next frame's run of the same command knows
 * it has already started it.
 *
 * The original works in the battle's first work cells as it goes; what it
 * leaves there is scaffolding, each command's in its own leaves helper. */
#include "battle/script_language.h"

#include "battle/animation_data.h"
#include "battle/battle.h"
#include "battle/effects.h"
#include "original_effects.h"
#include "vars.h"

/* ---- Playing an animation -------------------------------------------------------------- */

/* A combatant's frame and delay lists hold four bytes an animation. */
enum { FRAMES_PER_ANIMATION = 4 };

/* Where an animation starts over: before its first frame, and with the next
 * frame's graphics taken afresh for the facing. */
enum { BEFORE_THE_FIRST_FRAME = 0xFF, TAKE_AFRESH = 0xFF };

static void play_combatant_animation(GameState *g, uint8_t combatant, uint8_t animation, PlayMode mode)
{
    uint16_t part = (uint16_t)(animation * FRAMES_PER_ANIMATION);
    set_battle_scheduled_frame_lists(g, combatant, (uint16_t)(battle_object_frame_lists(g, combatant) + part));
    set_battle_scheduled_delay_lists(g, combatant, (uint16_t)(battle_object_delay_lists(g, combatant) + part));
    set_battle_animating(g, combatant, mode);
    set_battle_animation_delay(g, combatant, 0);
    set_battle_animation_frame(g, combatant, BEFORE_THE_FIRST_FRAME);
    set_battle_object_facing_shown(g, combatant, TAKE_AFRESH);
}

/* An effect's animation by the number an operand gives: the original takes
 * it unchecked against the effect's sixteen, and finds its start by doubling
 * the number in a byte. */
enum { EFFECT_ANIMATIONS = 16 };

static uint8_t effect_animation_length(const GameState *g, uint8_t animation)
{
    return animation < EFFECT_ANIMATIONS ? battle_effect_animation_frames(g, animation)
                                         : battle_effect_animation_frames_unchecked(g, animation);
}
static uint16_t effect_animation_start(const GameState *g, uint8_t animation)
{
    uint8_t entry = (uint8_t)(animation * 2) / 2;
    return entry < EFFECT_ANIMATIONS ? battle_effect_animation_starts(g, entry) : battle_effect_animation_starts_unchecked(g, entry);
}

static void play_effect_animation(GameState *g, uint8_t effect, uint8_t animation, PlayMode mode)
{
    set_battle_effect_animation_length(g, effect, effect_animation_length(g, animation));
    set_battle_effect_animation_place(g, effect, effect_animation_start(g, animation));
    set_battle_effect_animating(g, effect, mode);
    set_battle_effect_frame_time(g, effect, 0);
    set_battle_effect_frame(g, effect, BEFORE_THE_FIRST_FRAME);
    set_battle_effect_facing_shown(g, effect, TAKE_AFRESH);
}

/* What starting an animation leaves: the animation and the mode, and for
 * combatants the last one's battle slot and its lists' step to the
 * animation, and the other targets counted. */
enum { LEFT_ANIMATION = 0x00, LEFT_PART = 0x02, LEFT_COMBATANT = 0x0A, LEFT_COUNTED = 0x0C, LEFT_MODE = 0x0E };

static void leave_played(GameState *g, ScriptChannel ch, uint8_t animation, PlayMode mode, unsigned played, uint8_t last)
{
    script_leave(g, LEFT_ANIMATION, animation);
    script_leave(g, LEFT_MODE, mode);
    if (ch.kind == SCRIPT_EFFECT)
        return;
    if (played != 0) {
        script_leave(g, LEFT_COMBATANT, last);
        script_leave_word(g, LEFT_PART, (uint16_t)(animation * FRAMES_PER_ANIMATION));
    }
    if (ch.kind == SCRIPT_OTHER_TARGETS)
        script_leave_word(g, LEFT_COUNTED, (uint8_t)played);
}

// $C1:4D08
/* Start the animation on what the channel animates. */
static void play(GameState *g, ScriptChannel ch, uint8_t animation, PlayMode mode)
{
    uint8_t combatant = 0;
    unsigned k = 0;
    if (ch.kind == SCRIPT_EFFECT)
        play_effect_animation(g, script_effect(g), animation, mode);
    else
        for (; script_each_combatant(g, ch, k, &combatant); k++)
            play_combatant_animation(g, combatant, animation, mode);
    ORIGINAL_EFFECT(leave_played(g, ch, animation, mode, k, combatant));
}

static bool combatants_animating(const GameState *g, ScriptChannel ch)
{
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, ch, k, &combatant); k++)
        if (battle_animating(g, combatant))
            return true;
    return false;
}

// $C1:4D04
/* The animation plays over and over, and the channel goes on at once. */
static ScriptFlow play_animation(GameState *g, ScriptChannel channel, uint8_t animation, PlayMode mode)
{
    play(g, channel, animation, mode);
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(play_animation, channel, animation, mode)

/* How a command plays an animation and waits for it: whether an effect is
 * shown while it plays (and the effect's or the combatant's mark it keeps is
 * the shown wait's), whether the other targets' wait is for every one of them
 * or only the first (the first's mark left set when it is every one), and
 * the mark it keeps. */
typedef struct {
    bool shown;
    bool every_target;
    uint8_t mark;
} WaitStyle;

static uint8_t combatant_mark(const GameState *g, WaitStyle style, uint8_t combatant)
{
    return style.shown ? battle_animation_shown_wait(g, combatant) : battle_animation_wait(g, combatant);
}
static void set_combatant_mark(GameState *g, WaitStyle style, uint8_t combatant, uint8_t mark)
{
    if (style.shown)
        set_battle_animation_shown_wait(g, combatant, mark);
    else
        set_battle_animation_wait(g, combatant, mark);
}
static uint8_t effect_mark(const GameState *g, WaitStyle style, uint8_t effect)
{
    return style.shown ? battle_effect_shown_wait(g, effect) : battle_effect_wait(g, effect);
}
static void set_effect_mark(GameState *g, WaitStyle style, uint8_t effect, uint8_t mark)
{
    if (style.shown)
        set_battle_effect_shown_wait(g, effect, mark);
    else
        set_battle_effect_wait(g, effect, mark);
}

enum { NOT_WAITING = 0, WAITING = 1 };

static ScriptFlow effect_plays_and_waits(GameState *g, ScriptChannel ch, uint8_t animation, WaitStyle style)
{
    uint8_t effect = script_effect(g);
    if (effect_mark(g, style, effect) == NOT_WAITING) {
        set_effect_mark(g, style, effect, style.mark);
        play(g, ch, animation, PLAY_ONCE);
        if (style.shown)
            set_battle_effect_shown(g, effect, (uint8_t)(battle_effect_shown(g, effect) + 1));
        return SCRIPT_STAY;
    }
    if (battle_effect_animating(g, effect))
        return SCRIPT_STAY;
    set_effect_mark(g, style, effect, NOT_WAITING);
    if (style.shown)
        set_battle_effect_shown(g, effect, 0);
    return SCRIPT_NEXT_FRAME;
}

/* The combatant whose mark a wait keeps: the channel's own, or the first of
 * the other targets. */
static uint8_t marked_combatant(const GameState *g, ScriptChannel ch)
{
    return ch.kind == SCRIPT_OTHER_TARGETS ? battle_script_subjects(g, OTHER_TARGETS_START) : script_combatant(g, ch);
}

static ScriptFlow play_and_wait_as(GameState *g, ScriptChannel ch, uint8_t animation, WaitStyle style)
{
    if (ch.kind == SCRIPT_EFFECT)
        return effect_plays_and_waits(g, ch, animation, style);
    uint8_t marked = marked_combatant(g, ch);
    if (combatant_mark(g, style, marked) == NOT_WAITING) {
        set_combatant_mark(g, style, marked, style.mark);
        play(g, ch, animation, PLAY_ONCE);
        return SCRIPT_STAY;
    }
    bool every = ch.kind == SCRIPT_OTHER_TARGETS && style.every_target;
    if (every ? combatants_animating(g, ch) : battle_animating(g, marked) != 0)
        return SCRIPT_STAY;
    if (!every)
        set_combatant_mark(g, style, marked, NOT_WAITING);
    return SCRIPT_NEXT_FRAME;
}

// $C1:4E22
/* The animation plays once and the channel waits for it to end: on the
 * other targets, for every one of them. An effect is shown while its
 * animation plays, and hidden after. */
static ScriptFlow play_and_wait_shown(GameState *g, ScriptChannel channel, uint8_t animation)
{
    return play_and_wait_as(g, channel, animation, (WaitStyle){ .shown = true, .every_target = true, .mark = WAITING });
}
SCRIPT_OPERATION(play_and_wait_shown, channel, animation)

// $C1:4EA2
/* The animation plays once and the channel waits for it to end: on the
 * other targets, for the first of them. */
static ScriptFlow play_and_wait(GameState *g, ScriptChannel channel, uint8_t animation)
{
    return play_and_wait_as(g, channel, animation, (WaitStyle){ .shown = false, .every_target = false, .mark = WAITING });
}
SCRIPT_OPERATION(play_and_wait, channel, animation)

// $C1:577D
/* As play_and_wait with the mark the second operand gives (a mark of 0
 * starts the animation again every frame), and on the other targets waiting
 * for every one of them. On an effect channel the original plays it over and
 * over and runs its second operand as a command, which no script does: that
 * is the original's to run (its row's needs). */
enum { LEFT_MARK = 0x00 };

static void leave_mark(GameState *g, uint8_t mark)
{
    script_leave_word(g, LEFT_PART, 0);
    script_leave(g, LEFT_MARK, mark);
}

static ScriptFlow play_and_wait_marked(GameState *g, ScriptChannel channel, uint8_t animation, uint8_t mark)
{
    ORIGINAL_EFFECT(leave_mark(g, mark));
    return play_and_wait_as(g, channel, animation, (WaitStyle){ .shown = false, .every_target = true, .mark = mark });
}
SCRIPT_OPERATION(play_and_wait_marked, channel, animation, mark)

/* ---- Speed ----------------------------------------------------------------------------- */

/* A mover's speed: a step every so many frames, of so many steps. */
static void set_mover_speed(GameState *g, Mover m, uint8_t period, uint8_t steps)
{
    set_battle_effect_period(g, mover_entry(m), period);
    set_battle_effect_steps(g, mover_entry(m), steps);
}

/* A combatant's own speed: a party member's the one its effects start from,
 * an enemy's its orbit's (the original reads the party's tables on by battle
 * slot, into the enemies' that follow). */
static uint8_t own_period(const GameState *g, uint8_t combatant)
{
    return combatant < BATTLE_PARTY_SIZE ? battle_effect_period_start(g, combatant)
                                         : battle_orbit_period(g, combatant - BATTLE_PARTY_SIZE);
}
static uint8_t own_steps(const GameState *g, uint8_t combatant)
{
    return combatant < BATTLE_PARTY_SIZE ? battle_effect_steps_start(g, combatant)
                                         : battle_orbit_radius_step(g, combatant - BATTLE_PARTY_SIZE);
}

// $C1:4F1A
/* The combatants go back to their own speed; an effect keeps its. */
static ScriptFlow own_speed(GameState *g, ScriptChannel channel)
{
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, channel, k, &combatant); k++)
        set_mover_speed(g, combatant_mover(combatant), own_period(g, combatant), own_steps(g, combatant));
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(own_speed, channel)

// $C1:4F54
/* The combatants, or the effect's anchor, move at one of the eight speeds.
 * On the other targets the original counts them in its first work cells. */
enum { LEFT_TARGETS_COUNTED = 0x00 };

static ScriptFlow set_speed(GameState *g, ScriptChannel channel, uint8_t speed)
{
    uint8_t period = battle_script_speed_period(g, speed), steps = battle_script_speed_steps(g, speed);
    if (channel.kind == SCRIPT_EFFECT) {
        set_mover_speed(g, anchor_mover(script_effect(g)), period, steps);
        return SCRIPT_NEXT_FRAME;
    }
    uint8_t combatant;
    unsigned k;
    for (k = 0; script_each_combatant(g, channel, k, &combatant); k++)
        set_mover_speed(g, combatant_mover(combatant), period, steps);
    if (channel.kind == SCRIPT_OTHER_TARGETS)
        ORIGINAL_EFFECT(script_leave_word(g, LEFT_TARGETS_COUNTED, (uint8_t)k));
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(set_speed, channel, speed)

/* ---- How the combatants are drawn ------------------------------------------------------ */

/* The flags a combatant's drawing keeps in bits 0-6, beside bit 7, which
 * sets it apart from the battlefield's plane: out of it (hidden's), or above
 * it (drawn on top's), where the targeting and the collisions pass it over.
 * The original keeps the bits it sets in a work cell. */
enum { OFF_THE_PLANE = 0x80, DRAWING_FLAGS = 0x7F, LEFT_BITS = 0x0E };

// $C1:5806, $C1:5840
/* The combatants get, or lose, a topmost entry in the draw list as well. */
static ScriptFlow drawn_on_top(GameState *g, ScriptChannel channel, bool on)
{
    ORIGINAL_EFFECT(script_leave(g, LEFT_BITS, on));
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, channel, k, &combatant); k++)
        set_battle_object_extra_draw_on(g, combatant, (uint8_t)((battle_object_extra_draw_on(g, combatant) & OFF_THE_PLANE) | on));
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(drawn_on_top, channel, on)

// $C1:5844, $C1:5879
/* The combatants are hidden, or shown. A channel's own combatant is also put
 * back on the battlefield's plane (the original sets the whole byte). */
static ScriptFlow hidden(GameState *g, ScriptChannel channel, bool on)
{
    ORIGINAL_EFFECT(script_leave(g, LEFT_BITS, on));
    if (channel.kind == SCRIPT_ONE_COMBATANT) {
        set_battle_object_hidden(g, script_combatant(g, channel), on);
        return SCRIPT_NEXT_FRAME;
    }
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, channel, k, &combatant); k++)
        set_battle_object_hidden(g, combatant, (uint8_t)((battle_object_hidden(g, combatant) & OFF_THE_PLANE) | on));
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(hidden, channel, on)

// $C1:5CF1, $C1:5D12
/* The channel's combatant leaves the battlefield's plane, or comes back to
 * it, keeping its drawing flags; only on channels 0-3. */
static ScriptFlow withdrawn(GameState *g, ScriptChannel channel, bool on)
{
    uint8_t plane = on ? OFF_THE_PLANE : 0;
    ORIGINAL_EFFECT(script_leave(g, LEFT_BITS, plane));
    if (channel.channel < OTHER_TARGETS_CHANNEL) {
        uint8_t combatant = script_combatant(g, channel);
        set_battle_object_hidden(g, combatant, (uint8_t)((battle_object_hidden(g, combatant) & DRAWING_FLAGS) | plane));
    }
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(withdrawn, channel, on)

// $C1:587D
/* The combatants' sprites are drawn where they stand again. */
static ScriptFlow sprite_back_in_place(GameState *g, ScriptChannel channel)
{
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, channel, k, &combatant); k++)
        set_combatant_sprite_offset(g, combatant, 0, 0);
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(sprite_back_in_place, channel)

// $C1:58AB
/* The combatants are back in the battle, no longer down; the original counts
 * their presence up rather than setting it. The channel goes on at once. */
static ScriptFlow bring_back(GameState *g, ScriptChannel channel)
{
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, channel, k, &combatant); k++) {
        set_battle_slot_present(g, combatant, (uint8_t)(battle_slot_present(g, combatant) + 1));
        set_battle_combatant_down(g, combatant, 0);
    }
    return SCRIPT_NEXT;
}
SCRIPT_OPERATION(bring_back, channel)

// $C1:6F9A, $C1:6FEF
/* The combatants' sprites glide up or down a step a frame until one of them
 * is at the height asked (with every one before it in the list stepped this
 * frame); a step that passes over the height glides on for good. Gliding
 * home is to height 0, where the combatant stands. */
enum { LEFT_HEIGHT = 0x00, LEFT_STEP = 0x01, LEFT_LENGTH = 0x0E };

static void leave_glide(GameState *g, uint8_t height, uint8_t step, uint8_t length)
{
    script_leave(g, LEFT_HEIGHT, height);
    script_leave(g, LEFT_STEP, step);
    script_leave(g, LEFT_LENGTH, length);
}

static ScriptFlow glide_sprite(GameState *g, ScriptChannel channel, uint8_t length, uint8_t height, uint8_t step)
{
    ORIGINAL_EFFECT(leave_glide(g, height, step, length));
    if (channel.kind == SCRIPT_EFFECT)
        return SCRIPT_NEXT_FRAME;
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, channel, k, &combatant); k++) {
        uint8_t y = battle_sprite_offset_y(g, combatant);
        if (y == height)
            return SCRIPT_NEXT_FRAME;
        set_combatant_sprite_offset_y(g, combatant, (uint8_t)(y + step));
    }
    return SCRIPT_STAY;
}
SCRIPT_OPERATION(glide_sprite, channel, length, height, step)

// $C1:6FFC, $C1:702B
/* The combatants get a shadow (counted up, as the original does), or lose
 * it. On the other targets the original starts the list at channel 1's
 * subject, so the subjects of channels 1-3 get or lose theirs too. */
enum { SHADOW_OFF = 0 };

static ScriptFlow shadow(GameState *g, ScriptChannel channel, bool on)
{
    uint8_t combatant;
    for (unsigned k = 0; script_combatant_from(g, channel, SHADOW_LIST_START, k, &combatant); k++)
        set_battle_object_shadow(g, combatant, on ? (uint8_t)(battle_object_shadow(g, combatant) + 1) : SHADOW_OFF);
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(shadow, channel, on)

// $C1:753A
/* The combatants', or the effect's, sprites are drawn with the attribute
 * bits the operand gives. */
enum { LEFT_OPERAND = 0x00 };

static ScriptFlow attribute_bits(GameState *g, ScriptChannel channel, uint8_t bits)
{
    ORIGINAL_EFFECT(script_leave(g, LEFT_OPERAND, bits));
    if (channel.kind == SCRIPT_EFFECT) {
        set_battle_effect_attribute_bits(g, script_effect(g), bits);
        return SCRIPT_NEXT_FRAME;
    }
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, channel, k, &combatant); k++)
        set_battle_object_attribute_bits(g, combatant, bits);
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(attribute_bits, channel, bits)

// $C1:7579
/* The attribute bits go back to what they started as. The original reads
 * the byte after the command into its first work cell, and never uses it. */
static ScriptFlow attribute_bits_back(GameState *g, ScriptChannel channel)
{
    ORIGINAL_EFFECT(script_leave(g, LEFT_OPERAND, script_operand(g, 1)));
    if (channel.kind == SCRIPT_EFFECT) {
        uint8_t effect = script_effect(g);
        set_battle_effect_attribute_bits(g, effect, battle_effect_attribute_bits_start(g, effect));
        return SCRIPT_NEXT_FRAME;
    }
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, channel, k, &combatant); k++)
        set_battle_object_attribute_bits(g, combatant, battle_attribute_bits_start(g, combatant));
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(attribute_bits_back, channel)

/* ---- Trails ---------------------------------------------------------------------------- */

enum { TRAIL_POINTS = 4, TRAIL_ON = 3, TRAIL_OFF = 0, TRAIL_DUE_NOW = 1 };
enum { LEFT_TRAIL_PERIOD = 0x00, LEFT_AFTERIMAGE_PERIOD = 0x01, LEFT_TRAIL_LENGTH = 0x0E, LEFT_TRAIL_STATE = 0x0F };

/* The combatant's trail starts over from where it stands now: due at once,
 * its four points all here. */
static void restart_trail(GameState *g, uint8_t combatant, uint8_t state)
{
    set_battle_trail_countdown(g, combatant, TRAIL_DUE_NOW);
    set_battle_afterimage_countdown(g, combatant, TRAIL_DUE_NOW);
    for (unsigned point = 0; point < TRAIL_POINTS; point++)
        set_battle_trail_low_x_byte(g, point, combatant, combatant_x(g, combatant));
    for (unsigned point = 0; point < TRAIL_POINTS; point++)
        set_battle_trail_low_y_byte(g, point, combatant, combatant_y(g, combatant));
    set_battle_trail_on(g, combatant, state);
}

/* Stopping a trail runs the original's starting body without operands, so it
 * sets the periods from whatever its first work cells hold. Nothing reads a
 * trail's periods while it is off, and starting it sets them. */
static void leave_stale_periods(GameState *g, uint8_t combatant)
{
    set_battle_trail_period(g, combatant, script_left(g, LEFT_TRAIL_PERIOD));
    set_battle_afterimage_period(g, combatant, script_left(g, LEFT_AFTERIMAGE_PERIOD));
}

static void leave_trail(GameState *g, bool on, uint8_t length, uint8_t trail_period, uint8_t afterimage_period)
{
    if (on) {
        script_leave(g, LEFT_TRAIL_PERIOD, trail_period);
        script_leave(g, LEFT_AFTERIMAGE_PERIOD, afterimage_period);
    }
    script_leave(g, LEFT_TRAIL_LENGTH, length);
    script_leave(g, LEFT_TRAIL_STATE, on ? TRAIL_ON : TRAIL_OFF);
}

// $C1:691B, $C1:69BA
/* The combatants leave a trail and afterimages, recorded and drawn every so
 * many frames (the operands), or stop. Channels 5-7 act on the other
 * targets, and an effect channel on nothing. */
static ScriptFlow trail(GameState *g, ScriptChannel channel, uint8_t length, bool on, uint8_t trail_period, uint8_t afterimage_period)
{
    ORIGINAL_EFFECT(leave_trail(g, on, length, trail_period, afterimage_period));
    ScriptChannel trailed = channel;
    if (channel.kind == SCRIPT_ONE_COMBATANT && channel.channel > OTHER_TARGETS_CHANNEL)
        trailed.kind = SCRIPT_OTHER_TARGETS;
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, trailed, k, &combatant); k++) {
        if (on) {
            set_battle_trail_period(g, combatant, trail_period);
            set_battle_afterimage_period(g, combatant, afterimage_period);
        } else {
            ORIGINAL_EFFECT(leave_stale_periods(g, combatant));
        }
        restart_trail(g, combatant, on ? TRAIL_ON : TRAIL_OFF);
    }
    return SCRIPT_NEXT_FRAME;
}
SCRIPT_OPERATION(trail, channel, length, on, trail_period, afterimage_period)

/* ---- The table ------------------------------------------------------------------------- */

#define SPEED(b, n) { b, OPERATION(set_speed), .speed = CONSTANT(n), .length = 1, .needs = NEEDS_SUBJECTS }

SCRIPT_OPCODES(script_pose_opcodes,
    { 0x02, OPERATION(play_animation), .animation = OPERAND(1), .mode = CONSTANT(PLAY_LOOPING), .length = 2, .needs = NEEDS_SUBJECTS },
    { 0x03, OPERATION(play_and_wait_shown), .animation = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS },
    { 0x04, OPERATION(play_and_wait), .animation = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS },
    { 0x05, OPERATION(play_and_wait), .animation = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS },
    { 0x06, OPERATION(play_and_wait), .animation = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS },
    { 0x26, OPERATION(play_and_wait_marked), .animation = OPERAND(1), .mark = OPERAND(2), .length = 3, .needs = NEEDS_A_COMBATANT_CHANNEL },

    { 0x07, OPERATION(own_speed), .length = 1, .needs = NEEDS_SUBJECTS },
    SPEED(0x08, 0), SPEED(0x09, 1), SPEED(0x0A, 2), SPEED(0x0B, 3), SPEED(0x0C, 4), SPEED(0x0D, 5), SPEED(0x0E, 6), SPEED(0x0F, 7),

    { 0x27, OPERATION(drawn_on_top), .on = CONSTANT(true), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0x28, OPERATION(drawn_on_top), .on = CONSTANT(false), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0x29, OPERATION(hidden), .on = CONSTANT(true), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0x2A, OPERATION(hidden), .on = CONSTANT(false), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0x5D, OPERATION(withdrawn), .on = CONSTANT(false), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0x5E, OPERATION(withdrawn), .on = CONSTANT(true), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0x2B, OPERATION(sprite_back_in_place), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0x2C, OPERATION(bring_back), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0xC4, OPERATION(glide_sprite), .height = OPERAND(1), .step = OPERAND(2), .length = 3, .needs = NEEDS_SUBJECTS },
    { 0xC5, OPERATION(glide_sprite), .height = CONSTANT(0), .step = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS },
    { 0xD0, OPERATION(shadow), .on = CONSTANT(true), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0xD1, OPERATION(shadow), .on = CONSTANT(false), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0xD9, OPERATION(attribute_bits), .bits = OPERAND(1), .length = 2, .needs = NEEDS_SUBJECTS },
    { 0xDA, OPERATION(attribute_bits_back), .length = 1, .needs = NEEDS_SUBJECTS },
    { 0xA4, OPERATION(trail), .on = CONSTANT(true), .trail_period = OPERAND(1), .afterimage_period = OPERAND(2), .length = 3, .needs = NEEDS_SUBJECTS },
    { 0xA5, OPERATION(trail), .on = CONSTANT(false), .length = 1, .needs = NEEDS_SUBJECTS });
