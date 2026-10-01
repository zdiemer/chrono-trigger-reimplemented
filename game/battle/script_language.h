/* Battle: the animation script language -- what its commands share
 * (battle/scripts.c runs the channels).
 *
 * A channel animates what it was started for, which is one of three kinds:
 *
 *   - channels 0-3 (and 5-7, which no script starts), one combatant: the
 *     channel's subject (battle_script_subjects);
 *   - channel 4, the action's other targets: the subjects from the fifth on,
 *     up to the first with its top bit set;
 *   - channels 8-15, an effect and its anchor: effect e = channel - 8
 *     (battle_script_effect), which moves as mover 11 + e.
 *
 * Each file of commands has a table of rows, a row a command byte: the
 * operation, how each of its parameters comes from the command's operands
 * (or is fixed by the byte), the command's length, and what the operation
 * needs of the state to run as the original does. The runner decodes the
 * command by its row into a ScriptCommand, and the operation takes the
 * parameters it names. It says how the channel goes on (ScriptFlow): past the
 * command, or staying on it to run it again next frame (a wait); and whether
 * the channel carries on with its next command in this frame. */
#ifndef CT_GAME_BATTLE_SCRIPT_LANGUAGE_H
#define CT_GAME_BATTLE_SCRIPT_LANGUAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

enum { SCRIPT_CHANNELS = 16, FIRST_EFFECT_CHANNEL = 8, OTHER_TARGETS_CHANNEL = 4 };

typedef enum { SCRIPT_ONE_COMBATANT, SCRIPT_OTHER_TARGETS, SCRIPT_EFFECT } ScriptChannelKind;

/* The channel a command runs on. */
typedef struct {
    uint8_t channel;
    ScriptChannelKind kind;
} ScriptChannel;

/* The channel running now, as the channel runner leaves it for the command. */
ScriptChannel script_channel_running(const GameState *g);

/* The combatant a one-combatant channel animates; the effect an effect
 * channel does (the one whose channel runs, which the commands that act on
 * "the channel's effect" take on any channel). */
uint8_t script_combatant(const GameState *g, ScriptChannel ch);
uint8_t script_effect(const GameState *g);

/* The other-targets list: the subjects from `first` on, up to the first with
 * its top bit set. The list's own start is OTHER_TARGETS_START; the shadows
 * start it at channel 1's subject and the color rotation at channel 3's, as
 * the original does. */
enum { OTHER_TARGETS_START = 4, SHADOW_LIST_START = 1, ROTATION_LIST_START = 3, LIST_ENDS = 0x80 };
bool script_listed(const GameState *g, unsigned at, uint8_t *combatant);

/* The k-th combatant a command acts on for a combatant channel: the
 * channel's subject (k = 0 only), or on the other-targets channel the k-th
 * target listed from `first`. False past the last. */
bool script_combatant_from(const GameState *g, ScriptChannel ch, unsigned first, unsigned k, uint8_t *combatant);
static inline bool script_each_combatant(const GameState *g, ScriptChannel ch, unsigned k, uint8_t *combatant)
{
    return script_combatant_from(g, ch, OTHER_TARGETS_START, k, combatant);
}

/* The background's colors, which a rule's cycle runs to the last of. */
enum { BACKGROUND_COLORS = 128 };

/* What the original leaves in the battle's first work cells ($80 on), its
 * registers as it works: scaffolding only, written under ORIGINAL_EFFECT. */
void script_leave(GameState *g, unsigned cell, uint8_t value);
void script_leave_word(GameState *g, unsigned cell, uint16_t value);
uint8_t script_left(const GameState *g, unsigned cell);

/* ---- A script's place ------------------------------------------------------------------ */

/* Where a channel is in its script: a 24-bit address, the ROM's or work
 * RAM's, and the byte the original keeps after it, which an address carried
 * past the last bank counts up. */
typedef struct {
    uint32_t at;
    uint8_t beyond;
} ScriptPlace;

ScriptPlace script_place(const GameState *g);
void set_script_place(GameState *g, ScriptPlace place);
/* The place `length` bytes on: an offset carried out of its bank carries
 * into the bank, as the original's word adds do. */
ScriptPlace script_place_plus(ScriptPlace place, uint8_t length);

/* Operand k of the command (the command byte is operand 0), wherever the
 * script is: the ROM or work RAM. */
uint8_t script_operand(const GameState *g, unsigned k);

/* Whether the script lies over the direct page, whose cells the commands
 * write as they read their operands: the original's to run. */
bool script_over_the_direct_page(const GameState *g);

/* ---- The commands ---------------------------------------------------------------------- */

/* How the channel goes on after a command. */
typedef enum {
    SCRIPT_NEXT,       /* past the command, and on to the next this frame */
    SCRIPT_NEXT_FRAME, /* past the command; the channel's frame ends */
    SCRIPT_STAY,       /* on the command, to run it again next frame */
    SCRIPT_AGAIN,      /* on the command, run again this frame */
    SCRIPT_MOVED,      /* the command moved the channel itself (a call, a return): on from there */
} ScriptFlow;

/* The script's values: its variables, named by an operand, its two marks,
 * and its point. */
typedef enum { SCRIPT_VARIABLE, SCRIPT_MARK_A, SCRIPT_MARK_B, SCRIPT_POINT_X, SCRIPT_POINT_Y } ScriptValue;

/* How an animation plays: once, or over and over (the value the animating
 * flags hold). */
typedef enum { PLAY_ONCE = 1, PLAY_LOOPING = 2 } PlayMode;

/* Which side of a combatant an effect is drawn on (its record's bit). */
typedef enum { DRAWN_IN_FRONT = 0x80, DRAWN_BEHIND = 0x40 } EffectSide;

/* Every parameter of the language: a row says which operands give the ones
 * its operation takes. */
#define SCRIPT_PARAMETERS(X)                                                                       \
    /* flow and values */                                                                          \
    X(uint8_t, subroutine) X(uint8_t, frames) X(uint8_t, place) X(ScriptValue, which)             \
    X(uint8_t, variable) X(uint8_t, other) X(uint8_t, value) X(uint8_t, amount)                    \
    /* poses */                                                                                    \
    X(uint8_t, animation) X(PlayMode, mode) X(uint8_t, mark) X(uint8_t, speed) X(bool, on)        \
    X(uint8_t, height) X(uint8_t, step) X(uint8_t, bits) X(uint8_t, trail_period)                 \
    X(uint8_t, afterimage_period)                                                                  \
    /* effects and colors */                                                                       \
    X(uint8_t, named) X(uint8_t, anchor) X(EffectSide, side) X(uint8_t, set) X(uint8_t, period)   \
    X(uint8_t, frame) X(uint8_t, slot) X(uint8_t, first) X(uint8_t, last)

/* A command, decoded by its row: the channel it runs on, its length, and the
 * parameters of the language, of which an operation takes the ones it names. */
typedef struct {
    ScriptChannel channel;
    uint8_t length;
#define SCRIPT_PARAMETER_FIELD(type, name) type name;
    SCRIPT_PARAMETERS(SCRIPT_PARAMETER_FIELD)
#undef SCRIPT_PARAMETER_FIELD
} ScriptCommand;

typedef ScriptFlow (*ScriptOperation)(GameState *g, const ScriptCommand *command);

/* SCRIPT_OPERATION(op, parameter...): the form of the operation `op` the
 * table calls, which hands it the parameters it names, in that order, after
 * the game's state (`channel` and `length` among them). OPERATION(op) names
 * it in a row. */
#define SCRIPT_TAKE_(name) command->name
#define SCRIPT_TAKE_1(a) , SCRIPT_TAKE_(a)
#define SCRIPT_TAKE_2(a, b) SCRIPT_TAKE_1(a), SCRIPT_TAKE_(b)
#define SCRIPT_TAKE_3(a, b, c) SCRIPT_TAKE_2(a, b), SCRIPT_TAKE_(c)
#define SCRIPT_TAKE_4(a, b, c, d) SCRIPT_TAKE_3(a, b, c), SCRIPT_TAKE_(d)
#define SCRIPT_TAKE_5(a, b, c, d, e) SCRIPT_TAKE_4(a, b, c, d), SCRIPT_TAKE_(e)
#define SCRIPT_COUNT_(_1, _2, _3, _4, _5, n, ...) n
#define SCRIPT_COUNT(...) SCRIPT_COUNT_(__VA_ARGS__, 5, 4, 3, 2, 1, 0)
#define SCRIPT_GLUE_(a, b) a##b
#define SCRIPT_GLUE(a, b) SCRIPT_GLUE_(a, b)
#define SCRIPT_OPERATION(op, ...)                                                                  \
    static ScriptFlow op##_command(GameState *g, const ScriptCommand *command)                   \
    {                                                                                              \
        return op(g SCRIPT_GLUE(SCRIPT_TAKE_, SCRIPT_COUNT(__VA_ARGS__))(__VA_ARGS__));            \
    }
#define SCRIPT_OPERATION_TAKING_NOTHING(op)                                                        \
    static ScriptFlow op##_command(GameState *g, const ScriptCommand *command)                   \
    {                                                                                              \
        (void)command;                                                                             \
        return op(g);                                                                              \
    }
#define OPERATION(op) .run = op##_command

/* How a parameter comes from the command: fixed by its byte, or an operand
 * byte (1 is the byte after the command's). */
typedef enum { ARG_NONE, ARG_CONSTANT, ARG_OPERAND } ScriptArgKind;
typedef struct {
    ScriptArgKind kind;
    uint8_t operand_or_constant;
} ScriptArg;
#define CONSTANT(k) { ARG_CONSTANT, (k) }
#define OPERAND(n) { ARG_OPERAND, (n) }

/* What an operation needs of the state to run as the original does: what its
 * channel animates being a combatant or an effect it can index by (and for
 * one, a combatant's channel); the channel's effect being one, on any
 * channel; for the colors, the lists and colors they reach in range too.
 * Where it isn't, the original's handler runs, whose indexed writes reach past
 * the tables (an ending's script runs on a channel whose subject is none). */
typedef enum { NEEDS_NOTHING, NEEDS_SUBJECTS, NEEDS_A_COMBATANT_CHANNEL, NEEDS_EFFECT, NEEDS_COLORS } ScriptNeeds;

typedef struct {
    uint8_t command;
    ScriptOperation run;
    struct {
#define SCRIPT_PARAMETER_SPEC(type, name) ScriptArg name;
        SCRIPT_PARAMETERS(SCRIPT_PARAMETER_SPEC)
#undef SCRIPT_PARAMETER_SPEC
    };
    uint8_t length; /* the command's bytes, its own included */
    ScriptNeeds needs;
} ScriptOpcode;

typedef struct {
    const ScriptOpcode *rows;
    unsigned count;
} ScriptOpcodes;

#define SCRIPT_OPCODES(name, ...)                                \
    static const ScriptOpcode name##_rows[] = { __VA_ARGS__ };   \
    const ScriptOpcodes name = { name##_rows, sizeof name##_rows / sizeof name##_rows[0] }

/* Every file's rows (battle/script_language.c indexes them). */
extern const ScriptOpcodes script_flow_opcodes, script_pose_opcodes, script_effect_opcodes, script_color_opcodes;

/* The row of a command byte, or NULL for one still the original's. */
const ScriptOpcode *script_opcode(uint8_t command);

/* Whether the row's operation runs in this state as the original's handler
 * would, and running it: decode, operate, and move the channel on. */
bool script_runs_here(const GameState *g, const ScriptOpcode *row);
void script_perform(GameState *g, const ScriptOpcode *row);

#endif
