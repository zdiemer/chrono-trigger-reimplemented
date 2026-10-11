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
 * handler, and the channel moved on as the operation says. A command whose
 * operation's needs the state doesn't meet runs as the original's handler,
 * called as the original's dispatch calls it. The commands here are the ones
 * that steer the channel: its pace, loops, calls and jumps, the waits, the
 * scripts' variables and a poke into work RAM. The others are in the files of
 * what they work on (screen_layers.c, screen_shapes.c, screen_circles.c,
 * screen_settings.c, screen_waves.c, screen_specials.c, and the window,
 * motion and palette effects of the D1 bank in screen_window.c,
 * screen_motion.c and screen_palette.c).
 *
 * While a command runs, battle_screen_script is its place: at its first
 * operand, and at its last byte once it is done, the channel stepping on from
 * there. The window, motion and palette effects read their operands through
 * it, and the original's handlers do. */
#include "battle/screen_script.h"

#include "battle/bridge.h"
#include "battle/frame.h"
#include "charge.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "platform/input.h"
#include "port/native.h"
#include "wram.h"

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
    &screen_flow_opcodes,    &screen_layer_opcodes,  &screen_shape_opcodes,  &screen_circle_opcodes,  &screen_special_opcodes, &screen_setting_opcodes,
    &screen_wave_opcodes,    &screen_window_opcodes, &screen_motion_opcodes, &screen_palette_opcodes,
    &screen_cut_opcodes,     &screen_between_opcodes, &screen_beam_opcodes,
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
    case ARG_BIT: return (byte >> a.value) & 1;
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

/* Filling window 1 ($88's fills from the fourth) returns A's high byte set,
 * which the original's runner takes into the place of the next frame of the
 * effect's graphics, should one follow: no script of the game's asks for it,
 * and the original's handler runs it, so that the runner's machine is the
 * original's. */
enum { FILL_WINDOWS = 0x88, FILL_WINDOW_1 = 3 };

static bool leaves_a_high_byte(const ScreenCommand *c, uint8_t opcode) { return opcode == FILL_WINDOWS && c->fill >= FILL_WINDOW_1; }

/* Whether a command runs here: it has an operation (or a bridge), the state
 * meets its needs, and the original's runner reads nothing of the machine
 * after it. */
static bool runs_here(const GameState *g, const ScreenOpcode *row, const ScreenCommand *c)
{
    return row && (row->operation || row->bridge) && (!row->needs || row->needs(g, c))
           && !leaves_a_high_byte(c, row->opcode);
}

/* Where the channel's place is left once a command is done: on its last
 * byte, from which the channel steps on. */
static void finish_command(GameState *g, const ScreenOpcode *row, const ScreenCommand *c, ScreenFlow flow)
{
    uint32_t bank = battle_screen_script(g) & 0xFF0000u;
    switch (flow.kind) {
    case SCREEN_ON: set_battle_screen_script(g, bank | (uint16_t)(c->at + row->length - 1)); break;
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

/* The battle's rule ($E1:DE00) that some fights bring in: a script takes
 * another way in the fight that has the first. */

// $CD:1B5E
static ScreenFlow jump_in_the_first_rule(GameState *g, uint16_t to)
{
    return (battle_enemy_rule(g) & BATTLE_RULE_NUMBER) == BATTLE_THE_FIRST_RULE ? screen_go_to(to) : screen_on();
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

/* ---- Work RAM -------------------------------------------------------------------------------- */

/* Work RAM's first bank, where the scripts' pokes land, and the cells that
 * hold the running channel's place, which the runner sets once the command is
 * done (a poke there would be the runner's to undo). */
enum { WORK_RAM_BANK = 0x7E, PLACE_CELLS_FROM = 0x40, PLACE_CELLS_TO = 0x42 };

// $CD:17A9
/* Write a byte into work RAM at an address the script gives. The original
 * steps on past the byte and the address's low byte only, so the address's
 * high byte is the next the channel runs (no script of the game's has the
 * command). */
static ScreenFlow poke(GameState *g, uint8_t poked, uint16_t address)
{
    wram_write8(g, (uint32_t)WORK_RAM_BANK << 16 | address, poked);
    return screen_on();
}
SCREEN_OPERATION(poke, poked, address)

static bool pokes_past_the_place(const GameState *g, const ScreenCommand *c)
{
    (void)g;
    return c->address < PLACE_CELLS_FROM || c->address > PLACE_CELLS_TO;
}

/* ---- Waiting for the beam --------------------------------------------------------------------- */

/* The original's wait for a line of the picture, and the dispatch's call of
 * the handler it stands in for. */
enum { WAIT_FOR_LINE = 0xCD3B82, DISPATCH_CALL = 0xCD16AF };

// $CD:1D93
/* Wait for the picture to reach a line before the channel goes on (the
 * timing service's wait, which a host drawing a frame at a time answers at
 * once). It works nothing out. No script of the game's has the command. */
static bool wait_for_line(Snes *s, const ScreenCommand *c)
{
    return battle_wait_line(s, DISPATCH_CALL, WAIT_FOR_LINE, DC_X | DC_Y | DC_FLAGS, c->line);
}

/* ---- Waiting on the rest of the battle -------------------------------------------------------- */

// $CD:1813
/* Wait while the effect's graphics a script asked for are still to load. */
static ScreenFlow wait_for_effect_graphics(GameState *g, uint16_t at, uint8_t back)
{
    return screen_wait_back(g, battle_effect_graphics_wanted(g) != 0, at, back);
}
SCREEN_OPERATION(wait_for_effect_graphics, at, back)

static bool battle_palettes_animating(const GameState *g)
{
    uint8_t kinds = 0;
    for (unsigned slot = BATTLE_FIRST_OWN_ANIMATION; slot < BATTLE_FIRST_OWN_ANIMATION + BATTLE_OWN_ANIMATIONS; slot++)
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
    { 0xF9, OPERATION(poke), .needs = pokes_past_the_place, .length = 2, .poked = BYTE(0), .address = WORD(1) },
    { 0xC1, BRIDGE(wait_for_line), .length = 1, .line = BYTE(0) },
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

/* What a triangle's sizing ($CD:211B) leaves that only the machine knows:
 * the last corner down's word in the cell $4B, as the sized corner, but where
 * the size moves the corner out from the center (or leaves it on it) the
 * original reads its product as a word, and the controller's low byte (as the
 * console last read it) comes along as the word's high byte. Every command
 * that draws a triangle at its size leaves it. */
static void leave_the_sizing(Snes *s, const ScreenCommand *c)
{
    GameState *g = state_of(s);
    uint8_t shape = c->shape & 1, size = battle_shape_size(g, shape);
    uint8_t down = battle_shape_center_y(g, shape);
    uint8_t last = battle_shape_corners(g, (2 * 2 + 1) * 2 + shape);
    uint8_t moved = (uint8_t)((uint8_t)(last >= down ? last - down : down - last) * size >> 8);
    uint16_t word = last >= down
                        ? (uint16_t)((in_read_low(m_platform(s), PF_AT(0xCD2162), 0) << 8 | moved) + down)
                        : (uint16_t)(down - moved);
    screen_leave_word(g, 0x4B, word);
}

static bool draws_a_sized_triangle(uint8_t opcode)
{
    static const uint8_t drawing[] = { 0xAE, 0xBA, 0xBB, 0xBE };
    for (unsigned i = 0; i < sizeof drawing; i++)
        if (drawing[i] == opcode)
            return true;
    return false;
}

/* What the original's handler leaves of the machine beside what its
 * operation does. */
static void leave_the_machine(Snes *s, uint8_t opcode, const ScreenCommand *c)
{
    if (draws_a_sized_triangle(opcode))
        leave_the_sizing(s, c);
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
    if (row->bridge) {
        finish_command(g, row, &c, screen_on());
        return row->bridge(s, &c);
    }
    ScreenFlow flow;
    NATIVE_TIME_AS(s, handler_of(opcode), flow = row->operation(g, &c));
    finish_command(g, row, &c, flow);
    ORIGINAL_EFFECT(leave_the_machine(s, opcode, &c));
    s->cpu.a = 0;
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

/* The swing's kept word ($8C, $8D) names one of four words; the original's
 * handler jumps through a table of four by the operand's low seven bits, and
 * past it into code that isn't a handler. No script of the game's has either
 * command (their rows' needs refuse the rest); a step that comes to one
 * naming no word is the original runner's from the command on, as its
 * dispatch is called with the byte in A. */
enum { KEEP_SWING_WORD = 0x8C, KEEP_WORD_ASIDE = 0x8D, SWING_WORDS = 4 };

static bool runs_past_its_table(const GameState *g, uint8_t byte, uint16_t at)
{
    return (byte == KEEP_SWING_WORD || byte == KEEP_WORD_ASIDE)
           && (screen_script_byte_at(g, (uint16_t)(at + 1)) & 0x7F) >= SWING_WORDS;
}

static void hand_the_command_back(Snes *s, uint8_t byte, ScreenChannel channel)
{
    s->cpu.a = byte;
    s->cpu.x = (uint16_t)(channel * SCREEN_CHANNEL_RECORD_BYTES);
    m_stop_at_own(s, 0xCD1695, DC_Y | DC_N | DC_V | DC_Z | DC_C);
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
            if (runs_past_its_table(g, byte, at)) {
                hand_the_command_back(s, byte, channel);
                return false;
            }
            if (!run_command_as_the_original_calls_it(s, byte, channel))
                return false;
            set_battle_screen_script(g, (battle_screen_script(g) & 0xFF0000u) | (uint16_t)(screen_place(g) + 1));
            break;
        }
    }
}

/* ---- The channels, each frame ------------------------------------------------------------- */

/* A few battlefields keep BG3 still while the screen scripts run; on the
 * others it follows channel 0's layer. They are also the ones whose BG3 isn't
 * a layer of their own (battlefield_change.c). */
bool battle_keeps_bg3_still(uint16_t battlefield)
{
    static const uint8_t still[] = { 0x86, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F };
    for (unsigned i = 0; i < sizeof still; i++)
        if ((uint8_t)battlefield == still[i])
            return true;
    return false;
}

bool battle_bg3_stays_still(const GameState *g) { return battle_keeps_bg3_still(battle_background(g)); }

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
    set_battle_scroll_mode(g, battle_bg3_stays_still(g) ? BATTLE_SCROLL_STEADY : BATTLE_SCROLL_BG3_MOVING);
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

/* ---- A battlefield's own channels ------------------------------------------------------- */

/* A battlefield can run screen scripts of its own: when it is loaded, the
 * channels forget what ran on them, and each it names a script for starts on
 * it at once, the script's first byte giving its pace and its first step
 * coming on the next frame. */
enum { NO_BATTLEFIELD_SCRIPT = 0xFF, SCRIPT_PACE = 0x0F };

/* Everything the channels keep: their records, their layers' moves, and their
 * loops, calls, waves and swings, three runs of 32 bytes. Each is every
 * channel variable there rather than one the RAM map names, so they are
 * cleared by address (game/wram.h). */
enum { CHANNELS_STATE_AT = 0x7ECA32, CHANNELS_STATE_WORDS = 0x30, CHANNELS_STATE_BYTES = 2 * CHANNELS_STATE_WORDS };

/* The original keeps the script's place in the battle's cells $53-$55; the
 * last started is left there. */
enum { LEFT_SCRIPT = 0x53, LEFT_SCRIPT_BANK = 0x55 };

// $CD:15D5
static void start_battlefield_script(GameState *g, ScreenChannel channel, uint8_t script)
{
    uint16_t start = screen_script_start(g, script);
    set_battle_screen_channels_pace(g, channel, battle_screen_script_byte(g, SCREEN_SCRIPT_BANK | start) & SCRIPT_PACE);
    set_battle_screen_channels_place(g, channel, SCREEN_SCRIPT_BANK | (uint16_t)(start + 1));
    set_battle_screen_channels_countdown(g, channel, 1);
    set_battle_screen_channels_running(g, channel, 1);
    ORIGINAL_EFFECT(screen_leave_word(g, LEFT_SCRIPT, start));
    ORIGINAL_EFFECT(screen_leave_cell(g, LEFT_SCRIPT_BANK, (uint8_t)(SCREEN_SCRIPT_BANK >> 16)));
}

// $CD:0D33
void battle_start_battlefield_scripts(GameState *g)
{
    wram_fill16(g, CHANNELS_STATE_AT, CHANNELS_STATE_WORDS, 0);
    ORIGINAL_EFFECT(charge_as(g, 0xCD0D33, CHANNELS_STATE_BYTES));
    for (ScreenChannel channel = 0; channel < SCREEN_CHANNELS; channel++) {
        uint8_t script = battle_battlefield_scripts(g, channel);
        if (script != NO_BATTLEFIELD_SCRIPT)
            start_battlefield_script(g, channel, script);
    }
}

/* ---- The entries ------------------------------------------------------------------------ */

void battle_run_screen_scripts(Snes *s)
{
    if (run_screen_scripts(s))
        m_rts(s);
}

/* It leaves A's high byte clear for its callers. */
void battle_start_battlefield_scripts_entry(Snes *s)
{
    battle_start_battlefield_scripts(state_of(s));
    s->cpu.a = 0;
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

static void run_entered(Snes *s, uint8_t opcode)
{
    GameState *g = state_of(s);
    const ScreenOpcode *row = screen_opcode(opcode);
    ScreenCommand c = decode(g, row, screen_running_channel(g), screen_place(g));
    ScreenFlow flow = row->operation(g, &c);
    finish_command(g, row, &c, flow);
    ORIGINAL_EFFECT(leave_the_machine(s, opcode, &c));
    s->cpu.a = 0;
    m_rts(s);
}

/* A command whose operation is C, entered as its handler: the command found
 * from its table's index in X, its operands at the place. */
void battle_screen_command(Snes *s) { run_entered(s, (uint8_t)(FIRST_COMMAND + (uint8_t)s->cpu.x / 2)); }

/* The triangle drawn at its size, entered as its handler from the dispatch or
 * from a model's command, whose original goes on into the triangle's code
 * once it has placed the corners: either way the triangle the command's
 * operand numbers (draw_model, screen_shapes.c, does the same in C). */
enum { DRAW_TRIANGLE = 0xAE };

void battle_screen_draw_triangle(Snes *s) { run_entered(s, DRAW_TRIANGLE); }

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

static bool command_runs_here(const GameState *g, const ScreenOpcode *row)
{
    if (!row || !on_a_channel(g, battle_screen_channel(g)))
        return false;
    ScreenCommand c = decode(g, row, screen_running_channel(g), screen_place(g));
    return runs_here(g, row, &c) && !row->bridge;
}

bool battle_screen_command_ready(Snes *s)
{
    vars_peek(true);
    uint16_t index = s->cpu.x;
    uint8_t opcode = (uint8_t)(FIRST_COMMAND + index / 2);
    bool ready = index < 2 * COMMANDS && index % 2 == 0 && command_runs_here(state_of(s), screen_opcode(opcode));
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && m_flag(s, CPU_FLAG_C) && ready;
}

/* The triangle reads neither the index nor the carry it is entered with. */
bool battle_screen_triangle_ready(Snes *s)
{
    vars_peek(true);
    bool ready = command_runs_here(state_of(s), screen_opcode(DRAW_TRIANGLE));
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && ready;
}
