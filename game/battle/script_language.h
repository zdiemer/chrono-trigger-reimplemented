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
 * the channel carries on with its next command in this frame. A row whose
 * command still calls out to the original's code (the sound driver) has no
 * operation: it decodes the command for a bridge, which takes the machine,
 * in battle/scripts.c's table beside the dispatch. */
#ifndef CT_GAME_BATTLE_SCRIPT_LANGUAGE_H
#define CT_GAME_BATTLE_SCRIPT_LANGUAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/positions.h"
#include "state.h"
#include "vars.h"

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

/* Move the channel on `length` bytes, for a command that goes on by a length
 * of its own (SCRIPT_MOVED_NEXT_FRAME). */
void script_go_past(GameState *g, uint8_t length);

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
    SCRIPT_MOVED_NEXT_FRAME, /* the command moved the channel itself; the channel's frame ends */
} ScriptFlow;

/* Where a command sends what it moves: to coordinates its operands give, to
 * the script's point, or to a place on the battlefield it names (battle/
 * script_places.c). */
typedef enum { AIM_AT_COORDINATES, AIM_AT_THE_POINT, AIM_AT_A_PLACE } ScriptAim;

/* The script's values: its variables, named by an operand, its two marks,
 * and its point. */
typedef enum { SCRIPT_VARIABLE, SCRIPT_MARK_A, SCRIPT_MARK_B, SCRIPT_POINT_X, SCRIPT_POINT_Y } ScriptValue;

/* How an animation plays: once, or over and over (the value the animating
 * flags hold). */
typedef enum { PLAY_ONCE = 1, PLAY_LOOPING = 2 } PlayMode;

/* Which side of a combatant an effect is drawn on (its record's bit). */
typedef enum { DRAWN_IN_FRONT = 0x80, DRAWN_BEHIND = 0x40 } EffectSide;

/* A throw's wait before its mover moves: the one before its rise, the one
 * after it, or none. */
typedef enum { NO_WAIT, WAIT_BEFORE, WAIT_AFTER } ThrowWait;

/* Whether a throw's wait of that kind is over (battle/script_moves.c). */
bool script_throw_wait_over(const GameState *g, ThrowWait wait);

/* Whether the channel ends for nobody being at the place `listed` in the
 * other targets' list (battle/script_flow.c). */
bool script_ends_unless_listed(const GameState *g, uint8_t listed);

/* Every parameter of the language: a row says which operands give the ones
 * its operation takes. */
#define SCRIPT_PARAMETERS(X)                                                                       \
    /* flow and values */                                                                          \
    X(uint8_t, subroutine) X(uint8_t, frames) X(uint8_t, listed) X(ScriptValue, which)            \
    X(uint8_t, variable) X(uint8_t, other) X(uint8_t, value) X(uint8_t, amount)                    \
    /* places, facings and headings */                                                             \
    X(ScriptAim, aim) X(uint8_t, x) X(uint8_t, y) X(uint8_t, place) X(uint8_t, toward)             \
    X(uint8_t, followed) X(uint8_t, facing) X(uint8_t, heading) X(uint8_t, steps)                  \
    /* moves */                                                                                    \
    X(uint8_t, lift) X(bool, downward) X(bool, short_hop)                                          \
    X(bool, stops_at_others) X(uint8_t, circle) X(uint8_t, start) X(uint8_t, quarter_turns)        \
    X(bool, flat) X(uint8_t, radius) X(ThrowWait, wait)                                            \
    /* sound and the screen */                                                                     \
    X(uint8_t, sound) X(uint8_t, voices) X(bool, hit) X(uint8_t, settings)                         \
    /* poses */                                                                                    \
    X(uint8_t, animation) X(PlayMode, mode) X(uint8_t, mark) X(uint8_t, speed) X(bool, on)        \
    X(uint8_t, height) X(uint8_t, step) X(uint8_t, bits) X(uint8_t, trail_period)                 \
    X(uint8_t, afterimage_period)                                                                  \
    /* effects and colors */                                                                       \
    X(uint8_t, named) X(uint8_t, anchor) X(EffectSide, side) X(uint8_t, set) X(uint8_t, period)   \
    X(uint8_t, frame) X(uint8_t, slot) X(uint8_t, first) X(uint8_t, last)

/* A command, decoded by its row: the channel it runs on, its length, and the
 * parameters of the language, of which an operation takes the ones it names. */
typedef struct ScriptCommand {
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
#define SCRIPT_TAKE_6(a, b, c, d, e, f) SCRIPT_TAKE_5(a, b, c, d, e), SCRIPT_TAKE_(f)
#define SCRIPT_TAKE_7(a, b, c, d, e, f, h) SCRIPT_TAKE_6(a, b, c, d, e, f), SCRIPT_TAKE_(h)
#define SCRIPT_TAKE_8(a, b, c, d, e, f, h, i) SCRIPT_TAKE_7(a, b, c, d, e, f, h), SCRIPT_TAKE_(i)
#define SCRIPT_TAKE_9(a, b, c, d, e, f, h, i, j) SCRIPT_TAKE_8(a, b, c, d, e, f, h, i), SCRIPT_TAKE_(j)
#define SCRIPT_COUNT_(_1, _2, _3, _4, _5, _6, _7, _8, _9, n, ...) n
#define SCRIPT_COUNT(...) SCRIPT_COUNT_(__VA_ARGS__, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
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
 * the tables, but for the waits on an animation, which take a combatant
 * channel whose subject is nobody too (an ending's script runs on a channel
 * for a partner the party doesn't have; script_poses.c). The jumps to the
 * script's point need the usual lift besides (below). */
typedef enum {
    NEEDS_NOTHING, NEEDS_SUBJECTS, NEEDS_A_COMBATANT_CHANNEL, NEEDS_ONE_MOVER, NEEDS_EFFECT, NEEDS_COLORS,
    NEEDS_A_SCREEN_EFFECT, NEEDS_SUBJECTS_AND_A_QUIET_PLACE, NEEDS_SUBJECTS_AND_THE_USUAL_LIFT,
    NEEDS_SUBJECTS_OR_NOBODY, NEEDS_A_PLACE_APART_FROM_THE_SOUND
} ScriptNeeds;

/* The subject of a combatant channel for no one: an empty place of the
 * action's (a partner the party doesn't have). */
enum { SCRIPT_NOBODY = 0xFF };

/* The lift a jump to the script's point takes: once, at full bounce. The
 * original takes it from a work cell it shares with other work ($90), which
 * the animation slots' frame leaves at their count, 6, and which a jump
 * given its own lift, or a walk's search, overwrites until the next frame;
 * the rows lift by the 6, and leave a jump to the original while the cell
 * holds anything else (battle/scripts.c looks). */
enum { POINT_JUMP_LIFT = 6 };
bool script_point_jump_lift_as_usual(const GameState *g);

typedef struct {
    uint8_t command;
    ScriptOperation run; /* none for a bridge's row */
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
extern const ScriptOpcodes script_flow_opcodes, script_pose_opcodes, script_effect_opcodes, script_color_opcodes,
    script_place_opcodes, script_move_opcodes, script_walk_opcodes, script_sound_opcodes, script_number_opcodes;

/* The row of a command byte, or NULL for one still the original's. */
const ScriptOpcode *script_opcode(uint8_t command);

/* Whether the row's operation runs in this state as the original's handler
 * would; decoding its command; running its operation (a row that has one);
 * and moving the channel on as the operation or the bridge says. */
bool script_runs_here(const GameState *g, const ScriptOpcode *row);
ScriptCommand script_decode(const GameState *g, const ScriptOpcode *row);
ScriptFlow script_operate(GameState *g, const ScriptOpcode *row);
void script_move_on(GameState *g, const ScriptOpcode *row, ScriptFlow flow);

/* ---- Places and facings (battle/script_places.c) ----------------------------------------- */

/* The places a command can name, 0-$47 (and their kinds again from $80,
 * script_places.c), and the facings, 0-$18: the original's tables end there,
 * and a command naming one past them is the original's to run. */
enum { LAST_PLACE = 0x47, LAST_FACING = 0x18 };

/* $C1:762E: the point on the battlefield a place names, worked out now. The
 * battle keeps it as the place last found, which one of the places names. */
BattlePoint script_find_place(GameState *g, uint8_t place);

/* Whether working the place out uses the work cells the original's commands
 * keep their operands in: a command that reads its operands back from them
 * after finding such a place takes what the place left (the chases' heading,
 * an orbit's circle), so those commands leave such places to the original
 * (NEEDS_SUBJECTS_AND_A_QUIET_PLACE). */
bool script_place_works_in_cells(uint8_t place);

/* Whether working the place out reads the sound driver's command, which a
 * sound played from a place has written its sound into first: a subject far
 * past the combatants, whose place the original reads on past the
 * battlefield's objects' into it (script_places.c). No script names one. */
bool script_place_reads_the_sound_command(const GameState *g, uint8_t place);

/* Whether a place is one the original's table has, worked out on what the
 * game has (the place along the heading takes the running channel's mover,
 * which is one of the sixteen channels'). */
bool script_place_known(const GameState *g, uint8_t place);

/* $C1:75CC: the facing a facing code names, for something standing at
 * `from`; the battle keeps it as the facing last found. */
uint8_t script_find_facing(GameState *g, uint8_t facing, BattlePoint from);

/* An animation slot can follow a place, found again each frame (battle/
 * objects.c); the battle keeps the place one up, 0 for none. */
static inline void set_animation_slot_follows(GameState *g, unsigned slot, uint8_t place)
{
    set_battle_animation_position_modes(g, slot, (uint8_t)(place + 1));
}
static inline bool animation_slot_follows(const GameState *g, unsigned slot, uint8_t *place)
{
    uint8_t kept = battle_animation_position_modes(g, slot);
    *place = (uint8_t)(kept - 1);
    return kept != 0;
}

/* The script's point, which the placing commands leave and the moves aim at. */
BattlePoint script_point(const GameState *g);

/* Where a command aims: its coordinates, the script's point, or a place; the
 * battle keeps it as the place last found. */
BattlePoint script_aim(GameState *g, ScriptAim aim, uint8_t x, uint8_t y, uint8_t place);

#endif
