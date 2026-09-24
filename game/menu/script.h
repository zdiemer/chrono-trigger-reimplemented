/* The sprite-object mode's command scripts: how a command is read, and what
 * it tells the interpreter to do next.
 *
 * An object that runs a script (menu/object.h) goes through it a command at
 * a time each frame, until a command says to wait for the next frame or the
 * object is done. A command is an opcode byte and its operands; most opcodes
 * are one of a family -- change a byte of the object's state or of the
 * game's, test one and branch, start another object with a handler or a
 * script, last a number of frames -- so the language is two things:
 *
 * - operations, C functions over the game's state that take the parameters
 *   they read by name and type, after the object running the script:
 *
 *       static ScriptFlow change_byte(GameState *g, MenuObject self, ByteChange change,
 *                                     ByteRef byte, uint8_t value)
 *       SCRIPT_OPERATION(change_byte, change, byte, value)
 *
 * - a table, a row an opcode, saying which operation it is and how its
 *   operands decode into those parameters (a constant, an operand byte or
 *   word, a byte of the object named by its offset, a cell of low RAM named
 *   by its address...), and how long it is:
 *
 *       { 0x0B, OPERATION(change_byte), .change = CONSTANT(BYTE_COUNT_UP), .byte = SLOT_BYTE(1),
 *         .length = 2 },
 *       { 0x11, OPERATION(change_byte), .change = CONSTANT(BYTE_COUNT_UP), .byte = CELL(1),
 *         .length = 3 },
 *
 * The interpreter (menu/script.c) decodes a command once, by its row, and
 * calls the operation, which says what happens next with a ScriptFlow and
 * never sees the bytes. What a few commands reach that isn't the language's
 * -- the map's scrolling and redrawing, the sound queue, a routine a script
 * names -- they reach through services (ScriptServices), which the
 * interpreter's runner supplies. Offsets in a script wrap in its bank. */
#ifndef CT_GAME_MENU_SCRIPT_H
#define CT_GAME_MENU_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/object.h"
#include "state.h"

/* ---- What happens next ------------------------------------------------------------------ */

typedef enum {
    SCRIPT_NEXT,         /* on to the next command */
    SCRIPT_BRANCH,       /* on by `by` bytes from this command (by 0: the object waits, as for any 0) */
    SCRIPT_GO_TO,        /* on at `to` in the script's bank */
    SCRIPT_WAIT,         /* this command again next frame */
    SCRIPT_END,          /* the object is done, and its slot free */
    SCRIPT_HANDED_BACK,  /* a service handed the rest to someone else (ScriptServices) */
} ScriptFlowKind;

typedef struct {
    ScriptFlowKind kind;
    uint16_t to;
    int16_t by;
} ScriptFlow;

static inline ScriptFlow next_command(void) { return (ScriptFlow){ SCRIPT_NEXT, 0, 0 }; }
static inline ScriptFlow branch_by(int8_t by) { return (ScriptFlow){ SCRIPT_BRANCH, 0, by }; }
static inline ScriptFlow go_to(uint16_t to) { return (ScriptFlow){ SCRIPT_GO_TO, to, 0 }; }
static inline ScriptFlow wait_a_frame(void) { return (ScriptFlow){ SCRIPT_WAIT, 0, 0 }; }
static inline ScriptFlow end_object(void) { return (ScriptFlow){ SCRIPT_END, 0, 0 }; }
static inline ScriptFlow handed_back(void) { return (ScriptFlow){ SCRIPT_HANDED_BACK, 0, 0 }; }

/* A condition: its branch where it holds, on to the next command where it
 * doesn't. */
static inline ScriptFlow branch_if_holds(bool holds, int8_t by) { return holds ? branch_by(by) : next_command(); }

/* Whether the object stops on the command this frame. */
static inline bool script_flow_stops(ScriptFlow f)
{
    return f.kind == SCRIPT_WAIT || f.kind == SCRIPT_END || (f.kind == SCRIPT_BRANCH && f.by == 0);
}

/* ---- The language's types --------------------------------------------------------------- */

/* A byte a command changes or tests: one of the object's own, by its offset
 * from the slot's start (menu_object_byte), or a cell of low RAM, by its
 * address. */
typedef struct {
    bool of_the_object;
    uint16_t at;
} ByteRef;

uint8_t script_byte_at(const GameState *g, MenuObject self, ByteRef b);
void script_set_byte_at(GameState *g, MenuObject self, ByteRef b, uint8_t v);

typedef enum {
    BYTE_CLEAR, BYTE_COUNT_UP, BYTE_COUNT_DOWN, BYTE_SET, BYTE_SET_BITS, BYTE_CLEAR_BITS, BYTE_ADD,
    BYTE_SUBTRACT,
} ByteChange;

typedef enum {
    TEST_ZERO, TEST_NONZERO, TEST_EQUAL, TEST_NOT_EQUAL, TEST_ANY_BIT, TEST_NO_BIT, TEST_BELOW, TEST_AT_LEAST,
} ByteTest;

/* Which of the map's planes a scroll moves: the first two together (and the
 * layer over them with them), or the one plane the script names. */
enum { BOTH_PLANES = 0 };

/* Every parameter an operation of the language takes, by type and name. */
#define SCRIPT_PARAMETERS(X)                                                                    \
    X(ByteChange, change) X(ByteTest, test) X(ByteRef, byte) X(ByteRef, from) X(uint8_t, value) \
    X(int8_t, by) X(uint16_t, to)                                                               \
    X(uint16_t, handler) X(MenuSlotKind, slots) X(uint32_t, script) X(uint8_t, frames)          \
    X(bool, moving) X(bool, animating) X(MenuAxis, axis) X(uint16_t, target) X(uint8_t, forward_script) \
    X(uint8_t, back_script) X(uint16_t, word) X(uint16_t, second_word) X(uint8_t, sprite_script)  \
    X(uint8_t, keep) X(uint8_t, bits) X(uint8_t, plane) X(uint16_t, age)                        \
    X(uint32_t, colours) X(uint8_t, row) X(uint8_t, hand_over)                                   \
    X(uint16_t, location) X(uint8_t, region) X(uint8_t, tile_x) X(uint8_t, tile_y)              \
    X(uint8_t, column) X(uint8_t, member)                                                        \
    X(uint8_t, table) X(uint8_t, entry) X(bool, mark)                                            \
    X(uint32_t, source) X(uint16_t, vram) X(uint16_t, size)                                      \
    X(uint8_t, from_plane) X(uint8_t, from_column) X(uint8_t, from_row)                          \
    X(uint8_t, columns) X(uint8_t, rows) X(uint8_t, group)                                       \
    X(uint8_t, sound) X(uint8_t, first) X(uint8_t, second) X(uint8_t, third) X(bool, unless_kept)       X(uint32_t, routine) X(bool, far)

/* What a few commands reach that isn't the game's state: the original's code
 * on the development system, the game's own elsewhere. Each says whether the
 * port still has the game after it (false: the call handed the rest of the
 * frame to someone else, and the command stops there). */
typedef struct {
    /* Scroll one of the map's planes by a number of whole map tiles (menu/map.c). */
    bool (*scroll_map)(void *ctx, MenuAxis axis, uint8_t plane, int8_t step);
    /* Redraw a plane whole. */
    bool (*redraw_map)(void *ctx, uint8_t plane);
    /* Pass the sound request on to the driver when it is free ($C2:2ED9). */
    bool (*pass_on_sound)(void *ctx);
    /* Run a routine a script names, in bank C2 or anywhere. */
    bool (*call_routine)(void *ctx, uint32_t routine, bool far);
    void *ctx;
} ScriptServices;

/* A command, decoded by its row: the object whose script is running, where
 * the command is, the services, and every parameter of the language, of which
 * an operation takes the ones it names. */
typedef struct {
    MenuObject self;
    uint32_t at;   /* the command's opcode byte, 24-bit */
    const ScriptServices *services;
#define SCRIPT_PARAMETER_FIELD(type, name) type name;
    SCRIPT_PARAMETERS(SCRIPT_PARAMETER_FIELD)
#undef SCRIPT_PARAMETER_FIELD
} ScriptCommand;

typedef ScriptFlow (*ScriptOperation)(GameState *g, const ScriptCommand *command);

#define SCRIPT_TAKE_(name) command->name
#define SCRIPT_TAKE_1(a) , SCRIPT_TAKE_(a)
#define SCRIPT_TAKE_2(a, b) SCRIPT_TAKE_1(a), SCRIPT_TAKE_(b)
#define SCRIPT_TAKE_3(a, b, c) SCRIPT_TAKE_2(a, b), SCRIPT_TAKE_(c)
#define SCRIPT_TAKE_4(a, b, c, d) SCRIPT_TAKE_3(a, b, c), SCRIPT_TAKE_(d)
#define SCRIPT_TAKE_5(a, b, c, d, e) SCRIPT_TAKE_4(a, b, c, d), SCRIPT_TAKE_(e)
#define SCRIPT_TAKE_6(a, b, c, d, e, f) SCRIPT_TAKE_5(a, b, c, d, e), SCRIPT_TAKE_(f)
#define SCRIPT_TAKE_7(a, b, c, d, e, f, h) SCRIPT_TAKE_6(a, b, c, d, e, f), SCRIPT_TAKE_(h)
#define SCRIPT_TAKE_8(a, b, c, d, e, f, h, i) SCRIPT_TAKE_7(a, b, c, d, e, f, h), SCRIPT_TAKE_(i)
#define SCRIPT_COUNT_(_1, _2, _3, _4, _5, _6, _7, _8, n, ...) n
#define SCRIPT_COUNT(...) SCRIPT_COUNT_(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define SCRIPT_GLUE_(a, b) a##b
#define SCRIPT_GLUE(a, b) SCRIPT_GLUE_(a, b)

/* SCRIPT_OPERATION(op, parameter...): the form of `op` the table calls, which
 * hands it the running object and the decoded parameters it names, in that
 * order. SCRIPT_OPERATION_OF_THE_OBJECT(op) for one that takes nothing else,
 * SCRIPT_OPERATION_TAKING_NOTHING(op) for one that takes neither, and
 * SCRIPT_SERVED_OPERATION(op, parameter...) for one that takes the services
 * after the object. */
#define SCRIPT_OPERATION(op, ...)                                                               \
    static ScriptFlow op##_command(GameState *g, const ScriptCommand *command)                 \
    {                                                                                          \
        return op(g, command->self SCRIPT_GLUE(SCRIPT_TAKE_, SCRIPT_COUNT(__VA_ARGS__))(__VA_ARGS__)); \
    }
#define SCRIPT_SERVED_OPERATION(op, ...)                                                        \
    static ScriptFlow op##_command(GameState *g, const ScriptCommand *command)                 \
    {                                                                                          \
        return op(g, command->self, command->services                                         \
                  SCRIPT_GLUE(SCRIPT_TAKE_, SCRIPT_COUNT(__VA_ARGS__))(__VA_ARGS__));           \
    }
#define SCRIPT_OPERATION_OF_THE_OBJECT(op)                                                       \
    static ScriptFlow op##_command(GameState *g, const ScriptCommand *command)                 \
    {                                                                                          \
        return op(g, command->self);                                                           \
    }
#define SCRIPT_OPERATION_TAKING_NOTHING(op)                                                     \
    static ScriptFlow op##_command(GameState *g, const ScriptCommand *command)                 \
    {                                                                                          \
        (void)command;                                                                         \
        return op(g);                                                                          \
    }
#define OPERATION(op) .run = op##_command

/* ---- How an operand becomes a parameter -------------------------------------------------- */

typedef enum {
    ARG_NONE,
    ARG_CONSTANT,      /* fixed by the opcode */
    ARG_BYTE,          /* the operand byte */
    ARG_SIGNED,        /* the operand byte, signed (a branch's distance) */
    ARG_WORD,          /* the operand word */
    ARG_LONG,          /* three operand bytes: an address anywhere */
    ARG_WORD_AND_BANK, /* the operand word, and the bank the byte at `bank_at` names */
    ARG_SLOT_BYTE,     /* the operand byte names a byte of the running object */
    ARG_CELL,          /* the operand word names a cell of low RAM */
} ScriptArgKind;

typedef struct {
    ScriptArgKind kind;
    uint8_t operand;   /* which: 1 is the byte after the opcode */
    uint32_t constant;
    uint8_t bank_at;
} ScriptArg;

#define CONSTANT(k) { ARG_CONSTANT, 0, (k), 0 }
#define OPERAND(n) { ARG_BYTE, (n), 0, 0 }
#define SIGNED_OPERAND(n) { ARG_SIGNED, (n), 0, 0 }
#define OPERAND_WORD(n) { ARG_WORD, (n), 0, 0 }
#define OPERAND_LONG(n) { ARG_LONG, (n), 0, 0 }
#define WORD_IN_BANK(n, b) { ARG_WORD_AND_BANK, (n), 0, (b) }
#define SLOT_BYTE(n) { ARG_SLOT_BYTE, (n), 0, 0 }
#define CELL(n) { ARG_CELL, (n), 0, 0 }

typedef struct {
    uint8_t opcode;
    ScriptOperation run;
    struct {
#define SCRIPT_PARAMETER_SPEC(type, name) ScriptArg name;
        SCRIPT_PARAMETERS(SCRIPT_PARAMETER_SPEC)
#undef SCRIPT_PARAMETER_SPEC
    };
    uint8_t length;        /* the command's bytes, its opcode's included */
} ScriptOpcode;

typedef struct {
    const ScriptOpcode *rows;
    unsigned count;
} ScriptOpcodes;

#define SCRIPT_OPCODES(name, ...)                                      \
    static const ScriptOpcode name##_rows[] = { __VA_ARGS__ };         \
    const ScriptOpcodes name = { name##_rows, sizeof name##_rows / sizeof name##_rows[0] }

extern const ScriptOpcodes menu_script_opcodes;

/* The row of an opcode, or NULL where there is none (past the language's
 * $52). */
const ScriptOpcode *menu_script_opcode(uint8_t opcode);

/* The command at `at`, decoded by its row. */
ScriptCommand menu_script_decode(const GameState *g, const ScriptOpcode *row, MenuObject self, uint32_t at,
                                 const ScriptServices *services);

/* Whether the command is one the port runs as the original does: every cell
 * it names is low RAM, and every table it indexes is one it has (a script the
 * game has never breaks either). */
bool menu_script_runs_here(const GameState *g, const ScriptOpcode *row, const ScriptCommand *c);

/* Run a decoded command. */
static inline ScriptFlow menu_script_perform(GameState *g, const ScriptOpcode *row, const ScriptCommand *c)
{
    return row->run(g, c);
}

/* ---- The interpreter ---------------------------------------------------------------------- */

/* How the interpreter runs each command: `run` runs the command at `at` for
 * the object and says what happens next (on the development system, it may
 * hand the rest to the original: SCRIPT_HANDED_BACK). */
typedef struct {
    ScriptFlow (*run)(void *ctx, MenuObject self, uint32_t at);
    void *ctx;
} ScriptRunner;

typedef enum {
    SCRIPT_WAITING,       /* the object waits for next frame */
    SCRIPT_DONE,          /* the object is done: free its slot */
    SCRIPT_GAVE_BACK,     /* a command handed the rest to someone else */
} ScriptEnd;

/* Run the object's script for this frame: commands from where it left off,
 * until one makes it wait or ends it. `stopped` is where the command it
 * stopped on is. */
ScriptEnd menu_run_script(GameState *g, MenuObject self, const ScriptRunner *r, uint32_t *stopped);

/* Where the script goes on from the command at `at` after `flow`. */
uint32_t menu_script_follow(const ScriptOpcode *row, uint32_t at, ScriptFlow flow);

/* A runner with nothing between the interpreter and the commands. */
ScriptFlow menu_script_run_directly(GameState *g, const ScriptServices *services, MenuObject self, uint32_t at);

#endif
