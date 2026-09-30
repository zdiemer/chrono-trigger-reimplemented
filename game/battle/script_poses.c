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
 * hold the channel until it is over, keeping a mark per combatant or effect
 * while they wait, so the next frame's run of the same command knows it has
 * already started it. */
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

/* The per-combatant and per-effect cells the animation keeps: the frame it
 * is on, and the marks the two kinds of wait keep while they wait. */
typedef enum { FRAME_ROW, SHOWN_WAIT_ROW, WAIT_ROW } AnimationRow;

static uint8_t combatant_mark(const GameState *g, AnimationRow row, uint8_t combatant)
{
    return battle_animation_wait_cells(g, row * BATTLE_COMBATANTS + combatant);
}
static void set_combatant_mark(GameState *g, AnimationRow row, uint8_t combatant, uint8_t mark)
{
    set_battle_animation_wait_cells(g, row * BATTLE_COMBATANTS + combatant, mark);
}
static uint8_t effect_mark(const GameState *g, AnimationRow row, uint8_t effect)
{
    return battle_effect_command_cells(g, row * BATTLE_EFFECTS + effect);
}
static void set_effect_mark(GameState *g, AnimationRow row, uint8_t effect, uint8_t mark)
{
    set_battle_effect_command_cells(g, row * BATTLE_EFFECTS + effect, mark);
}

enum { NOT_WAITING = 0, WAITING = 1 };

static void play_combatant_animation(GameState *g, uint8_t combatant, uint8_t animation, PlayMode mode)
{
    uint16_t part = (uint16_t)(animation * FRAMES_PER_ANIMATION);
    set_battle_scheduled_frame_lists(g, combatant, (uint16_t)(battle_object_frame_lists(g, combatant) + part));
    set_battle_scheduled_delay_lists(g, combatant, (uint16_t)(battle_object_delay_lists(g, combatant) + part));
    set_battle_animating(g, combatant, mode);
    set_battle_animation_delay(g, combatant, 0);
    set_combatant_mark(g, FRAME_ROW, combatant, BEFORE_THE_FIRST_FRAME);
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
    set_effect_mark(g, FRAME_ROW, effect, BEFORE_THE_FIRST_FRAME);
    set_battle_effect_facing_shown(g, effect, TAKE_AFRESH);
}

/* What starting a combatant's animation leaves in the work cells: the
 * combatant and its lists' step to the animation. */
static void leave_combatant_played(GameState *g, uint8_t combatant, uint8_t animation)
{
    script_leave(g, SCRIPT_CELL_COMBATANT, combatant);
    script_leave_word(g, SCRIPT_CELL_PART, (uint16_t)(animation * FRAMES_PER_ANIMATION));
}

// $C1:4D08
/* Start the animation on what the channel animates. The animation's number
 * stays in the first work cell. */
static void play(GameState *g, ScriptChannel ch, uint8_t animation, PlayMode mode)
{
    set_battle_work(g, SCRIPT_CELL_OPERAND, animation);
    ORIGINAL_EFFECT(script_leave(g, SCRIPT_CELL_MODE, mode));
    if (ch.kind == SCRIPT_EFFECT) {
        play_effect_animation(g, script_effect(g), animation, mode);
        return;
    }
    uint8_t combatant = 0;
    unsigned k;
    for (k = 0; script_each_combatant(g, ch, k, &combatant); k++)
        play_combatant_animation(g, combatant, animation, mode);
    if (k != 0)
        ORIGINAL_EFFECT(leave_combatant_played(g, combatant, animation));
    if (ch.kind == SCRIPT_OTHER_TARGETS)
        ORIGINAL_EFFECT(script_leave_word(g, SCRIPT_CELL_COUNT, (uint8_t)k));
}

static bool combatants_animating(const GameState *g, ScriptChannel ch)
{
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, ch, k, &combatant); k++)
        if (battle_animating(g, combatant))
            return true;
    return false;
}

/* The combatant whose mark a wait keeps: the channel's own, or the first of
 * the other targets. */
static uint8_t marked_combatant(const GameState *g, ScriptChannel ch)
{
    return ch.kind == SCRIPT_OTHER_TARGETS ? battle_script_subjects(g, OTHER_TARGETS_START) : script_combatant(g, ch);
}

// $C1:4D04
/* The animation plays over and over, and the channel goes on at once. */
ScriptStep script_play_animation(GameState *g, ScriptChannel ch, uint8_t mode)
{
    play(g, ch, script_operand(g, 1), (PlayMode)mode);
    return script_next(2);
}

// $C1:4E22
/* The animation plays once and the channel waits for it to end: on the
 * other targets, for every one of them to end it (their mark, on the first,
 * is left set). An effect is shown while its animation plays, and hidden
 * after. */
ScriptStep script_play_shown_and_wait(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)form;
    if (ch.kind == SCRIPT_EFFECT) {
        uint8_t effect = script_effect(g);
        if (effect_mark(g, SHOWN_WAIT_ROW, effect) == NOT_WAITING) {
            set_effect_mark(g, SHOWN_WAIT_ROW, effect, WAITING);
            play(g, ch, script_operand(g, 1), PLAY_ONCE);
            set_battle_effect_shown(g, effect, (uint8_t)(battle_effect_shown(g, effect) + 1));
            return script_stays();
        }
        if (battle_effect_animating(g, effect))
            return script_stays();
        set_effect_mark(g, SHOWN_WAIT_ROW, effect, NOT_WAITING);
        set_battle_effect_shown(g, effect, 0);
        return script_next_frame(2);
    }
    uint8_t marked = marked_combatant(g, ch);
    if (combatant_mark(g, SHOWN_WAIT_ROW, marked) == NOT_WAITING) {
        set_combatant_mark(g, SHOWN_WAIT_ROW, marked, WAITING);
        play(g, ch, script_operand(g, 1), PLAY_ONCE);
        return script_stays();
    }
    if (combatants_animating(g, ch))
        return script_stays();
    if (ch.kind == SCRIPT_ONE_COMBATANT)
        set_combatant_mark(g, SHOWN_WAIT_ROW, marked, NOT_WAITING);
    return script_next_frame(2);
}

// $C1:4EA2
/* The animation plays once and the channel waits for it to end: on the
 * other targets, for the first of them only. */
ScriptStep script_play_and_wait(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)form;
    if (ch.kind == SCRIPT_EFFECT) {
        uint8_t effect = script_effect(g);
        if (effect_mark(g, WAIT_ROW, effect) == NOT_WAITING) {
            set_effect_mark(g, WAIT_ROW, effect, WAITING);
            play(g, ch, script_operand(g, 1), PLAY_ONCE);
            return script_stays();
        }
        if (battle_effect_animating(g, effect))
            return script_stays();
        set_effect_mark(g, WAIT_ROW, effect, NOT_WAITING);
        return script_next_frame(2);
    }
    uint8_t marked = marked_combatant(g, ch);
    if (combatant_mark(g, WAIT_ROW, marked) == NOT_WAITING) {
        set_combatant_mark(g, WAIT_ROW, marked, WAITING);
        play(g, ch, script_operand(g, 1), PLAY_ONCE);
        return script_stays();
    }
    if (battle_animating(g, marked))
        return script_stays();
    set_combatant_mark(g, WAIT_ROW, marked, NOT_WAITING);
    return script_next_frame(2);
}

// $C1:577D
/* As script_play_and_wait, with the mark the second operand (a mark of 0
 * starts the animation again every frame), and on the other targets waiting
 * for every one of them, leaving the mark set. On an effect channel the
 * original plays the animation over and over instead, and goes on at once
 * from the second operand, as if it were the next command (no script does
 * this). */
enum { MARKED_LENGTH = 3 };

ScriptStep script_play_and_wait_marked(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)form;
    ORIGINAL_EFFECT(script_leave_word(g, SCRIPT_CELL_PART, 0));
    uint8_t mark = script_operand(g, 2);
    set_battle_work(g, SCRIPT_CELL_OPERAND, mark);
    if (ch.kind == SCRIPT_EFFECT) {
        uint8_t effect = script_effect(g);
        if (effect_mark(g, WAIT_ROW, effect) == NOT_WAITING) {
            set_effect_mark(g, WAIT_ROW, effect, mark);
            play(g, ch, script_operand(g, 1), PLAY_LOOPING);
            return script_next(2);
        }
        if (battle_effect_animating(g, effect))
            return script_stays();
        set_effect_mark(g, WAIT_ROW, effect, NOT_WAITING);
        return script_next_frame(MARKED_LENGTH);
    }
    uint8_t marked = marked_combatant(g, ch);
    if (combatant_mark(g, WAIT_ROW, marked) == NOT_WAITING) {
        set_combatant_mark(g, WAIT_ROW, marked, mark);
        play(g, ch, script_operand(g, 1), PLAY_ONCE);
        return script_stays();
    }
    if (combatants_animating(g, ch))
        return script_stays();
    if (ch.kind == SCRIPT_ONE_COMBATANT)
        set_combatant_mark(g, WAIT_ROW, marked, NOT_WAITING);
    return script_next_frame(MARKED_LENGTH);
}

/* ---- Speed ----------------------------------------------------------------------------- */

/* A mover's speed: a step every so many frames, of so many steps. The
 * anchors' movers follow the combatants'. */
static void set_mover_speed(GameState *g, unsigned mover, uint8_t period, uint8_t steps)
{
    set_battle_effect_period(g, mover, period);
    set_battle_effect_steps(g, mover, steps);
}
static unsigned anchor_mover(uint8_t effect) { return BATTLE_COMBATANTS + effect; }

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
ScriptStep script_own_speed(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)form;
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, ch, k, &combatant); k++)
        set_mover_speed(g, combatant, own_period(g, combatant), own_steps(g, combatant));
    return script_next_frame(1);
}

// $C1:4F54
/* The combatants, or the effect's anchor, move at one of the eight speeds.
 * On the other targets the original counts them in the first work cells. */
ScriptStep script_speed(GameState *g, ScriptChannel ch, uint8_t speed)
{
    uint8_t period = battle_script_speed_period(g, speed), steps = battle_script_speed_steps(g, speed);
    if (ch.kind == SCRIPT_EFFECT) {
        set_mover_speed(g, anchor_mover(script_effect(g)), period, steps);
        return script_next_frame(1);
    }
    uint8_t combatant;
    unsigned k;
    for (k = 0; script_each_combatant(g, ch, k, &combatant); k++)
        set_mover_speed(g, combatant, period, steps);
    if (ch.kind == SCRIPT_OTHER_TARGETS)
        script_leave_word(g, SCRIPT_CELL_OPERAND, (uint8_t)k);
    return script_next_frame(1);
}

/* ---- How the combatants are drawn ------------------------------------------------------ */

/* The flags a combatant's drawing keeps in bits 0-6, beside bit 7, which
 * sets it apart from the battlefield's plane: out of it (hidden's), or above
 * it (drawn on top's), where the targeting and the collisions pass it over. */
enum { OFF_THE_PLANE = 0x80, DRAWING_FLAGS = 0x7F };

// $C1:5806, $C1:5840
/* The combatants get, or lose, a topmost entry in the draw list as well. */
ScriptStep script_drawn_on_top(GameState *g, ScriptChannel ch, uint8_t on)
{
    ORIGINAL_EFFECT(script_leave(g, SCRIPT_CELL_MODE, on));
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, ch, k, &combatant); k++)
        set_battle_object_extra_draw_on(g, combatant, (uint8_t)((battle_object_extra_draw_on(g, combatant) & OFF_THE_PLANE) | on));
    return script_next_frame(1);
}

// $C1:5844, $C1:5879
/* The combatants are hidden, or shown. A channel's own combatant is also put
 * back on the battlefield's plane (the original sets the whole byte). */
ScriptStep script_hidden(GameState *g, ScriptChannel ch, uint8_t on)
{
    ORIGINAL_EFFECT(script_leave(g, SCRIPT_CELL_MODE, on));
    if (ch.kind == SCRIPT_ONE_COMBATANT) {
        set_battle_object_hidden(g, script_combatant(g, ch), on);
        return script_next_frame(1);
    }
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, ch, k, &combatant); k++)
        set_battle_object_hidden(g, combatant, (uint8_t)((battle_object_hidden(g, combatant) & OFF_THE_PLANE) | on));
    return script_next_frame(1);
}

// $C1:5CF1, $C1:5D12
/* The channel's combatant leaves the battlefield's plane, or comes back to
 * it, keeping its drawing flags; only on channels 0-3. */
ScriptStep script_withdrawn(GameState *g, ScriptChannel ch, uint8_t on)
{
    uint8_t plane = on ? OFF_THE_PLANE : 0;
    ORIGINAL_EFFECT(script_leave(g, SCRIPT_CELL_MODE, plane));
    if (ch.channel < OTHER_TARGETS_CHANNEL) {
        uint8_t combatant = script_combatant(g, ch);
        set_battle_object_hidden(g, combatant, (uint8_t)((battle_object_hidden(g, combatant) & DRAWING_FLAGS) | plane));
    }
    return script_next_frame(1);
}

// $C1:587D
/* The combatants' sprites are drawn where they stand again. */
ScriptStep script_sprite_back_in_place(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)form;
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, ch, k, &combatant); k++)
        set_combatant_sprite_offset(g, combatant, 0, 0);
    return script_next_frame(1);
}

// $C1:58AB
/* The combatants are back in the battle, no longer down; the original counts
 * their presence up rather than setting it. The channel goes on at once. */
ScriptStep script_bring_back(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)form;
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, ch, k, &combatant); k++) {
        set_battle_slot_present(g, combatant, (uint8_t)(battle_slot_present(g, combatant) + 1));
        set_battle_combatant_down(g, combatant, 0);
    }
    return script_next(1);
}

// $C1:6F9A, $C1:6FEF
/* The combatants' sprites glide up or down a step a frame until one of them
 * is at the height asked (with every one before it in the list stepped this
 * frame); a step that passes over the height glides on for good. The first
 * form names the height, the second glides back to where they stand. The
 * height and the step stay in the first work cells. */
ScriptStep script_glide_sprite(GameState *g, ScriptChannel ch, uint8_t form)
{
    GlideForm glide = (GlideForm)form;
    uint8_t height = 0, step, length;
    if (glide == GLIDE_TO_HEIGHT) {
        height = script_operand(g, 1);
        set_battle_work(g, SCRIPT_CELL_OPERAND, height);
        step = script_operand(g, 2);
        set_battle_work(g, SCRIPT_CELL_OPERAND_2, step);
        length = 3;
    } else {
        step = script_operand(g, 1);
        set_battle_work(g, SCRIPT_CELL_OPERAND_2, step);
        set_battle_work(g, SCRIPT_CELL_OPERAND, height);
        length = 2;
    }
    ORIGINAL_EFFECT(script_leave(g, SCRIPT_CELL_MODE, length));
    if (ch.kind == SCRIPT_EFFECT)
        return script_next_frame(length);
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, ch, k, &combatant); k++) {
        uint8_t y = battle_sprite_offset_y(g, combatant);
        if (y == height)
            return script_next_frame(length);
        set_combatant_sprite_offset_y(g, combatant, (uint8_t)(y + step));
    }
    return script_stays();
}

// $C1:6FFC, $C1:702B
/* The combatants get a shadow (counted up, as the original does), or lose
 * it. On the other targets the original starts the list at the second
 * channel's subject, so the subjects of channels 1-3 get or lose theirs too. */
enum { SHADOW_LIST_START = 1, SHADOW_OFF = 0 };

ScriptStep script_shadow(GameState *g, ScriptChannel ch, uint8_t on)
{
    uint8_t combatant;
    for (unsigned k = 0; script_combatant_from(g, ch, SHADOW_LIST_START, k, &combatant); k++)
        set_battle_object_shadow(g, combatant, on ? (uint8_t)(battle_object_shadow(g, combatant) + 1) : SHADOW_OFF);
    return script_next_frame(1);
}

// $C1:753A
/* The combatants', or the effect's, sprites are drawn with the attribute
 * bits the operand gives. */
ScriptStep script_attribute_bits(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)form;
    uint8_t bits = script_operand(g, 1);
    set_battle_work(g, SCRIPT_CELL_OPERAND, bits);
    if (ch.kind == SCRIPT_EFFECT) {
        set_battle_effect_attribute_bits(g, script_effect(g), bits);
        return script_next_frame(2);
    }
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, ch, k, &combatant); k++)
        set_battle_object_attribute_bits(g, combatant, bits);
    return script_next_frame(2);
}

// $C1:7579
/* The attribute bits go back to what they started as. The original reads
 * the byte after the command into the first work cell, and never uses it. */
ScriptStep script_attribute_bits_back(GameState *g, ScriptChannel ch, uint8_t form)
{
    (void)form;
    set_battle_work(g, SCRIPT_CELL_OPERAND, script_operand(g, 1));
    if (ch.kind == SCRIPT_EFFECT) {
        uint8_t effect = script_effect(g);
        set_battle_effect_attribute_bits(g, effect, battle_effect_attribute_bits_start(g, effect));
        return script_next_frame(1);
    }
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, ch, k, &combatant); k++)
        set_battle_object_attribute_bits(g, combatant, battle_attribute_bits_start(g, combatant));
    return script_next_frame(1);
}

/* ---- Trails ---------------------------------------------------------------------------- */

enum { TRAIL_POINTS = 4, TRAIL_ON = 3, TRAIL_OFF = 0, TRAIL_DUE_NOW = 1 };

/* The combatant starts, or stops, recording a trail every so many frames,
 * and drawing its afterimages every so many, from where it stands now. */
static void set_trail(GameState *g, uint8_t combatant, uint8_t on, uint8_t trail_period, uint8_t afterimage_period)
{
    set_battle_trail_period(g, combatant, trail_period);
    set_battle_afterimage_period(g, combatant, afterimage_period);
    set_battle_trail_countdown(g, combatant, TRAIL_DUE_NOW);
    set_battle_afterimage_countdown(g, combatant, TRAIL_DUE_NOW);
    for (unsigned point = 0; point < TRAIL_POINTS; point++)
        set_battle_trail_low_x_byte(g, point, combatant, combatant_x(g, combatant));
    for (unsigned point = 0; point < TRAIL_POINTS; point++)
        set_battle_trail_low_y_byte(g, point, combatant, combatant_y(g, combatant));
    set_battle_trail_on(g, combatant, on);
}

// $C1:691B, $C1:69BA
/* The combatants leave a trail and afterimages, at the periods the operands
 * give, which stay in the first work cells, or stop. Stopping runs the same
 * body without operands, so it sets the periods from whatever those cells
 * hold; nothing reads a trail's periods while it is off, and starting it
 * again sets them. Channels 5-7 act on the other targets, and an effect
 * channel on nothing. */
ScriptStep script_trail(GameState *g, ScriptChannel ch, uint8_t on)
{
    uint8_t length = on ? 3 : 1;
    if (on) {
        set_battle_work(g, SCRIPT_CELL_OPERAND, script_operand(g, 1));
        set_battle_work(g, SCRIPT_CELL_OPERAND_2, script_operand(g, 2));
    }
    uint8_t trail_period = battle_work(g, SCRIPT_CELL_OPERAND), afterimage_period = battle_work(g, SCRIPT_CELL_OPERAND_2);
    uint8_t state = on ? TRAIL_ON : TRAIL_OFF;
    ORIGINAL_EFFECT((script_leave(g, SCRIPT_CELL_MODE, length), script_leave(g, SCRIPT_CELL_MODE_2, state)));
    ScriptChannel trailed = ch;
    if (ch.kind == SCRIPT_ONE_COMBATANT && ch.channel > OTHER_TARGETS_CHANNEL)
        trailed.kind = SCRIPT_OTHER_TARGETS;
    uint8_t combatant;
    for (unsigned k = 0; script_each_combatant(g, trailed, k, &combatant); k++)
        set_trail(g, combatant, state, trail_period, afterimage_period);
    return script_next_frame(length);
}
