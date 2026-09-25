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
 * - operations, written as C functions over the game's state that take the
 *   parameters they read, by name and type:
 *
 *       static EventFlow loop_animation(GameState *g, FieldObject self, uint8_t animation)
 *       EVENT_OPERATION(loop_animation, self, animation)
 *
 *   where EVENT_OPERATION, after the function, says which of the command's
 *   decoded parameters it takes, in its own order;
 * - a table, one row per opcode, saying which operation it is and how its
 *   operands decode into those parameters (a constant, an operand byte or
 *   word, bits of one, the value of the variable an operand names, the object
 *   an operand names...), how long it is, and for a condition where its jump
 *   is:
 *
 *       { 0xAA, OPERATION(loop_animation), .animation = OPERAND(1), .length = 2 },
 *       { 0xB3, OPERATION(loop_animation), .animation = CONSTANT(0), .length = 1 },
 *
 * Each file of game/event/ ends with its rows. The engine decodes a command
 * once, by its row, and calls the operation with what it takes; an operation
 * says what happens next with an EventFlow and never sees the bytes. A row
 * with no operation is a command that still calls out to the original's code
 * (event/bridge.h): its operands are named the same way.
 *
 * Offsets in the script are 16-bit and wrap. */
#ifndef CT_GAME_EVENT_SCRIPT_H
#define CT_GAME_EVENT_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
#include "state.h"
#include "vars.h"

#include "event/variables.h"
#include "field/object.h"
#include "field/party.h"

/* ---- The script -------------------------------------------------------------------- */

static inline uint8_t script_byte(const GameState *g, uint16_t at) { return event_script_byte(g, at); }

static inline uint16_t script_word(const GameState *g, uint16_t at)
{
    return (uint16_t)(event_script_byte(g, at) | event_script_byte(g, (unsigned)at + 1) << 8);
}

/* Scripts name an object by its offset in the field's object tables, which
 * is how a link stores it (field/object.h); the object whose script is
 * running is the field's current object. */

/* ---- What happens next ------------------------------------------------------------- */

typedef enum {
    FLOW_NEXT,        /* on to the next command, this frame */
    FLOW_END_TURN,    /* on to the next command, but not before next frame */
    FLOW_STAY,        /* the object's turn ends on this command, which runs again next frame */
    FLOW_AGAIN,       /* this command runs again at once */
    FLOW_JUMP,        /* the command's jump: a condition that failed, or a jump */
    FLOW_NEXT_AFTER,  /* on to the command `to` bytes on, this frame: a command shorter than its row */
    FLOW_GO_TO,       /* on at another place in the script, this frame */
} EventFlowKind;

typedef struct {
    EventFlowKind kind;
    uint16_t to;   /* FLOW_NEXT_AFTER, FLOW_GO_TO */
} EventFlow;

static inline EventFlow next_command(void) { return (EventFlow){ FLOW_NEXT, 0 }; }
static inline EventFlow end_turn(void) { return (EventFlow){ FLOW_END_TURN, 0 }; }
static inline EventFlow stay(void) { return (EventFlow){ FLOW_STAY, 0 }; }
static inline EventFlow run_again(void) { return (EventFlow){ FLOW_AGAIN, 0 }; }
static inline EventFlow take_jump(void) { return (EventFlow){ FLOW_JUMP, 0 }; }
static inline EventFlow next_command_after(uint16_t bytes) { return (EventFlow){ FLOW_NEXT_AFTER, bytes }; }
static inline EventFlow go_to(uint16_t to) { return (EventFlow){ FLOW_GO_TO, to }; }

/* A condition's outcome: on after it when it holds, its jump when it doesn't. */
static inline EventFlow unless_jump(bool holds) { return holds ? next_command() : take_jump(); }

/* Where the script goes on, and whether the object's turn does (this frame). */
typedef struct {
    uint16_t next;
    bool go_on;
} EventStep;

/* ---- What a command is called with --------------------------------------------------- */

/* The language's own types for what its commands name (the variables' and
 * the story's are event/variables.h's, the objects' field/object.h's). */
typedef uint32_t Address;     /* anywhere in memory: bank << 16 | offset */
typedef enum { WINDOW_ANYWHERE = 0, WINDOW_TOP = 1, WINDOW_BOTTOM = 2 } WindowPlace;   /* a dialog's */
/* The game's three records of a location: where the party is, where it goes
 * next, and one put aside for later. */
typedef enum { WHERE_THE_PARTY_IS, WHERE_IT_GOES_NEXT, PUT_ASIDE } LocationRecord;
/* How a script moves an object: walking turns it to face its way and runs
 * its walking and standing animations; sliding keeps its facing, and it
 * doesn't animate (a push, a float). */
typedef enum { WALKING, SLIDING } MoveStyle;
/* Where a walk toward another object ends: next to it, or where the walker
 * stands well on screen (someone running in to join a scene). */
typedef enum { NEXT_TO_IT, WELL_ON_SCREEN } WalkEnd;
/* A tint's colour, added to the picture or taken away from it. */
typedef enum { TINT_ADDED, TINT_TAKEN_AWAY } TintMix;
/* A place in a formation a script lays out for the party (event/party.c):
 * the tile the member walks to, and the way it faces there. */
typedef struct {
    MapTile tile;
    Facing facing;
} FormationPlace;

/* Every parameter an operation of the language takes, by type and name. A
 * row of the table says how the command's operands decode into the ones its
 * operation reads (`.animation = OPERAND(1)`, `.animation = CONSTANT(0)`);
 * the engine decodes them into an EventCommand, and hands the operation those
 * it takes (EVENT_OPERATION). */
#define EVENT_PARAMETERS(X)                                                                      \
    /* animation */                                                                              \
    X(uint8_t, animation) X(uint8_t, half_passes) X(uint8_t, frames) X(uint8_t, frame)          \
    /* the scripts' variables and the story */                                                   \
    X(EventVariable, variable) X(uint16_t, value) X(uint16_t, amount) X(Width, width)           \
    X(uint8_t, flags) X(uint8_t, times) X(StoryOffset, story_at) X(uint8_t, flag_bit)           \
    X(bool, on_second_page) X(uint8_t, offset) X(bool, raise) X(Address, address)               \
    /* conditions */                                                                             \
    X(uint16_t, left) X(uint16_t, right) X(uint8_t, comparison) X(uint8_t, progress)            \
    X(uint8_t, point) X(uint16_t, buttons)                                                       \
    /* objects */                                                                                \
    X(FieldObject, object) X(FieldObject, target) X(PartyPlace, member) X(bool, there)              X(uint8_t, level)                                                                            \
    X(uint8_t, function) X(bool, takes) X(Facing, facing) X(EventVariable, column)              \
    X(EventVariable, row) X(uint8_t, speed) X(bool, solid)                                       \
    X(ObjectBlocking, blocked_by) X(ObjectArrival, arrival) X(uint8_t, move_length)             \
    X(uint8_t, tile_x) X(uint8_t, tile_y) X(bool, centred) X(uint16_t, x) X(uint16_t, y)        \
    X(uint8_t, layering) X(ObjectSprite, sprite)                                                 \
    /* moves */                                                                                  \
    X(MoveStyle, style) X(bool, counted) X(WalkEnd, until) X(uint8_t, steps) X(bool, once)      \
    X(Direction, direction) X(uint8_t, height) X(uint8_t, velocity_x) X(uint8_t, velocity_y)    \
    X(uint8_t, acceleration)                                                                     \
    /* the party */                                                                              \
    X(uint8_t, character) X(uint8_t, first_argument) X(uint8_t, second_argument)                \
    /* sound */                                                                                  \
    X(uint8_t, effect) X(uint8_t, song) X(uint8_t, volume) X(uint8_t, sound_command)            \
    /* memory */                                                                                 \
    X(uint8_t, bank) X(uint16_t, size)                                                           \
    /* dialog */                                                                                 \
    X(Address, text) X(uint8_t, line) X(uint8_t, first_choice) X(uint8_t, last_choice)          \
    X(WindowPlace, window) X(uint8_t, answer)                                                    \
    /* places and battles */                                                                     \
    X(uint16_t, map_and_how) X(uint16_t, map) X(uint8_t, how) X(uint16_t, tile)                  \
    X(LocationRecord, record)                                                                    \
    X(bool, raise_1e) X(uint8_t, setup) X(uint8_t, terms)                                        \
    /* the screen */                                                                             \
    X(uint8_t, control) X(uint8_t, brightness) X(uint8_t, rate) X(uint8_t, changes)                \
    X(uint8_t, colour) X(TintMix, mix) X(uint8_t, shaking) X(uint16_t, from) X(uint16_t, to)       \
    X(uint16_t, destination) X(uint8_t, maps) X(uint16_t, drift) X(uint8_t, layers)             \
    X(uint8_t, scroll_x) X(uint8_t, scroll_y) X(uint8_t, scanline)

/* And the parameters a row decodes into a record of the language's rather
 * than a number: the three places of a formation, the leader's first. */
#define EVENT_RECORD_PARAMETERS(X)     X(FormationPlace, leader_place) X(FormationPlace, first_place) X(FormationPlace, second_place)

/* A command, decoded by its row: the object whose script is running, where
 * the command is in the script, the host (for the commands that wait on the
 * sound driver), and every parameter of the language, of which an operation
 * takes the ones it names. Only the engine sees it whole. */
typedef struct {
    FieldObject self;
    uint16_t at;
    Platform *platform;
#define EVENT_PARAMETER_FIELD(type, name) type name;
    EVENT_PARAMETERS(EVENT_PARAMETER_FIELD)
    EVENT_RECORD_PARAMETERS(EVENT_PARAMETER_FIELD)
#undef EVENT_PARAMETER_FIELD
} EventCommand;

typedef EventFlow (*EventOperation)(GameState *g, const EventCommand *command);

/* EVENT_OPERATION(op, parameter...): the form of the operation `op` the table
 * calls, which hands it the decoded command's parameters it names, in that
 * order, after the game's state. OPERATION(op) names it in a row. An
 * operation that takes no parameter is EVENT_OPERATION_TAKING_NOTHING(op). */
#define EVENT_TAKE_(name) command->name
#define EVENT_TAKE_1(a) , EVENT_TAKE_(a)
#define EVENT_TAKE_2(a, b) , EVENT_TAKE_(a), EVENT_TAKE_(b)
#define EVENT_TAKE_3(a, b, c) , EVENT_TAKE_(a), EVENT_TAKE_(b), EVENT_TAKE_(c)
#define EVENT_TAKE_4(a, b, c, d) , EVENT_TAKE_(a), EVENT_TAKE_(b), EVENT_TAKE_(c), EVENT_TAKE_(d)
#define EVENT_TAKE_5(a, b, c, d, e) EVENT_TAKE_4(a, b, c, d), EVENT_TAKE_(e)
#define EVENT_TAKE_6(a, b, c, d, e, f) EVENT_TAKE_5(a, b, c, d, e), EVENT_TAKE_(f)
#define EVENT_TAKE_7(a, b, c, d, e, f, h) EVENT_TAKE_6(a, b, c, d, e, f), EVENT_TAKE_(h)
#define EVENT_COUNT_(_1, _2, _3, _4, _5, _6, _7, n, ...) n
#define EVENT_COUNT(...) EVENT_COUNT_(__VA_ARGS__, 7, 6, 5, 4, 3, 2, 1, 0)
#define EVENT_GLUE_(a, b) a##b
#define EVENT_GLUE(a, b) EVENT_GLUE_(a, b)
#define EVENT_OPERATION(op, ...)                                                                   \
    static EventFlow op##_command(GameState *g, const EventCommand *command)                     \
    {                                                                                              \
        return op(g EVENT_GLUE(EVENT_TAKE_, EVENT_COUNT(__VA_ARGS__))(__VA_ARGS__));               \
    }
#define EVENT_OPERATION_TAKING_NOTHING(op)                                                         \
    static EventFlow op##_command(GameState *g, const EventCommand *command)                     \
    {                                                                                              \
        (void)command;                                                                             \
        return op(g);                                                                              \
    }
#define OPERATION(op) .run = op##_command

/* What the port needs of the game's state before it runs a command, beyond
 * what its operands' kinds imply (event/engine.c): EVENT_CHECK(check,
 * parameter...) after a check taking the game's state and those parameters,
 * and CHECK(check) in a row. Looked at without the machine's time (a guard's
 * peek). */
typedef bool (*EventCheck)(const GameState *g, const EventCommand *command);
#define EVENT_CHECK(check, ...)                                                                    \
    static bool check##_check(const GameState *g, const EventCommand *command)                   \
    {                                                                                              \
        return check(g EVENT_GLUE(EVENT_TAKE_, EVENT_COUNT(__VA_ARGS__))(__VA_ARGS__));            \
    }
#define EVENT_CHECK_TAKING_NOTHING(check)                                                          \
    static bool check##_check(const GameState *g, const EventCommand *command)                   \
    {                                                                                              \
        (void)command;                                                                             \
        return check(g);                                                                           \
    }
#define CHECK(check) .accepts = check##_check

/* ---- How an operand becomes a parameter ------------------------------------------------ */

typedef enum {
    ARG_NONE,
    ARG_CONSTANT,        /* fixed by the opcode (a short form's) */
    ARG_BYTE,            /* the operand byte */
    ARG_BYTE_PLUS_ONE,   /* the operand byte, plus one (a count the script gives less one) */
    ARG_BITS,            /* bits of the operand byte: shifted down, then masked */
    ARG_WORD,            /* the operand word */
    ARG_WORD_BITS,       /* bits of the operand word */
    ARG_ADDRESS,         /* three operand bytes: an address anywhere in memory */
    ARG_VARIABLE,        /* the variable the operand names, for the operation to set */
    ARG_VARIABLE_BYTE,   /* the value of the variable the operand names, as a byte */
    ARG_VARIABLE_WORD,   /* the value of the variable the operand names, as a word */
    ARG_OBJECT,          /* the object the operand names */
    ARG_SELF,            /* the object whose script this is */
    ARG_MEMBER,          /* the party member the operand names, by their place (anyone there or not) */
    ARG_MEMBER_OBJECT,   /* the object of the party member the operand names */
    ARG_MEMBER_PRESENT,  /* whether there is a party member where the operand says */
    ARG_STORY_WORD,      /* the word of the story memory at the operand word */
    ARG_STORY_PROGRESS,  /* the story's progress counter */
    ARG_BIT,             /* the operand is a bit number: the byte with that bit set */
    ARG_ALL_BUT_BIT,     /* the operand is a bit number: the byte with every other bit set */
    ARG_FLAG,            /* the operand is 0 or 1: false or true */
    ARG_FORMATION_PLACE, /* two operands, a tile's column and row, their top bits the way it faces */
} EventArgKind;

typedef struct {
    EventArgKind kind;
    uint8_t operand;     /* which: 1 is the byte after the opcode */
    uint16_t constant;   /* ARG_CONSTANT; the mask of ARG_BITS and ARG_WORD_BITS */
    uint8_t shift;       /* ARG_BITS, ARG_WORD_BITS */
} EventArg;

#define CONSTANT(k) { ARG_CONSTANT, 0, (k), 0 }
#define OPERAND(n) { ARG_BYTE, (n), 0, 0 }
#define OPERAND_PLUS_ONE(n) { ARG_BYTE_PLUS_ONE, (n), 0, 0 }
#define OPERAND_BITS(n, shift, mask) { ARG_BITS, (n), (mask), (shift) }
#define OPERAND_WORD(n) { ARG_WORD, (n), 0, 0 }
#define OPERAND_WORD_BITS(n, shift, mask) { ARG_WORD_BITS, (n), (mask), (shift) }
#define OPERAND_ADDRESS(n) { ARG_ADDRESS, (n), 0, 0 }
#define VARIABLE(n) { ARG_VARIABLE, (n), 0, 0 }
#define VARIABLE_BYTE(n) { ARG_VARIABLE_BYTE, (n), 0, 0 }
#define VARIABLE_WORD(n) { ARG_VARIABLE_WORD, (n), 0, 0 }
#define OBJECT(n) { ARG_OBJECT, (n), 0, 0 }
#define SELF { ARG_SELF, 0, 0, 0 }
#define MEMBER(n) { ARG_MEMBER, (n), 0, 0 }
#define MEMBER_OBJECT(n) { ARG_MEMBER_OBJECT, (n), 0, 0 }
#define MEMBER_PRESENT(n) { ARG_MEMBER_PRESENT, (n), 0, 0 }
#define STORY_WORD(n) { ARG_STORY_WORD, (n), 0, 0 }
#define STORY_PROGRESS { ARG_STORY_PROGRESS, 0, 0, 0 }
#define BIT(n) { ARG_BIT, (n), 0, 0 }
#define ALL_BUT_BIT(n) { ARG_ALL_BUT_BIT, (n), 0, 0 }
#define FLAG(n) { ARG_FLAG, (n), 0, 0 }
#define FORMATION_PLACE(n) { ARG_FORMATION_PLACE, (n), 0, 0 }

/* The widths, as a row gives them. */
#define BYTES CONSTANT(AS_BYTE)
#define WORDS CONSTANT(AS_WORD)

/* ---- The table ------------------------------------------------------------------------ */

typedef struct {
    uint8_t opcode;
    EventOperation run;    /* the operation, or NULL for a command that still calls out (event/bridge.h) */
    struct {               /* how each parameter the operation reads is decoded; the rest are ARG_NONE */
#define EVENT_PARAMETER_SPEC(type, name) EventArg name;
        EVENT_PARAMETERS(EVENT_PARAMETER_SPEC)
        EVENT_RECORD_PARAMETERS(EVENT_PARAMETER_SPEC)
#undef EVENT_PARAMETER_SPEC
    };
    uint8_t length;        /* the command's bytes, its opcode's included */
    uint8_t jump;          /* the operand holding the jump's distance (conditions, jumps), or 0 */
    bool back;             /* the jump goes back */
    bool member_may_be_absent;   /* a party member operand may name an empty place */
    EventCheck accepts;    /* NULL, or what else the port needs (the guard's part) */
} EventOpcode;

typedef struct {
    const EventOpcode *rows;
    unsigned count;
} EventOpcodes;

#define EVENT_OPCODES(name, ...)                                  \
    static const EventOpcode name##_rows[] = { __VA_ARGS__ };     \
    const EventOpcodes name = { name##_rows, sizeof name##_rows / sizeof name##_rows[0] }

/* Every file's rows (event/engine.c indexes them). */
extern const EventOpcodes event_animation_opcodes, event_calls_opcodes, event_dialog_opcodes,
    event_facing_opcodes, event_flow_opcodes, event_input_opcodes, event_locations_opcodes, event_memory_opcodes,
    event_movement_opcodes, event_party_opcodes, event_properties_opcodes, event_screen_opcodes,
    event_sound_opcodes, event_story_opcodes, event_variables_opcodes;

/* The row of an opcode, or NULL for one with no port (event/engine.c). */
const EventOpcode *event_opcode(uint8_t opcode);

/* Whether the port runs the command at `at` in this state: the running
 * object is one, the command is inside the script buffer, its operands name
 * what their kinds say, and its row's own check holds. */
bool event_runs_here(const GameState *g, const EventOpcode *row, uint16_t at);

/* The command at `at`, decoded by its row. */
EventCommand event_decode(const GameState *g, Platform *pf, const EventOpcode *row, uint16_t at);

/* Run the command at `at` by its row: decode, operate, and where next. */
EventStep event_perform(GameState *g, Platform *pf, const EventOpcode *row, uint16_t at);

/* Where the script goes on after a command at `at` whose operation said
 * `flow`. */
EventStep event_follow(GameState *g, const EventOpcode *row, uint16_t at, EventFlow flow);

#endif
