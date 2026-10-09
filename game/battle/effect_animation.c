/* Battle: the effects' animations (battle/effects.h).
 *
 * An effect shows its animation a frame at a time: each frame is a sprite
 * set, drawn at an offset from the effect's anchor, for a number of frames of
 * time. Once a frame the battle counts every animating effect's frame time
 * down; an effect whose time has run out moves on to its next frame, read
 * from the effect's animation (its sprite set, offset and time), or, at the
 * animation's end, starts it over or stops it.
 *
 * An animation is a run of frames for each way the anchor can face, one run
 * after another, each as long as the animation (battle/script_poses.c starts
 * it); the frame shown is the one of the run for the way the anchor faces
 * now, so an effect that follows a chase turns with it. */
#include "battle/effects.h"

#include "battle/animation.h"
#include "battle/animation_data.h"
#include "battle/arith.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/script_language.h"
#include "charge.h"
#include "original_effects.h"
#include "vars.h"

static bool effect_animating(const GameState *g, unsigned effect) { return battle_effect_animating(g, effect) != 0; }

/* Count each animating effect's frame time down a frame, to 0. */
static void count_frame_times_down(GameState *g)
{
    for (unsigned effect = 0; effect < BATTLE_EFFECTS; effect++) {
        uint8_t left = battle_effect_frame_time(g, effect);
        if (effect_animating(g, effect) && left != 0)
            set_battle_effect_frame_time(g, effect, (uint8_t)(left - 1));
    }
}

static bool frame_is_up(const GameState *g, unsigned effect)
{
    return effect_animating(g, effect) && battle_effect_frame_time(g, effect) == 0;
}

/* ---- An effect's next frame --------------------------------------------------------------- */

/* Starting an animation puts its frame before the first, and stepping from
 * there comes round to the first. */
enum { BEFORE_THE_FIRST_FRAME = 0xFF };

/* The sprite set the effect draws: the frame's five low bits and the sixth
 * from its time, and a mark once its animation has ended (the drawing takes
 * the six bits). */
enum { SET_LOW_BITS = 0x1F, SET_HIGH_BIT = 0x20, SET_ANIMATION_ENDED = 0x80 };

static uint8_t sprite_set_of(BattleEffectFrame frame, bool ended)
{
    return (uint8_t)((frame.marks & SET_LOW_BITS) | (frame.high_set ? SET_HIGH_BIT : 0) | (ended ? SET_ANIMATION_ENDED : 0));
}

/* What the original leaves in its cells of the last frame it took: the
 * facing's run's place, the effect doubled, and the frame's end and high
 * bit. */
enum { LEFT_FACING_RUN = 0x02, LEFT_EFFECT_WORD = 0x04, LEFT_ENDED = 0x0E, LEFT_HIGH_SET = 0x0F };

typedef struct {
    uint16_t facing_run;
    uint8_t effect;
    bool ended, high_set;
} TakenFrame;

static void leave_taken_frame(GameState *g, TakenFrame t)
{
    set_battle_work(g, LEFT_FACING_RUN, (uint8_t)t.facing_run);
    set_battle_work(g, LEFT_FACING_RUN + 1, (uint8_t)(t.facing_run >> 8));
    set_battle_work(g, LEFT_EFFECT_WORD, (uint8_t)(t.effect * 2));
    set_battle_work(g, LEFT_EFFECT_WORD + 1, 0);
    set_battle_work(g, LEFT_ENDED, t.ended ? SET_ANIMATION_ENDED : 0);
    set_battle_work(g, LEFT_HIGH_SET, t.high_set);
}

// $CC:F2A9
/* The effect moves on to its animation's next frame: the sprite set it
 * draws, where it draws it from the anchor and for how long. Coming round to
 * the first frame stops an effect its script waits on. A frame that ends the
 * animation starts a looping one over at the next step, and stops one that
 * plays once, which then draws nothing more.
 *
 * The original compares the anchor's facing with the one the frames were
 * last taken for and then stores it back where it read it, so the facing
 * shown is never brought up to date here; the run taken is the anchor's
 * facing's either way. It finds the run with the multiplier, the run's length
 * in records as a byte, and the frame's record in a 16-bit offset in bank
 * $CE, as kept here. */
static TakenFrame take_next_frame(GameState *g, unsigned effect)
{
    uint8_t facing = battle_anchor_facing(g, effect);
    uint8_t run_bytes = (uint8_t)(battle_effect_animation_length(g, effect) * BATTLE_EFFECT_FRAME_BYTES);
    uint16_t facing_run = battle_multiply(g, facing, run_bytes);

    uint8_t number = (uint8_t)(battle_effect_frame(g, effect) + 1);
    if (number == 0 && battle_effect_wait(g, effect))
        set_battle_effect_animating(g, effect, 0);
    set_battle_effect_frame(g, effect, number);

    uint16_t at = (uint16_t)(number * BATTLE_EFFECT_FRAME_BYTES + facing_run + battle_effect_animation_place(g, effect));
    BattleEffectFrame frame = battle_effect_frame_record(g, at);
    set_battle_effect_frame_time(g, effect, frame.time);
    set_battle_anchor_offset_x(g, effect, (uint16_t)frame.x);
    set_battle_anchor_offset_y(g, effect, (uint16_t)frame.y);

    bool ended = false;
    if (frame.marks & BATTLE_EFFECT_FRAME_ENDS) {
        if (battle_effect_animating(g, effect) == PLAY_LOOPING) {
            set_battle_effect_frame(g, effect, BEFORE_THE_FIRST_FRAME);
        } else {
            set_battle_effect_animating(g, effect, 0);
            ended = true;
        }
    }
    set_battle_effect_sprite_set(g, effect, sprite_set_of(frame, ended));
    return (TakenFrame){ facing_run, (uint8_t)effect, ended, frame.high_set };
}

/* ---- The entries ------------------------------------------------------------------------- */

/* The effect stepping, which the original keeps in the battle's first work
 * cells (a word) as it goes round them. */
static void leave_stepping(GameState *g, uint16_t effect)
{
    set_battle_work(g, 0, (uint8_t)effect);
    set_battle_work(g, 1, (uint8_t)(effect >> 8));
}

// $CC:F278
/* Step the effects' animations a frame: count their frame times down, then
 * move each effect whose time is up on to its next frame. */
void battle_animate_effect_frames(GameState *g)
{
    count_frame_times_down(g);
    TakenFrame last = { 0 };
    bool taken = false;
    for (unsigned effect = 0; effect < BATTLE_EFFECTS; effect++) {
        if (!frame_is_up(g, effect))
            continue;
        TIMED_AS(g, 0xCCF2A9, last = take_next_frame(g, effect));
        taken = true;
    }
    if (taken)
        ORIGINAL_EFFECT(leave_taken_frame(g, last));
    ORIGINAL_EFFECT(leave_stepping(g, BATTLE_EFFECTS));
}

void battle_animate_effects(Snes *s)
{
    battle_animate_effect_frames(state_of(s));
    m_rtl(s);
}

/* The effect comes in the first work cells, as the loop above leaves it. */
static uint16_t stepping(const GameState *g) { return (uint16_t)(battle_work(g, 0) | battle_work(g, 1) << 8); }

void battle_take_effect_frame(Snes *s)
{
    GameState *g = state_of(s);
    TakenFrame taken = take_next_frame(g, stepping(g));
    ORIGINAL_EFFECT(leave_taken_frame(g, taken));
    m_rts(s);
}

/* Only an effect, from the battle's page. */
bool battle_names_stepping_effect(Snes *s)
{
    vars_peek(true);
    bool effect = stepping(state_of(s)) < BATTLE_EFFECTS;
    vars_peek(false);
    return battle_on_page(s) && effect;
}
