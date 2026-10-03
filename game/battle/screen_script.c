/* Battle: the screen script channels, and the commands that steer them.
 *
 * While a battle effect's screen scripts run (battle_screen_scripts_running),
 * the frame work steps each running channel at its own pace
 * (battle_run_screen_scripts): when its countdown comes round, the channel
 * runs its script's commands until a step's end, a frame of the effect's
 * graphics or the script's end (battle/screen_script.h). When both channels
 * have ended, the scripts are over, and the frame wait puts the battlefield
 * back. While they run, BG3 follows channel 0's layer, unless the battlefield
 * is one whose BG3 stays still.
 *
 * A command is run through its row (battle/screen_script.h): decoded once,
 * its operation called with the parameters it takes, timed as the original's
 * handler, and the channel moved on as the operation says. A command whose row
 * has no operation, or whose operation's needs the state doesn't meet, runs
 * as the original's handler, called as the original's dispatch calls it. The
 * commands here are the ones that steer the channel: its pace, loops, calls
 * and jumps, the waits, and the scripts' variables. The others are in the
 * files of what they work on (screen_layers.c, screen_shapes.c,
 * screen_settings.c, screen_waves.c, and the window, motion and palette
 * effects of the D1 bank in screen_window.c, screen_motion.c and
 * screen_palette.c).
 *
 * While a command runs, battle_screen_script is its place: at its first
 * operand, and at its last byte once it is done, the channel stepping on from
 * there. The window, motion and palette effects read their operands through
 * it, and the original's handlers do. */
#include "battle/screen_script.h"

#include "battle/bridge.h"
#include "battle/frame.h"
#include "original_effects.h"
#include "platform/input.h"
#include "port/native.h"

/* ---- The language ----------------------------------------------------------------------- */

enum { STEP_ENDS = 0x7F, FIRST_COMMAND = 0x80, LAST_COMMAND = 0xF9, SCRIPT_ENDS = 0xFF, COMMANDS = LAST_COMMAND - FIRST_COMMAND + 1 };

/* The original's handlers, by command byte from $80: its dispatch table
 * ($CD:16B5), whose time each command takes, and which runs a command that
 * has no operation here. */
static const uint32_t handlers[COMMANDS] = {
    0xCD2A4A, 0xCD2DCB, 0xCD2DD6, 0xCD2EA2, 0xCD2EB5, 0xCD2EC8, 0xCD2EDE, 0xCD2687, 0xCD29DA, 0xCD2FBC,
    0xCD2FED, 0xCD2FB7, 0xCD2958, 0xCD2999, 0xCD29E3, 0xCD253B, 0xCD2EF7, 0xCD2F4E, 0xCD2F61, 0xCD2F6E,
    0xCD2F8B, 0xCD2FAB, 0xCD2FB1, 0xCD30BE, 0xCD3092, 0xCD30B8, 0xCD308C, 0xCD3083, 0xCD303B, 0xCD3032,
    0xCD3029, 0xCD3020, 0xCD3017, 0xCD2952, 0xCD278E, 0xCD2717, 0xCD26E0, 0xCD2690, 0xCD26C0, 0xCD2660,
    0xCD27CA, 0xCD2518, 0xCD1C32, 0xCD24F3, 0xCD2460, 0xCD21C0, 0xCD2173, 0xCD20F3, 0xCD20ED, 0xCD20D8,
    0xCD20D3, 0xCD1FC8, 0xCD1FA0, 0xCD1F53, 0xCD1F53, 0xCD1F4D, 0xCD1F47, 0xCD1F3C, 0xCD1E91, 0xCD1E7C,
    0xCD1E45, 0xCD1E36, 0xCD1E27, 0xCD1E07, 0xCD1D98, 0xCD1D93, 0xCD1D56, 0xCD1D4D, 0xCD1C7B, 0xCD1C63,
    0xCD1BCE, 0xCD1BAB, 0xCD1B9F, 0xCD1B78, 0xCD1B6D, 0xCD1B5E, 0xCD1B1F, 0xCD1AEB, 0xCD1AE6, 0xCD1AE1,
    0xCD1ACB, 0xCD1ABE, 0xCD1AB2, 0xCD1A9C, 0xCD1A8B, 0xCD1A7A, 0xCD1A66, 0xCD19E5, 0xCD19C2, 0xCD19B1,
    0xCD1937, 0xCD1932, 0xCD1923, 0xCD191E, 0xCD1918, 0xCD1913, 0xCD190D, 0xCD1B9A, 0xCD1907, 0xCD18DF,
    0xCD18CF, 0xCD18C8, 0xCD18C2, 0xCD187E, 0xCD1874, 0xCD1879, 0xCD1864, 0xCD185F, 0xCD185A, 0xCD1855,
    0xCD1850, 0xCD1834, 0xCD182A, 0xCD1813, 0xCD180A, 0xCD184B, 0xCD1805, 0xCD17FF, 0xCD17E5, 0xCD17DF,
    0xCD17BE, 0xCD17A9,
};

static uint32_t handler_of(uint8_t opcode) { return handlers[opcode - FIRST_COMMAND]; }

static const ScreenOpcodes *const every_file[] = {
    &screen_flow_opcodes,    &screen_layer_opcodes,  &screen_shape_opcodes,  &screen_setting_opcodes,
    &screen_wave_opcodes,    &screen_window_opcodes, &screen_motion_opcodes, &screen_palette_opcodes,
};

const ScreenOpcode *screen_opcode(uint8_t opcode)
{
    if (opcode < FIRST_COMMAND || opcode > LAST_COMMAND)
        return NULL;
    for (unsigned f = 0; f < sizeof every_file / sizeof every_file[0]; f++)
        for (unsigned r = 0; r < every_file[f]->count; r++)
            if (every_file[f]->rows[r].opcode == opcode)
                return &every_file[f]->rows[r];
    return NULL;
}

/* ---- Decoding a command ---------------------------------------------------------------- */

static uint8_t operand(const GameState *g, uint16_t at, unsigned n) { return screen_script_byte_at(g, (uint16_t)(at + n)); }

static uint16_t decoded(const GameState *g, uint16_t at, ScreenArg a)
{
    uint8_t byte = a.kind == ARG_CONSTANT || a.kind == ARG_NONE ? 0 : operand(g, at, a.operand);
    switch ((ScreenArgKind)a.kind) {
    case ARG_NONE: return 0;
    case ARG_CONSTANT: return a.value;
    case ARG_BYTE: return byte;
    case ARG_WORD: return (uint16_t)(byte | operand(g, at, a.operand + 1u) << 8);
    case ARG_TRIPLE: return 0;
    case ARG_LOW_BIT: return byte & 0x01;
    case ARG_TOP_BIT: return byte >> 7;
    case ARG_LOW_SEVEN: return byte & 0x7F;
    case ARG_LOW_FIVE: return byte & 0x1F;
    case ARG_TOP_THREE: return byte >> 5;
    case ARG_LOW_NIBBLE: return byte & 0x0F;
    case ARG_HIGH_NIBBLE: return byte >> 4;
    case ARG_MASKED: return byte & a.value;
    }
    return 0;
}

static ScreenTriple decoded_triple(const GameState *g, uint16_t at, ScreenArg a)
{
    ScreenTriple t = { { 0, 0, 0 } };
    if (a.kind == ARG_TRIPLE)
        for (unsigned i = 0; i < 3; i++)
            t.of[i] = operand(g, at, a.operand + i);
    return t;
}

static ScreenCommand decode(const GameState *g, const ScreenOpcode *row, ScreenChannel channel, uint16_t at)
{
    ScreenCommand c = { 0 };
    c.channel = channel;
    c.at = at;
#define SCREEN_DECODE_FIELD(type, name) c.name = (type)decoded(g, at, row->name);
    SCREEN_PARAMETERS(SCREEN_DECODE_FIELD)
#undef SCREEN_DECODE_FIELD
#define SCREEN_DECODE_TRIPLE(name) c.name = decoded_triple(g, at, row->name);
    SCREEN_TRIPLE_PARAMETERS(SCREEN_DECODE_TRIPLE)
#undef SCREEN_DECODE_TRIPLE
    return c;
}

/* Whether a command runs here: it has an operation, and the state meets its
 * needs. */
static bool runs_here(const GameState *g, const ScreenOpcode *row, const ScreenCommand *c)
{
    return row && row->operation && (!row->needs || row->needs(g, c));
}

/* Where the channel's place is left once a command is done: on its last
 * byte, from which the channel steps on. */
static void finish_command(GameState *g, const ScreenOpcode *row, const ScreenCommand *c, ScreenFlow flow)
{
    uint32_t bank = battle_screen_script(g) & 0xFF0000u;
    switch (flow.kind) {
    case SCREEN_ON:
    case SCREEN_ON_AND_DRAW: set_battle_screen_script(g, bank | (uint16_t)(c->at + row->length - 1)); break;
    case SCREEN_GO_TO: set_battle_screen_script(g, bank | (uint16_t)(flow.to - 1)); break;
    case SCREEN_MOVED: break;
    }
}

/* The original works a wait's distance back out as a word in its cell $45. */
ScreenFlow screen_wait_back(GameState *g, bool waiting, uint16_t at, uint8_t back)
{
    if (!waiting)
        return screen_on();
    ORIGINAL_EFFECT(screen_leave_word(g, 0x45, back));
    return screen_go_to((uint16_t)(at + 1 - back));
}

/* ---- Laying a triangle out for the rasterizer ------------------------------------------------ */

/* The original's rasterizer ($CD:367C) takes the corners in the battle page's
 * cells $12-$17, across then down, the window in battle_window_quad_index and
 * a cleared $20: its calling convention, which only this writes. */
enum { CORNER_CELLS = 0x12, RASTERIZER_FLAG_CELL = 0x20 };

void screen_lay_out_triangle(GameState *g, uint8_t window, const ScreenTriangle *t)
{
    set_battle_window_quad_index(g, window);
    for (unsigned corner = 0; corner < 3; corner++) {
        set_battle_low_scratch(g, CORNER_CELLS + 2 * corner, t->x[corner]);
        set_battle_low_scratch(g, CORNER_CELLS + 2 * corner + 1, t->y[corner]);
    }
    set_battle_low_scratch(g, RASTERIZER_FLAG_CELL, 0);
}

/* ---- The channel's pace, loops, calls and jumps ---------------------------------------------- */

// $CD:2DCB
/* Set how many frames apart the channel takes its steps, from the next. */
static ScreenFlow set_pace(GameState *g, ScreenChannel channel, uint8_t pace)
{
    set_battle_screen_channels_pace(g, channel, pace);
    set_battle_screen_channels_countdown(g, channel, pace);
    return screen_on();
}
SCREEN_OPERATION(set_pace, channel, pace)

// $CD:2EA2, $CD:2EC8
/* Start a loop that runs `times` times through to its end. The counting loop
 * also shows the frames it passes one further on each time round, from the
 * frames as they are numbered. */
static ScreenFlow begin_loop(GameState *g, ScreenChannel channel, uint16_t at, uint8_t times, bool counting)
{
    set_battle_screen_loop_count(g, channel, (uint8_t)(times + 1));
    set_battle_screen_loop_place(g, channel, at);
    if (counting)
        set_battle_screen_channels_frame_offset(g, channel, 0);
    return screen_on();
}
SCREEN_OPERATION(begin_loop, channel, at, times, counting)

// $CD:2EB5, $CD:2EDE
/* The end of a loop: back to its start until it has run its times. */
static ScreenFlow end_loop(GameState *g, ScreenChannel channel, bool counting)
{
    uint8_t left = (uint8_t)(battle_screen_loop_count(g, channel) - 1);
    set_battle_screen_loop_count(g, channel, left);
    uint8_t offset = battle_screen_channels_frame_offset(g, channel);
    if (left == 0) {
        if (counting)
            set_battle_screen_channels_frame_offset(g, channel, 0);
        return screen_on();
    }
    if (counting)
        set_battle_screen_channels_frame_offset(g, channel, (uint8_t)(offset + 1));
    return screen_go_to((uint16_t)(battle_screen_loop_place(g, channel) + 1));
}
SCREEN_OPERATION(end_loop, channel, counting)

// $CD:2F4E
/* Call a part of the script, coming back after this command. */
static ScreenFlow call(GameState *g, ScreenChannel channel, uint16_t at, uint16_t to)
{
    set_battle_screen_return_place(g, channel, (uint16_t)(at + 1));
    return screen_go_to(to);
}
SCREEN_OPERATION(call, channel, at, to)

// $CD:2F61
/* Come back from a call. */
static ScreenFlow come_back(GameState *g, ScreenChannel channel)
{
    return screen_go_to((uint16_t)(battle_screen_return_place(g, channel) + 1));
}
SCREEN_OPERATION(come_back, channel)

// $CD:1B6D
static ScreenFlow jump(GameState *g, uint16_t to)
{
    (void)g;
    return screen_go_to(to);
}
SCREEN_OPERATION(jump, to)

/* The battle's rule ($E1:DE00) that some fights bring in, whose number its
 * low seven bits hold. A script takes another way in the fight that has the
 * first. */
enum { RULE_NUMBER = 0x7F, THE_FIRST_RULE = 1 };

// $CD:1B5E
static ScreenFlow jump_in_the_first_rule(GameState *g, uint16_t to)
{
    return (battle_enemy_rule(g) & RULE_NUMBER) == THE_FIRST_RULE ? screen_go_to(to) : screen_on();
}
SCREEN_OPERATION(jump_in_the_first_rule, to)

/* ---- The scripts' variables ------------------------------------------------------------------ */

/* The variables the animation scripts share with the screen scripts
 * (battle_script_variables), named by a byte: a number past the 27 reaches
 * the work RAM after them, as the original's indexed access does. */
static uint8_t variable_value(const GameState *g, uint8_t variable)
{
    return battle_script_variables_unchecked(g, variable);
}

static void set_variable_value(GameState *g, uint8_t variable, uint8_t v)
{
    set_battle_script_variables_unchecked(g, variable, v);
}

/* A switch: the script follows its variable's low or high nibble through a
 * list of places that comes after the command, the nibble picking one. */
enum { SWITCH_PLACE_BYTES = 2 };

// $CD:1ACB (low nibble), $CD:1ABE (high nibble)
static ScreenFlow switch_on_variable(GameState *g, uint16_t at, uint8_t variable, uint8_t which)
{
    uint8_t v = variable_value(g, variable);
    uint8_t choice = which ? (uint8_t)(v >> 4) : (uint8_t)(v & 0x0F);
    uint16_t entry = (uint16_t)(at + 1 + SWITCH_PLACE_BYTES * choice);
    return screen_go_to((uint16_t)(screen_script_byte_at(g, entry) | screen_script_byte_at(g, (uint16_t)(entry + 1)) << 8));
}
SCREEN_OPERATION(switch_on_variable, at, variable, which)

// $CD:2518
/* Wait for a variable to be set. */
static ScreenFlow wait_for_variable(GameState *g, uint16_t at, uint8_t variable, uint8_t back)
{
    return screen_wait_back(g, variable_value(g, variable) == 0, at, back);
}
SCREEN_OPERATION(wait_for_variable, at, variable, back)

// $CD:19C2
/* Add to a variable, or clear it. */
static ScreenFlow change_variable(GameState *g, uint8_t variable, bool clear, uint8_t amount)
{
    if (clear) {
        set_variable_value(g, variable, 0);
        return screen_on();
    }
    set_variable_value(g, variable, (uint8_t)(variable_value(g, variable) + amount));
    ORIGINAL_EFFECT(screen_leave_cell(g, 0x45, amount));
    return screen_on();
}
SCREEN_OPERATION(change_variable, variable, clear, amount)

// $CD:18C8, $CD:1D4D (variable 15)
static ScreenFlow count_variable_up(GameState *g, uint8_t variable)
{
    set_variable_value(g, variable, (uint8_t)(variable_value(g, variable) + 1));
    return screen_on();
}
SCREEN_OPERATION(count_variable_up, variable)

/* ---- Waiting on the rest of the battle -------------------------------------------------------- */

// $CD:1813
/* Wait while the effect's graphics a script asked for are still to load. */
static ScreenFlow wait_for_effect_graphics(GameState *g, uint16_t at, uint8_t back)
{
    return screen_wait_back(g, battle_effect_graphics_wanted(g) != 0, at, back);
}
SCREEN_OPERATION(wait_for_effect_graphics, at, back)

/* The battle's own palette animations, in slots 8-11. */
enum { FIRST_BATTLE_ANIMATION = 8, BATTLE_ANIMATIONS = 4 };

static bool battle_palettes_animating(const GameState *g)
{
    uint8_t kinds = 0;
    for (unsigned slot = FIRST_BATTLE_ANIMATION; slot < FIRST_BATTLE_ANIMATION + BATTLE_ANIMATIONS; slot++)
        kinds |= palette_animations_kind(g, slot);
    return kinds != 0;
}

// $CD:1E07
/* Wait while the battle's palette animations run. */
static ScreenFlow wait_for_palette_animations(GameState *g, uint16_t at, uint8_t back)
{
    return screen_wait_back(g, battle_palettes_animating(g), at, back);
}
SCREEN_OPERATION(wait_for_palette_animations, at, back)

// $CD:17E5
/* Wait for the signal the other channel's special effect gives (the flag
 * that also turns the HP gradient the other way), and take it down. */
static ScreenFlow wait_for_signal(GameState *g, uint16_t at, uint8_t back)
{
    bool waiting = battle_gradient_reversed(g) == 0;
    set_battle_gradient_reversed(g, 0);
    return screen_wait_back(g, waiting, at, back);
}
SCREEN_OPERATION(wait_for_signal, at, back)

SCREEN_OPCODES(screen_flow_opcodes,
    { 0x81, OPERATION(set_pace), .length = 1, .pace = BYTE(0) },
    { 0x83, OPERATION(begin_loop), .length = 1, .times = BYTE(0), .counting = CONSTANT(false) },
    { 0x84, OPERATION(end_loop), .length = 0, .counting = CONSTANT(false) },
    { 0x85, OPERATION(begin_loop), .length = 1, .times = BYTE(0), .counting = CONSTANT(true) },
    { 0x86, OPERATION(end_loop), .length = 0, .counting = CONSTANT(true) },
    { 0x91, OPERATION(call), .length = 2, .to = WORD(0) },
    { 0x92, OPERATION(come_back), .length = 0 },
    { 0xCA, OPERATION(jump), .length = 2, .to = WORD(0) },
    { 0xCB, OPERATION(jump_in_the_first_rule), .length = 2, .to = WORD(0) },
    { 0xD0, OPERATION(switch_on_variable), .length = 1, .variable = BYTE(0), .which = CONSTANT(0) },
    { 0xD1, OPERATION(switch_on_variable), .length = 1, .variable = BYTE(0), .which = CONSTANT(1) },
    { 0xA9, OPERATION(wait_for_variable), .length = 2, .variable = BYTE(0), .back = BYTE(1) },
    { 0xD8, OPERATION(change_variable), .length = 2, .variable = LOW_SEVEN(0), .clear = TOP_BIT(0), .amount = BYTE(1) },
    { 0xE5, OPERATION(count_variable_up), .length = 1, .variable = BYTE(0) },
    { 0xC3, OPERATION(count_variable_up), .length = 0, .variable = CONSTANT(15) },
    { 0xF1, OPERATION(wait_for_effect_graphics), .length = 1, .back = BYTE(0) },
    { 0xBF, OPERATION(wait_for_palette_animations), .length = 1, .back = BYTE(0) },
    { 0xF6, OPERATION(wait_for_signal), .length = 1, .back = BYTE(0) },
    /* The commands still the original's: the special effects ($80); drawing
       the circles ($8B, and $B5, $B6 and $B8, which draw other shapes); the
       triangles worked out from animation positions through the battle's
       angle routine ($CE:E7CF: $AC, $CC, $D9, $DA), and centred on their own
       corners ($AD, $B4); a triangle drawn as its corners are, through the
       rasterizer's own setup ($A1); the beam of light ($F4); the layer's
       random drift ($D7), which adds a byte the original leaves in its cells;
       the commands that call into bank $CE ($97, $B2); a wait for a line of
       the picture ($C1); and a poke into work RAM ($F9). */
    { .opcode = 0x80 }, { .opcode = 0x8B }, { .opcode = 0x97 }, { .opcode = 0xA1 }, { .opcode = 0xAC },
    { .opcode = 0xAD }, { .opcode = 0xB2 }, { .opcode = 0xB4 }, { .opcode = 0xB5 }, { .opcode = 0xB6 },
    { .opcode = 0xB8 }, { .opcode = 0xC1 }, { .opcode = 0xCC }, { .opcode = 0xD7 }, { .opcode = 0xD9 },
    { .opcode = 0xDA }, { .opcode = 0xF4 }, { .opcode = 0xF9 },
);

/* ---- A command ---------------------------------------------------------------------------- */

/* The registers the original's dispatch ($CD:16A6) calls a handler with: the
 * command byte doubled (its table's index) in A and X, the place in Y, and
 * the carry its doubling shifted out, which is always set; N and Z as the
 * place's step left them. */
static void enter_as_the_dispatch_does(Snes *s, uint8_t opcode)
{
    uint16_t index = (uint8_t)(opcode * 2);
    uint16_t place = screen_place(state_of(s));
    s->cpu.a = index;
    s->cpu.x = index;
    s->cpu.y = place;
    m_set_flag(s, CPU_FLAG_C, true);
    m_set_flag(s, CPU_FLAG_N, place & 0x8000);
    m_set_flag(s, CPU_FLAG_Z, place == 0);
}

/* What the sizing leaves of the controller, as the original read it. */
static void leave_the_controller(Snes *s)
{
    screen_leave_controller_in_sizing(state_of(s), in_read_low(m_platform(s), PF_AT(0xCD2162), 0));
}

/* Draw the triangle an operation laid out, as the original's handler does
 * at its end: the rasterizer returns where the handler would. */
static bool draw_the_triangle(Snes *s)
{
    ORIGINAL_EFFECT(leave_the_controller(s));
    return battle_call(s, 0xCD16AF, 0xCD367C, DC_A | DC_X | DC_Y | DC_FLAGS);
}

// $CD:16A6
/* Run a command: its operation, or the original's handler. False where the
 * original's code carried on from a call-out. */
static bool run_command(Snes *s, uint8_t opcode, ScreenChannel channel)
{
    GameState *g = state_of(s);
    set_battle_screen_channel(g, (uint16_t)(channel * SCREEN_CHANNEL_RECORD_BYTES));
    uint16_t at = (uint16_t)(screen_place(g) + 1);
    set_battle_screen_script(g, (battle_screen_script(g) & 0xFF0000u) | at);
    const ScreenOpcode *row = screen_opcode(opcode);
    ScreenCommand c = { 0 };
    if (row)
        c = decode(g, row, channel, at);
    if (!runs_here(g, row, &c)) {
        enter_as_the_dispatch_does(s, opcode);
        /* no handler reads the overflow (tools/v_readers.py), which the
           command before it left */
        return battle_call(s, 0xCD16AF, handler_of(opcode), DC_V);
    }
    ScreenFlow flow;
    NATIVE_TIME_AS(s, handler_of(opcode), flow = row->operation(g, &c));
    finish_command(g, row, &c, flow);
    s->cpu.a = 0;
    if (flow.kind == SCREEN_ON_AND_DRAW)
        return draw_the_triangle(s);
    return true;
}

/* ---- A channel's step --------------------------------------------------------------------- */

/* A frame of the effect's animated graphics: sixteen tiles, 512 bytes, from
 * $7E:D000 on, which the frame work sends to the VRAM row the scripts chose
 * (battle_block_upload_row) at the next frame. A channel shows the frames one
 * further on for each time round its counting loop has gone. */
enum { EFFECT_FRAMES_AT = 0xD000, FRAME_BYTES = 0x200 };

static void show_frame(GameState *g, ScreenChannel channel, uint8_t frame)
{
    uint8_t shown = (uint8_t)(frame + battle_screen_channels_frame_offset(g, channel));
    set_battle_block_upload_source(g, (uint16_t)(EFFECT_FRAMES_AT + shown * FRAME_BYTES));
    set_battle_block_upload_request(g, (uint8_t)(battle_block_upload_request(g) + 1));
}

/* The channel waits from its step's end to its next step. Its place moves
 * within its bank. */
static void wait_after(GameState *g, ScreenChannel channel, uint16_t at)
{
    uint32_t place = battle_screen_channels_place(g, channel);
    set_battle_screen_channels_place(g, channel, (place & 0xFF0000u) | (uint16_t)(at + 1));
}

/* What the channel's next byte is. */
typedef enum { NEXT_FRAME, NEXT_WAIT, NEXT_END, NEXT_COMMAND, NEXT_UNKNOWN } NextByte;

static NextByte next_byte_is(uint8_t byte)
{
    if (byte == SCRIPT_ENDS)
        return NEXT_END;
    if (byte > LAST_COMMAND)
        return NEXT_UNKNOWN;
    if (byte >= FIRST_COMMAND)
        return NEXT_COMMAND;
    return byte == STEP_ENDS ? NEXT_WAIT : NEXT_FRAME;
}

/* A byte past the language's last command sends the original's dispatch
 * past its table, into code that isn't a handler. No script of the game's
 * has one; the original's runner goes on from the byte, at the head of its
 * loop, as its own step would come to it. */
static void hand_the_byte_back(Snes *s, ScreenChannel channel)
{
    s->cpu.a = 0;
    s->cpu.x = (uint16_t)(channel * SCREEN_CHANNEL_RECORD_BYTES);
    m_stop_at_own(s, 0xCD165F, DC_AL | DC_Y | DC_N | DC_Z | DC_C);
}

/* The commands are run through the dispatch's entry as the original calls
 * it, with its return on the stack, so that a command's call-outs meet the
 * original's where it makes them. A command whose handler is the original's
 * may leave A's high byte set, which the original then takes into the next
 * frame's place in the graphics; the channel goes on as C only where it
 * didn't (no command the game's scripts use does), and otherwise the
 * original's runner goes on from the dispatch's return. */
static bool run_command_as_the_original_calls_it(Snes *s, uint8_t opcode, ScreenChannel channel)
{
    s->cpu.a = opcode;
    s->cpu.x = (uint16_t)(channel * SCREEN_CHANNEL_RECORD_BYTES);
    if (!battle_run(s, 0xCD1695, 0xCD16A6, battle_run_screen_command))
        return false;
    if (s->cpu.a & 0xFF00) {
        m_stop_at_own(s, 0xCD1698, DC_AL | DC_Y | DC_FLAGS);
        return false;
    }
    return true;
}

// $CD:1654
/* Take a channel's step: run its commands until it shows a frame, ends its
 * step or ends its script. False where the original's code carried on. */
static bool step_channel(Snes *s, ScreenChannel channel)
{
    GameState *g = state_of(s);
    set_battle_screen_script(g, battle_screen_channels_place(g, channel));
    for (;;) {
        uint16_t at = screen_place(g);
        uint8_t byte = screen_script_byte_at(g, at);
        switch (next_byte_is(byte)) {
        case NEXT_END: set_battle_screen_channels_running(g, channel, 0); return true;
        case NEXT_FRAME: show_frame(g, channel, byte); wait_after(g, channel, at); return true;
        case NEXT_WAIT: wait_after(g, channel, at); return true;
        case NEXT_UNKNOWN: hand_the_byte_back(s, channel); return false;
        case NEXT_COMMAND:
            if (!run_command_as_the_original_calls_it(s, byte, channel))
                return false;
            set_battle_screen_script(g, (battle_screen_script(g) & 0xFF0000u) | (uint16_t)(screen_place(g) + 1));
            break;
        }
    }
}

/* ---- The channels, each frame ------------------------------------------------------------- */

/* A few battlefields keep BG3 still while the screen scripts run; on the
 * others it follows channel 0's layer. */
static bool bg3_stays_still(const GameState *g)
{
    static const uint8_t still[] = { 0x86, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F };
    uint8_t background = (uint8_t)battle_background(g);
    for (unsigned i = 0; i < sizeof still; i++)
        if (background == still[i])
            return true;
    return false;
}

/* Whether a channel's step comes round this frame: its countdown runs out,
 * and starts again from its pace. */
static bool step_comes_round(GameState *g, ScreenChannel channel)
{
    uint8_t left = (uint8_t)(battle_screen_channels_countdown(g, channel) - 1);
    set_battle_screen_channels_countdown(g, channel, left);
    if (left != 0)
        return false;
    set_battle_screen_channels_countdown(g, channel, battle_screen_channels_pace(g, channel));
    return true;
}

/* The channel's step is its own routine in the original, called with the
 * count of ended channels on the stack, the channel's offset in X and its
 * pace in A, which the countdown was just set from; it runs
 * as that routine's entry, with its call-outs where the original makes them. */
static bool step_as_the_original_calls_it(Snes *s, ScreenChannel channel, uint16_t ended)
{
    m_push16(s, ended);
    s->cpu.a = battle_screen_channels_pace(state_of(s), channel);
    s->cpu.x = (uint16_t)(channel * SCREEN_CHANNEL_RECORD_BYTES);
    if (!battle_run(s, 0xCD1631, 0xCD1654, battle_run_screen_channel))
        return false;
    m_pull16(s);
    return true;
}

// $CD:1609
/* Run the screen scripts for a frame: step each running channel whose step
 * comes round, and when both have ended, end the scripts and ask for the
 * battlefield back. Nothing runs while the battlefield is being put back. */
static bool run_screen_scripts(Snes *s)
{
    GameState *g = state_of(s);
    if (battle_background_restore_request(g) || !battle_screen_scripts_running(g))
        return true;
    set_battle_scroll_mode(g, bg3_stays_still(g) ? BATTLE_SCROLL_STEADY : BATTLE_SCROLL_BG3_MOVING);
    uint16_t ended = 0;
    for (ScreenChannel channel = 0; channel < SCREEN_CHANNELS; channel++) {
        if (!battle_screen_channels_running(g, channel))
            ended++;
        else if (step_comes_round(g, channel) && !step_as_the_original_calls_it(s, channel, ended))
            return false;
    }
    if (ended == SCREEN_CHANNELS) {
        set_battle_screen_scripts_running(g, 0);
        set_battle_background_restore_request(g, (uint8_t)(battle_background_restore_request(g) + 1));
    }
    return true;
}

/* ---- The entries ------------------------------------------------------------------------ */

void battle_run_screen_scripts(Snes *s)
{
    if (run_screen_scripts(s))
        m_rts(s);
}

/* The channel's offset in X, which the original keeps on the stack while the
 * channel's commands run and hands back. */
void battle_run_screen_channel(Snes *s)
{
    uint16_t offset = s->cpu.x;
    m_push16(s, offset);
    if (!step_channel(s, (ScreenChannel)(offset / SCREEN_CHANNEL_RECORD_BYTES)))
        return;
    s->cpu.x = m_pull16(s);
    m_rts(s);
}

/* The command byte in A, the channel's offset in X; the channel's offset
 * comes back in X. */
void battle_run_screen_command(Snes *s)
{
    if (!run_command(s, (uint8_t)s->cpu.a, (ScreenChannel)(s->cpu.x / SCREEN_CHANNEL_RECORD_BYTES)))
        return;
    s->cpu.x = battle_screen_channel(state_of(s));
    m_rts(s);
}

/* A command whose operation is C, entered as its handler: the command found
 * from its table's index in X, its operands at the place. One that draws a
 * triangle goes on into the rasterizer, as the original's handler does. */
void battle_screen_command(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t opcode = (uint8_t)(FIRST_COMMAND + (uint8_t)s->cpu.x / 2);
    const ScreenOpcode *row = screen_opcode(opcode);
    ScreenCommand c = decode(g, row, screen_running_channel(g), screen_place(g));
    ScreenFlow flow = row->operation(g, &c);
    finish_command(g, row, &c, flow);
    s->cpu.a = 0;
    if (flow.kind == SCREEN_ON_AND_DRAW) {
        ORIGINAL_EFFECT(leave_the_controller(s));
        m_stop_at(s, 0xCD367C, DC_A | DC_X | DC_Y | DC_FLAGS);
        return;
    }
    m_rts(s);
}

/* The guards. Every entry runs on the battle's page with the data bank $7E
 * and A's high byte clear, as the original's runner leaves them; a channel is
 * one of the two; a command is one of the language's, and one with an
 * operation whose needs are met where it is entered as its handler, with its
 * byte doubled in X and the carry the doubling shifted out set (the jitter
 * takes it into its first pick). */
static bool on_a_channel(const GameState *g, uint16_t offset)
{
    (void)g;
    return offset % SCREEN_CHANNEL_RECORD_BYTES == 0 && offset / SCREEN_CHANNEL_RECORD_BYTES < SCREEN_CHANNELS;
}

bool battle_screen_channel_ready(Snes *s)
{
    return battle_on_page_a_high_clear(s) && on_a_channel(state_of(s), s->cpu.x);
}

bool battle_screen_dispatch_ready(Snes *s)
{
    uint8_t opcode = (uint8_t)s->cpu.a;
    return battle_on_page_a_high_clear(s) && on_a_channel(state_of(s), s->cpu.x) && opcode >= FIRST_COMMAND
           && opcode <= LAST_COMMAND;
}

bool battle_screen_command_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    uint16_t index = s->cpu.x;
    uint8_t opcode = (uint8_t)(FIRST_COMMAND + index / 2);
    const ScreenOpcode *row = index < 2 * COMMANDS && index % 2 == 0 ? screen_opcode(opcode) : NULL;
    bool ready = false;
    if (row && on_a_channel(g, battle_screen_channel(g))) {
        ScreenCommand c = decode(g, row, screen_running_channel(g), screen_place(g));
        ready = runs_here(g, row, &c);
    }
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && m_flag(s, CPU_FLAG_C) && ready;
}
