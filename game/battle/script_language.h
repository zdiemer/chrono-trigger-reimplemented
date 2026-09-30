/* Battle: the animation script language -- what its commands share
 * (battle/scripts.c runs the channels and holds the command table).
 *
 * A channel animates what it was started for, which is one of three kinds:
 *
 *   - channels 0-3 (and 5-7, which no script starts), one combatant: the
 *     channel's subject (battle_script_subjects);
 *   - channel 4, the action's other targets: the subjects from the fifth on,
 *     up to the first with its top bit set;
 *   - channels 8-15, an effect and its anchor: effect e = channel - 8
 *     (battle_script_enemy), which moves as mover 11 + e.
 *
 * A command reads its operands after its own byte, does its work on what its
 * channel animates, and says how the channel goes on (ScriptStep): past the
 * command (its length), or staying on it to run it again next frame (a wait);
 * and whether the channel carries on with its next command in this frame or
 * its frame ends there. */
#ifndef CT_GAME_BATTLE_SCRIPT_LANGUAGE_H
#define CT_GAME_BATTLE_SCRIPT_LANGUAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

enum { SCRIPT_CHANNELS = 16, FIRST_EFFECT_CHANNEL = 8, OTHER_TARGETS_CHANNEL = 4 };

typedef enum { SCRIPT_ONE_COMBATANT, SCRIPT_OTHER_TARGETS, SCRIPT_EFFECT } ScriptChannelKind;

/* The channel a command runs on, and its command byte. */
typedef struct {
    uint8_t channel;
    ScriptChannelKind kind;
    uint8_t command;
} ScriptChannel;

/* The channel running now, as the channel runner leaves it for the command. */
ScriptChannel script_channel_running(const GameState *g, uint8_t command);

/* The combatant a one-combatant channel animates; the effect an effect
 * channel does. */
uint8_t script_combatant(const GameState *g, ScriptChannel ch);
uint8_t script_effect(const GameState *g);

/* The other-targets list: the subjects from `first` on (the list's own start,
 * OTHER_TARGETS_START, or where a few commands start by mistake: their quirk),
 * up to the first with its top bit set. */
enum { OTHER_TARGETS_START = 4, LIST_ENDS = 0x80 };
bool script_listed(const GameState *g, unsigned at, uint8_t *combatant);

/* The k-th combatant a command acts on for a combatant channel: the
 * channel's subject (k = 0 only), or on the other-targets channel the k-th
 * target listed from `first`. False past the last. */
bool script_combatant_from(const GameState *g, ScriptChannel ch, unsigned first, unsigned k, uint8_t *combatant);
static inline bool script_each_combatant(const GameState *g, ScriptChannel ch, unsigned k, uint8_t *combatant)
{
    return script_combatant_from(g, ch, OTHER_TARGETS_START, k, combatant);
}

/* The battle's first work cells ($80 on), where the commands leave their
 * operands and working values as the original does. What one command leaves
 * in the first two another reads as it finds them (the trail's periods, the
 * jumps' kind of height), so those are written plainly; the rest is
 * scaffolding (script_leave, under ORIGINAL_EFFECT). */
enum {
    SCRIPT_CELL_OPERAND = 0x00,
    SCRIPT_CELL_OPERAND_2 = 0x01,
    SCRIPT_CELL_PART = 0x02,
    SCRIPT_CELL_FROM = 0x04,
    SCRIPT_CELL_LEFT = 0x06,
    SCRIPT_CELL_COMBATANT = 0x0A,
    SCRIPT_CELL_COUNT = 0x0C,
    SCRIPT_CELL_MODE = 0x0E,
    SCRIPT_CELL_MODE_2 = 0x0F,
    SCRIPT_CELL_LIST_PLACE = 0x10,
    SCRIPT_CELL_PERIOD = 0x12,
};
void script_leave(GameState *g, unsigned cell, uint8_t value);
void script_leave_word(GameState *g, unsigned cell, uint16_t value);

/* Operand k of the command (the command byte is operand 0), wherever the
 * script is: the ROM or work RAM. A script in work RAM can lie over the cells
 * a command writes, so a command reads each operand after what the original
 * writes before reading it. */
uint8_t script_operand(const GameState *g, unsigned k);

/* How the channel goes on after a command. */
typedef struct {
    uint8_t length; /* past the command; 0 stays on it */
    bool go_on;     /* the channel runs its next command this frame */
} ScriptStep;

static inline ScriptStep script_next(uint8_t length) { return (ScriptStep){ length, true }; }
static inline ScriptStep script_next_frame(uint8_t length) { return (ScriptStep){ length, false }; }
static inline ScriptStep script_stays(void) { return (ScriptStep){ 0, false }; }
/* Nothing to move past: a command that moved the channel itself (a call, a
 * return), or ended it. */
static inline ScriptStep script_moved(bool go_on) { return (ScriptStep){ 0, go_on }; }

/* The channel's place moves on past `length` bytes. */
void script_advance(GameState *g, uint8_t length);
/* A command's step, taken. */
void script_take_step(GameState *g, ScriptStep step);

/* A script command in C: its work on the channel. Commands that differ only
 * in how their operands are encoded share one operation, their table rows
 * telling it which form they are (`form`, the operation's own enum). */
typedef ScriptStep (*ScriptOperation)(GameState *g, ScriptChannel ch, uint8_t form);

/* battle/script_flow.c */
ScriptStep script_end(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_halt(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_call(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_return(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_return_unless_point_low(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_wait(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_skip(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_end_unless_listed(GameState *g, ScriptChannel ch, uint8_t form);

/* The script's values: its variables, named by an operand, its two marks,
 * and its point. */
typedef enum { SCRIPT_VARIABLE, SCRIPT_MARK_A, SCRIPT_MARK_B, SCRIPT_POINT_X, SCRIPT_POINT_Y } ScriptValue;
ScriptStep script_wait_for_value(GameState *g, ScriptChannel ch, uint8_t value);
ScriptStep script_set_value(GameState *g, ScriptChannel ch, uint8_t value);
ScriptStep script_count_up(GameState *g, ScriptChannel ch, uint8_t value);
ScriptStep script_count_down(GameState *g, ScriptChannel ch, uint8_t value);
ScriptStep script_add_to_value(GameState *g, ScriptChannel ch, uint8_t value);
ScriptStep script_add_variable_to_mark_a(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_copy_variable(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_store_mark_a(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_add_held(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_swap_with_mark_a(GameState *g, ScriptChannel ch, uint8_t value);

/* battle/script_poses.c */
typedef enum { PLAY_ONCE = 1, PLAY_LOOPING = 2 } PlayMode;
ScriptStep script_play_animation(GameState *g, ScriptChannel ch, uint8_t mode);
ScriptStep script_play_shown_and_wait(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_play_and_wait(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_play_and_wait_marked(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_own_speed(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_speed(GameState *g, ScriptChannel ch, uint8_t speed);
ScriptStep script_drawn_on_top(GameState *g, ScriptChannel ch, uint8_t on);
ScriptStep script_hidden(GameState *g, ScriptChannel ch, uint8_t on);
ScriptStep script_withdrawn(GameState *g, ScriptChannel ch, uint8_t on);
ScriptStep script_sprite_back_in_place(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_bring_back(GameState *g, ScriptChannel ch, uint8_t form);
typedef enum { GLIDE_TO_HEIGHT, GLIDE_HOME } GlideForm;
ScriptStep script_glide_sprite(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_shadow(GameState *g, ScriptChannel ch, uint8_t on);
ScriptStep script_attribute_bits(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_attribute_bits_back(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_trail(GameState *g, ScriptChannel ch, uint8_t on);

/* battle/script_effects.c */
ScriptStep script_anchor_follows(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_anchor_stays(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_all_effects_shown(GameState *g, ScriptChannel ch, uint8_t on);
ScriptStep script_effect_shown(GameState *g, ScriptChannel ch, uint8_t on);
ScriptStep script_effect_drawn_with(GameState *g, ScriptChannel ch, uint8_t side);
/* battle/script_colors.c */
ScriptStep script_flash(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_flash_off(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_blink(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_rotate_colors(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_palette_frame(GameState *g, ScriptChannel ch, uint8_t form);
ScriptStep script_palettes_paused(GameState *g, ScriptChannel ch, uint8_t on);
ScriptStep script_palette_animation(GameState *g, ScriptChannel ch, uint8_t slot);
ScriptStep script_palette_animation_off(GameState *g, ScriptChannel ch, uint8_t slot);
ScriptStep script_colour_cycles_off(GameState *g, ScriptChannel ch, uint8_t form);

#endif
