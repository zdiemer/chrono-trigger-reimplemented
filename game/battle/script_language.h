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

/* Operand k of the command (the command byte is operand 0), wherever the
 * script is: the ROM or work RAM. */
uint8_t script_operand(const GameState *g, unsigned k);

/* How the channel goes on after a command. */
typedef struct {
    uint8_t length;    /* past the command; 0 stays on it */
    bool go_on;        /* the channel runs its next command this frame */
    bool through_tail; /* the original moved on through its commands' common advance
                          (not where it ended the channel or held without it),
                          whose flags the next command finds */
} ScriptStep;

static inline ScriptStep script_next(uint8_t length) { return (ScriptStep){ length, true, true }; }
static inline ScriptStep script_next_frame(uint8_t length) { return (ScriptStep){ length, false, true }; }
static inline ScriptStep script_stays(void) { return (ScriptStep){ 0, false, true }; }
/* Nothing to move past: a command that moved the channel itself (a call, a
 * return). */
static inline ScriptStep script_moved(bool go_on) { return (ScriptStep){ 0, go_on, true }; }
/* The channel ended, or held without moving on at all. */
static inline ScriptStep script_left_as_it_was(void) { return (ScriptStep){ 0, false, false }; }

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

#endif
