/* Battle: the screen script language -- what its commands share
 * (battle/screen_script.c runs the channels).
 *
 * A spell or an attack that dresses the whole screen -- the battlefield
 * shaking, a beam of light, the layers swaying, a triangle of darkness
 * turning -- does it with a screen script from bank $D1, run on one of two
 * channels. Each channel moves a layer of its own (battle_screen_layer).
 *
 * A script is a string of bytes. A byte below $7F shows a frame of the
 * effect's animated graphics and ends the channel's step; $7F only ends the
 * step; $FF ends the script. The bytes from $80 are commands, each followed by
 * its operands, which the channel runs one after the other within its step:
 * the waits are the commands that jump back to the step's end until what they
 * wait for has happened.
 *
 * The commands are the language's rows, one per command byte, in the files
 * that hold their operations (screen_*.c): the operation, how each of its
 * parameters comes from the command's operands, the command's length, and
 * what the operation needs of the state to run as the original does. The
 * runner decodes a command by its row into a ScreenCommand and calls the
 * operation with the parameters it names; the operation says where the
 * channel goes on (ScreenFlow). A row whose work is still the original's code
 * has a bridge instead, which the runner calls with the machine (the wait for
 * a line of the picture, $C1); and a command whose row's needs the state
 * doesn't meet runs as the original's handler, which the runner calls as the
 * original's dispatch does. */
#ifndef CT_GAME_BATTLE_SCREEN_SCRIPT_H
#define CT_GAME_BATTLE_SCREEN_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/screen.h"

/* ---- Where a command goes on -------------------------------------------------------------- */

typedef enum {
    SCREEN_ON,             /* past the command, to the next */
    SCREEN_GO_TO,          /* to another place in the script, in its bank */
    SCREEN_MOVED,          /* the operation moved the place itself (screen_command_took) */
} ScreenFlowKind;

typedef struct {
    ScreenFlowKind kind;
    uint16_t to;
} ScreenFlow;

static inline ScreenFlow screen_on(void) { return (ScreenFlow){ SCREEN_ON, 0 }; }
static inline ScreenFlow screen_go_to(uint16_t to) { return (ScreenFlow){ SCREEN_GO_TO, to }; }
static inline ScreenFlow screen_moved(void) { return (ScreenFlow){ SCREEN_MOVED, 0 }; }

/* A wait: on when what it waits for has happened, otherwise back `back`
 * bytes, counted from the command's second byte, to the step's end it waits
 * at. A script names the distance as a byte, and the place moves within its
 * bank. */
ScreenFlow screen_wait_back(GameState *g, bool waiting, uint16_t at, uint8_t back);

/* ---- What a command is called with ---------------------------------------------------- */

/* Three bytes, one for each corner of a triangle or each axis it turns about. */
typedef struct {
    uint8_t of[3];
} ScreenTriple;

/* How a circle is drawn (screen_circles.c): round; flattened below, its half
 * below the middle half as tall; flattened below and hung from the top of
 * the screen, the window above its middle as wide as it is there; twice as
 * tall; twice as wide; twice the size. */
typedef enum {
    CIRCLE_ROUND, CIRCLE_FLATTENED_BELOW, CIRCLE_HUNG_FROM_THE_TOP, CIRCLE_TALL, CIRCLE_WIDE, CIRCLE_TWICE_THE_SIZE,
    CIRCLE_KINDS
} CircleKind;

/* The special effects of command $80 (screen_specials.c), by number. */
typedef enum {
    SPECIAL_BANDS_OF_LIGHT_AT_FULL, SPECIAL_ROLL_THE_BANDS, SPECIAL_CLOSE_WEDGE_IN_THE_MIDDLE,
    SPECIAL_OPEN_WEDGE_IN_THE_MIDDLE, SPECIAL_CLOSE_THE_WINDOWS, SPECIAL_TURN_THE_CORNERS_ROUND,
    SPECIAL_WEDGE_FROM_THE_LEFT, SPECIAL_CLOSE_WEDGE_FROM_THE_LEFT, SPECIAL_STRAIGHTEN_THE_ANGLES,
    SPECIAL_SHOW_EFFECT_SPRITES_IN_FRONT, SPECIAL_BOB_LAYER_DOWN, SPECIAL_BOB_LAYER_UP,
    SPECIAL_LAY_OUT_THE_NUDGE_WAVE, SPECIAL_NUDGE_THE_WINDOW_LINES, SPECIAL_BANDS_OF_LIGHT,
    SPECIAL_CLEAR_THE_ENDED_OBJECTS_ATTRIBUTES, SPECIAL_PUT_OUT_THE_BANDS, SPECIAL_CYCLE_THE_BACKGROUND,
    SPECIAL_KEEP_THE_POSITION_ON_THE_BATTLEFIELD, SPECIAL_TALL_BANDS_OF_LIGHT, SPECIAL_RESTART_THE_WIPE,
    SPECIAL_CLEAR_THE_WAVE_LINES, SPECIAL_SHIFT_MARKED_LINES, SPECIAL_MOVE_THE_BAND_DOWN,
    SPECIAL_MOVE_THE_BAND_UP, SPECIAL_GIVE_THE_SIGNAL, SPECIAL_WHITEN_THE_SCENE, SPECIAL_HIDE_THE_ENEMIES,
    SPECIAL_WHITEN_THE_HIGH_SPRITE_PALETTES,
    SPECIAL_EFFECTS
} SpecialEffect;

/* Every parameter an operation of the language takes. A row says how the
 * command's operands decode into the ones its operation reads. */
#define SCREEN_PARAMETERS(X)                                                                       \
    /* flow and the scripts' variables */                                                         \
    X(uint16_t, to) X(uint8_t, back) X(uint8_t, times) X(bool, counting) X(uint8_t, variable)     \
    X(uint8_t, amount) X(bool, clear) X(uint8_t, pace)                                            \
    /* plain settings */                                                                          \
    X(uint8_t, value) X(uint8_t, setting) X(uint16_t, color) X(uint8_t, from) X(uint8_t, onto)   \
    X(SpecialEffect, special) X(uint16_t, address) X(uint8_t, poked) X(uint8_t, line)               \
    X(uint8_t, slot) X(uint8_t, set) X(uint8_t, by) X(bool, at_position) X(uint8_t, sound)       \
    X(uint8_t, effect)                                                                            \
    /* the layer */                                                                               \
    X(uint8_t, direction) X(uint8_t, distance) X(uint8_t, position) X(uint8_t, which)            \
    X(bool, to_swing) X(uint8_t, frames) X(uint8_t, orbit) X(uint8_t, spread)                    \
    X(bool, new_drifts) X(uint8_t, seed) X(uint8_t, drift) X(bool, drifts_forward)                \
    X(uint8_t, across) X(uint8_t, down) X(bool, stop)                                             \
    /* waves */                                                                                   \
    X(uint8_t, amplitude) X(uint8_t, every) X(uint8_t, speed) X(bool, fine)                      \
    /* circles and triangles */                                                                   \
    X(uint8_t, circle) X(CircleKind, circle_kind) X(uint8_t, shape) X(uint8_t, kind)               \
    X(uint8_t, x) X(uint8_t, y)                                                                   \
    X(uint8_t, radius) X(uint8_t, depth) X(uint8_t, size) X(uint8_t, scale_x)                    \
    X(uint8_t, scale_y) X(uint8_t, angle) X(uint8_t, limit) X(uint8_t, model) X(bool, spins)     \
    X(bool, own_depth)                                                                            \
    /* a window cut through the middle, bands between positions */                                \
    X(uint8_t, window) X(bool, sets_angle) X(uint8_t, turn) X(uint8_t, stray_bits)                \
    X(uint8_t, quad) X(uint8_t, pair) X(uint8_t, aim) X(uint8_t, kept_pair)                       \
    /* the palette and the screen */                                                              \
    X(uint8_t, mode) X(uint8_t, first) X(uint8_t, count) X(uint8_t, level) X(uint8_t, target)    \
    X(uint8_t, step) X(uint8_t, delay) X(uint8_t, period) X(uint8_t, layers) X(bool, smaller)    \
    X(uint8_t, how) X(uint8_t, fill)

/* ... and the ones that are three bytes: a triangle's three corners' or
 * axes', and the shake's settings for each axis (its amplitude, the one it
 * settles to and its rate). */
#define SCREEN_TRIPLE_PARAMETERS(X)                                                               \
    X(angles) X(radii) X(turns) X(sways) X(shake_across) X(shake_down)

/* A command, decoded by its row: the channel it runs on, where its operands
 * are, and the parameters its operation takes. */
typedef struct ScreenCommand {
    ScreenChannel channel;
    uint16_t at;        /* the place of its first operand */
#define SCREEN_PARAMETER_FIELD(type, name) type name;
    SCREEN_PARAMETERS(SCREEN_PARAMETER_FIELD)
#undef SCREEN_PARAMETER_FIELD
#define SCREEN_TRIPLE_FIELD(name) ScreenTriple name;
    SCREEN_TRIPLE_PARAMETERS(SCREEN_TRIPLE_FIELD)
#undef SCREEN_TRIPLE_FIELD
} ScreenCommand;

typedef ScreenFlow (*ScreenOperation)(GameState *g, const ScreenCommand *command);

/* SCREEN_OPERATION(op, parameter...): the form of the operation `op` the
 * table calls, which hands it the parameters it names, in that order, after
 * the game's state (`channel` and `at` among them). OPERATION(op) names it in
 * a row. */
#define SCREEN_TAKE_(name) command->name
#define SCREEN_TAKE_1(a) , SCREEN_TAKE_(a)
#define SCREEN_TAKE_2(a, b) SCREEN_TAKE_1(a), SCREEN_TAKE_(b)
#define SCREEN_TAKE_3(a, b, c) SCREEN_TAKE_2(a, b), SCREEN_TAKE_(c)
#define SCREEN_TAKE_4(a, b, c, d) SCREEN_TAKE_3(a, b, c), SCREEN_TAKE_(d)
#define SCREEN_TAKE_5(a, b, c, d, e) SCREEN_TAKE_4(a, b, c, d), SCREEN_TAKE_(e)
#define SCREEN_TAKE_6(a, b, c, d, e, f) SCREEN_TAKE_5(a, b, c, d, e), SCREEN_TAKE_(f)
#define SCREEN_TAKE_7(a, b, c, d, e, f, h) SCREEN_TAKE_6(a, b, c, d, e, f), SCREEN_TAKE_(h)
#define SCREEN_TAKE_8(a, b, c, d, e, f, h, i) SCREEN_TAKE_7(a, b, c, d, e, f, h), SCREEN_TAKE_(i)
#define SCREEN_TAKE_9(a, b, c, d, e, f, h, i, j) SCREEN_TAKE_8(a, b, c, d, e, f, h, i), SCREEN_TAKE_(j)
#define SCREEN_COUNT_(_1, _2, _3, _4, _5, _6, _7, _8, _9, n, ...) n
#define SCREEN_COUNT(...) SCREEN_COUNT_(__VA_ARGS__, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define SCREEN_GLUE_(a, b) a##b
#define SCREEN_GLUE(a, b) SCREEN_GLUE_(a, b)
#define SCREEN_OPERATION(op, ...)                                                                 \
    static ScreenFlow op##_command(GameState *g, const ScreenCommand *command)                    \
    {                                                                                             \
        return op(g SCREEN_GLUE(SCREEN_TAKE_, SCREEN_COUNT(__VA_ARGS__))(__VA_ARGS__));           \
    }
#define SCREEN_OPERATION_TAKING_NOTHING(op)                                                       \
    static ScreenFlow op##_command(GameState *g, const ScreenCommand *command)                    \
    {                                                                                             \
        (void)command;                                                                            \
        return op(g);                                                                             \
    }
#define OPERATION(op) .operation = op##_command

#define BRIDGE(b) .bridge = (b)

/* ---- How a row decodes its operands ----------------------------------------------------------- */

typedef enum {
    ARG_NONE,
    ARG_CONSTANT,     /* the row's own value */
    ARG_BYTE,         /* operand n */
    ARG_WORD,         /* operands n and n + 1, the low byte first */
    ARG_TRIPLE,       /* operands n to n + 2 */
    ARG_LOW_BIT,      /* bit 0 of operand n */
    ARG_TOP_BIT,      /* bit 7 */
    ARG_LOW_SEVEN,    /* bits 0-6 */
    ARG_LOW_FIVE,     /* bits 0-4 */
    ARG_TOP_THREE,    /* bits 5-7, down to 0-7 */
    ARG_LOW_NIBBLE,   /* bits 0-3 */
    ARG_HIGH_NIBBLE,  /* bits 4-7, down to 0-15 */
    ARG_MASKED,       /* the bits of the row's mask, in place */
    ARG_BIT,          /* the row's bit of operand n, down to 0-1 */
} ScreenArgKind;

typedef struct {
    uint8_t kind;
    uint8_t operand;
    uint16_t value;
} ScreenArg;

#define CONSTANT(v) { ARG_CONSTANT, 0, (v) }
#define BYTE(n) { ARG_BYTE, (n), 0 }
#define WORD(n) { ARG_WORD, (n), 0 }
#define TRIPLE(n) { ARG_TRIPLE, (n), 0 }
#define LOW_BIT(n) { ARG_LOW_BIT, (n), 0 }
#define TOP_BIT(n) { ARG_TOP_BIT, (n), 0 }
#define LOW_SEVEN(n) { ARG_LOW_SEVEN, (n), 0 }
#define LOW_FIVE(n) { ARG_LOW_FIVE, (n), 0 }
#define TOP_THREE(n) { ARG_TOP_THREE, (n), 0 }
#define LOW_NIBBLE(n) { ARG_LOW_NIBBLE, (n), 0 }
#define HIGH_NIBBLE(n) { ARG_HIGH_NIBBLE, (n), 0 }
#define MASKED(n, mask) { ARG_MASKED, (n), (mask) }
#define BIT(n, bit) { ARG_BIT, (n), (bit) }

/* What an operation needs of the state to run as the original does; where a
 * row's isn't met, the original's handler runs the command. */
typedef bool (*ScreenNeeds)(const GameState *g, const ScreenCommand *command);

/* A command whose work is still the original's code: the runner moves the
 * channel on past it, then calls this with the machine; false where the
 * original's code carried on. */
typedef bool (*ScreenBridge)(Snes *s, const ScreenCommand *command);

/* A row: the command byte, the operation (or the bridge), what it needs, the
 * operand bytes it takes, and how each parameter it reads decodes. (The
 * original's handler, whose time the operation takes, and which runs the
 * command where the row has neither or its needs aren't met, is
 * screen_script.c's.) */
typedef struct {
    uint8_t opcode;
    ScreenOperation operation;
    ScreenBridge bridge;
    ScreenNeeds needs;
    uint8_t length;
#define SCREEN_ARG_FIELD(type, name) ScreenArg name;
    SCREEN_PARAMETERS(SCREEN_ARG_FIELD)
#undef SCREEN_ARG_FIELD
#define SCREEN_TRIPLE_ARG_FIELD(name) ScreenArg name;
    SCREEN_TRIPLE_PARAMETERS(SCREEN_TRIPLE_ARG_FIELD)
#undef SCREEN_TRIPLE_ARG_FIELD
} ScreenOpcode;

typedef struct {
    const ScreenOpcode *rows;
    unsigned count;
} ScreenOpcodes;

#define SCREEN_OPCODES(name, ...)                                     \
    static const ScreenOpcode name##_rows[] = { __VA_ARGS__ };        \
    const ScreenOpcodes name = { name##_rows, sizeof name##_rows / sizeof name##_rows[0] }

/* The files' rows. */
extern const ScreenOpcodes screen_flow_opcodes, screen_layer_opcodes, screen_shape_opcodes,
    screen_setting_opcodes, screen_circle_opcodes, screen_special_opcodes, screen_wave_opcodes, screen_window_opcodes, screen_motion_opcodes,
    screen_palette_opcodes, screen_cut_opcodes, screen_between_opcodes, screen_beam_opcodes;

/* The row of a command byte, or none past the language's last. */
const ScreenOpcode *screen_opcode(uint8_t opcode);

/* ---- Places in the script ------------------------------------------------------------------- */

/* A place in the running channel's script, within its bank. */
static inline uint16_t screen_place(const GameState *g) { return (uint16_t)battle_screen_script(g); }

/* A byte of the running channel's script, at a place in its bank. */
static inline uint8_t screen_script_byte_at(const GameState *g, uint16_t at)
{
    return battle_screen_script_byte(g, (battle_screen_script(g) & 0xFF0000u) | at);
}

/* ---- The running channel ------------------------------------------------------------------- */

/* The running channel, as the dispatch leaves it for the commands. */
static inline ScreenChannel screen_running_channel(const GameState *g)
{
    return (ScreenChannel)(battle_screen_channel(g) / SCREEN_CHANNEL_RECORD_BYTES);
}

/* The two triangles and the two circles are each one of two. */
enum { SCREEN_SHAPES = 2, SCREEN_CIRCLES = 2 };

/* The screen scripts' frame ($CD:1609), as C: false where it went no
 * further. */
bool battle_screen_scripts_run(GameState *g, Platform *pf);

#endif
