/* The event script language: how a command is read, and what it tells the
 * engine to do next.
 *
 * Every object on a map runs a script: a string of commands, each an opcode
 * byte and its operands, in the buffer the map's event data decompresses to
 * (event_script). The engine (event/engine.c) runs each object's script a
 * turn at a time. Most opcodes are one of a family -- "loop an animation" with
 * the animation given, or fixed to 0 or 1 by a one-byte short form; "add to a
 * variable" a constant or another variable, a byte or a word -- so the
 * language is two things:
 *
 * - operations, written as C functions over the game's state with typed,
 *   named parameters: `loop_animation(g, self, animation)`;
 * - a table, one row per opcode, saying which operation it is and how its
 *   operands decode into the operation's parameters (a constant, an operand
 *   byte or word, the value of the variable an operand names, the object an
 *   operand names...), how long it is, and for a condition where its jump is.
 *
 * Each file of game/event/ ends with its rows. The engine decodes a command
 * once, by its row, and calls the operation; an operation says what happens
 * next with an EventFlow and never sees the bytes.
 *
 * Offsets in the script are 16-bit and wrap. */
#ifndef CT_GAME_EVENT_SCRIPT_H
#define CT_GAME_EVENT_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
#include "state.h"
#include "vars.h"

/* ---- The script -------------------------------------------------------------------- */

static inline uint8_t script_byte(const GameState *g, uint16_t at) { return event_script_byte(g, at); }

static inline uint16_t script_word(const GameState *g, uint16_t at)
{
    return (uint16_t)(event_script_byte(g, at) | event_script_byte(g, (unsigned)at + 1) << 8);
}

/* ---- Objects -------------------------------------------------------------------------- */

/* An object on the map: its slot in the field's object tables. Scripts name
 * one by its slot doubled (its offset in those tables); `slot_of` turns that
 * into the slot. */
typedef unsigned EventObject;

static inline EventObject slot_of(uint16_t doubled) { return doubled >> 1; }

/* The object whose script is running. */
static inline EventObject running_object(const GameState *g) { return slot_of(field_current_object(g)); }

/* ---- What happens next ------------------------------------------------------------- */

typedef enum {
    FLOW_NEXT,        /* on to the next command, this frame */
    FLOW_END_TURN,    /* on to the next command, but not before next frame */
    FLOW_STAY,        /* the object's turn ends on this command, which runs again next frame */
    FLOW_AGAIN,       /* this command runs again at once */
    FLOW_JUMP,        /* the command's jump: a condition that failed, or a jump */
    FLOW_NEXT_AFTER,  /* on to the command `to` bytes on, this frame: a command shorter than its row */
    FLOW_GO_TO,       /* on at another place in the script, this frame */
    FLOW_RESUME_AT,   /* on at another place in the script, next frame */
} EventFlowKind;

typedef struct {
    EventFlowKind kind;
    uint16_t to;   /* FLOW_NEXT_AFTER, FLOW_GO_TO, FLOW_RESUME_AT */
} EventFlow;

static inline EventFlow next_command(void) { return (EventFlow){ FLOW_NEXT, 0 }; }
static inline EventFlow end_turn(void) { return (EventFlow){ FLOW_END_TURN, 0 }; }
static inline EventFlow stay(void) { return (EventFlow){ FLOW_STAY, 0 }; }
static inline EventFlow run_again(void) { return (EventFlow){ FLOW_AGAIN, 0 }; }
static inline EventFlow take_jump(void) { return (EventFlow){ FLOW_JUMP, 0 }; }
static inline EventFlow next_command_after(uint16_t bytes) { return (EventFlow){ FLOW_NEXT_AFTER, bytes }; }
static inline EventFlow go_to(uint16_t to) { return (EventFlow){ FLOW_GO_TO, to }; }
static inline EventFlow resume_at(uint16_t to) { return (EventFlow){ FLOW_RESUME_AT, to }; }

/* A condition's outcome: on after it when it holds, its jump when it doesn't. */
static inline EventFlow unless_jump(bool holds) { return holds ? next_command() : take_jump(); }

/* Where the script goes on, and whether the object's turn does (this frame). */
typedef struct {
    uint16_t next;
    bool go_on;
} EventStep;

/* ---- A command's operands, decoded ----------------------------------------------------- */

/* How an operand becomes an operation's parameter. */
typedef enum {
    ARG_NONE,
    ARG_CONSTANT,        /* fixed by the opcode (a short form's) */
    ARG_BYTE,            /* the operand byte */
    ARG_WORD,            /* the operand word */
    ARG_ADDRESS,         /* three operand bytes: an address anywhere in memory */
    ARG_VARIABLE,        /* the variable the operand names, for the operation to set */
    ARG_VARIABLE_BYTE,   /* the value of the variable the operand names, as a byte */
    ARG_VARIABLE_WORD,   /* the value of the variable the operand names, as a word */
    ARG_OBJECT,          /* the object the operand names */
    ARG_SELF,            /* the object whose script this is */
    ARG_MEMBER_OBJECT,   /* the object of the party member the operand names */
    ARG_MEMBER_PRESENT,  /* whether there is a party member where the operand says */
    ARG_STORY_WORD,      /* the word of the story memory at the operand word */
    ARG_STORY_PROGRESS,  /* the story's progress counter */
    ARG_BIT,             /* the operand is a bit number: the byte with that bit set */
    ARG_ALL_BUT_BIT,     /* the operand is a bit number: the byte with every other bit set */
    ARG_HERE,            /* where the command is in the script */
} EventArgKind;

typedef struct {
    uint8_t kind;        /* EventArgKind */
    uint8_t operand;     /* which: 1 is the byte after the opcode */
    uint16_t constant;
} EventArg;

#define CONSTANT(k) { ARG_CONSTANT, 0, (k) }
#define BYTE(n) { ARG_BYTE, (n), 0 }
#define WORD(n) { ARG_WORD, (n), 0 }
#define ADDRESS(n) { ARG_ADDRESS, (n), 0 }
#define VARIABLE(n) { ARG_VARIABLE, (n), 0 }
#define VARIABLE_BYTE(n) { ARG_VARIABLE_BYTE, (n), 0 }
#define VARIABLE_WORD(n) { ARG_VARIABLE_WORD, (n), 0 }
#define OBJECT(n) { ARG_OBJECT, (n), 0 }
#define SELF { ARG_SELF, 0, 0 }
#define MEMBER_OBJECT(n) { ARG_MEMBER_OBJECT, (n), 0 }
#define MEMBER_PRESENT(n) { ARG_MEMBER_PRESENT, (n), 0 }
#define STORY_WORD(n) { ARG_STORY_WORD, (n), 0 }
#define STORY_PROGRESS { ARG_STORY_PROGRESS, 0, 0 }
#define BIT(n) { ARG_BIT, (n), 0 }
#define ALL_BUT_BIT(n) { ARG_ALL_BUT_BIT, (n), 0 }
#define HERE { ARG_HERE, 0, 0 }

enum { EVENT_ARGS = 4 };

/* What an operation is called with. */
typedef struct {
    EventObject self;            /* the object whose script is running */
    uint32_t arg[EVENT_ARGS];    /* its parameters, in the row's order */
    Platform *platform;          /* the host, for the commands that wait on the sound driver */
} EventArgs;

typedef EventFlow (*EventOperation)(GameState *g, const EventArgs *a);

/* What the port needs of the game's state before it runs a command, beyond
 * what its operands' kinds imply (event/engine.c); `at` is the command's
 * offset. Looked at without the machine's time (a guard's peek). */
typedef bool (*EventCheck)(const GameState *g, uint16_t at);

/* ---- The table ------------------------------------------------------------------------ */

typedef struct {
    uint8_t opcode;
    EventOperation run;
    EventArg args[EVENT_ARGS];
    uint8_t length;        /* the command's bytes, its opcode's included */
    uint8_t jump;          /* the operand holding the jump's distance (conditions, jumps), or 0 */
    bool back;             /* the jump goes back */
    bool member_may_be_absent;   /* a party member operand may name an empty place */
    EventCheck accepts;    /* NULL, or what else the port needs (the guard's part) */
    uint32_t routine;      /* the original's handler, whose registered entry runs this row */
    /* The rest is scaffolding: what the original leaves for its callers and
     * for shadow mode, which the engine and the entries reproduce around the
     * operation (event/leaves.h). None of it is the operation's business. */
    uint8_t parse, parse_on_jump;   /* the operand the original's parse stopped at, in Y */
    bool shared_comparison;         /* made by the original's comparison routine, which notes
                                     * the jump byte's offset and runs with 8-bit index
                                     * registers (so Y keeps only its low byte) */
    uint8_t keeps, keeps_second;    /* KEEPS_*: a parameter it keeps in its first scratch cells */
    uint8_t overflow;               /* OVERFLOW_*: the V flag its addition leaves */
} EventOpcode;

/* `keeps`: parameter n, as a byte or as a word; or the offset of operand n,
 * as a word. */
#define KEEPS_BYTE(n) (0x80 | (n))
#define KEEPS_WORD(n) (0xC0 | (n))
#define KEEPS_OFFSET(n) (0x20 | (n))

/* `overflow`: none (V as it was), or that of the addition or subtraction of
 * parameter 1 to or from the variable in parameter 0, in parameter 2's width. */
enum { OVERFLOW_NONE, OVERFLOW_OF_ADDING, OVERFLOW_OF_SUBTRACTING };

typedef struct {
    const EventOpcode *rows;
    unsigned count;
} EventOpcodes;

#define EVENT_OPCODES(name, ...)                                                   \
    static const EventOpcode name##_rows[] = { __VA_ARGS__ };                      \
    const EventOpcodes name = { name##_rows, sizeof name##_rows / sizeof name##_rows[0] }

/* Every file's rows (event/engine.c indexes them). */
extern const EventOpcodes event_animation_opcodes, event_arithmetic_opcodes, event_calls_opcodes,
    event_dialog_opcodes, event_facing_opcodes, event_flow_opcodes, event_input_opcodes, event_locations_opcodes,
    event_memory_opcodes, event_party_opcodes, event_properties_opcodes, event_screen_opcodes,
    event_sound_opcodes, event_story_opcodes, event_variables_opcodes;

/* The row of an opcode, or NULL for one with no port (event/engine.c). */
const EventOpcode *event_opcode(uint8_t opcode);

/* Whether the port runs the command at `at` in this state: the running
 * object is one, the command is inside the script buffer, its operands name
 * what their kinds say, and its row's own check holds. */
bool event_runs_here(const GameState *g, const EventOpcode *row, uint16_t at);

/* Run the command at `at` by its row: decode, operate, and where next. */
EventStep event_perform(GameState *g, Platform *pf, const EventOpcode *row, uint16_t at);

/* An object offset (a slot doubled) that names an object the port's
 * accessors reach as the original's code does: even, and in the tables. */
bool event_names_object(uint16_t doubled);

#endif
