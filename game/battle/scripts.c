/* Battle: the animation script channels.
 *
 * What the battle shows of an action -- a combatant stepping forward, a
 * tech's effect, the numbers rising over its targets -- is played by scripts
 * in the ROM, on sixteen channels (battle/script_language.h says what each
 * animates). Each frame the battle runs every channel that is running
 * (battle_run_scripts): a channel either waits out the frames a command gave
 * it, or runs commands until one ends its frame. A command byte past the
 * language's last holds the channel there.
 *
 * The commands are the language's rows (battle/script_language.h, the
 * script_*.c files and battle/numbers.c hold them), one for every command
 * byte. A command whose row's operation can run in the state runs it, timed
 * as the original's handler; a command with a bridge (the sound commands,
 * the table below) runs as its handler's entry, called as the original's
 * runner calls it. What a row's
 * needs turn away calls the original's handler, found through its dispatch
 * table (battle/script_data.h), with the channel's place in the script in
 * battle_script_pointer, which the handler reads its operands through and
 * moves on past them; a command that lets the channel go on within the frame
 * sets battle_script_continue. */
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/effects.h"
#include "battle/entries.h"
#include "battle/program.h"
#include "battle/script_data.h"
#include "battle/script_language.h"
#include "charge.h"
#include "port/native.h"
#include "vars.h"

/* The last command byte the language has: a byte past it holds the channel. */
enum { LAST_COMMAND = 0xDA };

/* The original's handler for a command, which the call-outs and the
 * commands' timing go by (battle/script_data.h). The runner's read of its
 * dispatch table is the runner's own time, which the channels' estimate was
 * calibrated without, so it is left out of it. */
static uint32_t handler_of(GameState *g, uint8_t command)
{
    uint32_t mark = 0;
    ORIGINAL_EFFECT(mark = charge_uncounted_begin(g));
    uint32_t handler = battle_script_handler(g, command);
    ORIGINAL_EFFECT(charge_uncounted_end(g, mark));
    return handler;
}

/* The commands that still call out to the original's code (the sound
 * driver), whose rows only decode them: each bridge takes the machine and the
 * command, and is false where the original carried on from the call-out
 * itself (the channel then not moved on); otherwise the channel goes on past
 * the command within the frame. */
typedef bool (*ScriptBridge)(Snes *s, const ScriptCommand *command);

static const struct {
    uint8_t command;
    ScriptBridge bridge;
} bridges[] = {
    { 0x78, script_play_sound_bridge },    { 0x79, script_play_sound_bridge },    { 0x7A, script_play_sound_at_bridge },
    { 0x7B, script_play_sound_at_bridge }, { 0x7C, script_weapon_sound_bridge }, { 0x7D, script_weapon_sound_bridge },
};

static ScriptBridge bridge_of(uint8_t command)
{
    for (unsigned i = 0; i < sizeof bridges / sizeof bridges[0]; i++)
        if (bridges[i].command == command)
            return bridges[i].bridge;
    return NULL;
}

static uint32_t channel_place(const GameState *g, unsigned channel) { return battle_script_pointers(g, channel); }
static void set_channel_place(GameState *g, unsigned channel, uint32_t at) { set_battle_script_pointers(g, channel, at); }

/* ---- One channel ------------------------------------------------------------------------- */

/* A channel's next command, or none when it holds. */
enum { HOLDS = -1 };

static int next_command(GameState *g)
{
    uint8_t command = script_operand(g, 0);
    set_battle_script_command(g, command);
    return command <= LAST_COMMAND ? command : HOLDS;
}

/* Take up a channel: its place in its script is where its commands read. The
 * byte offset of its pointer is kept beside it, as the original's callers
 * give it. True if it has frames to wait still (one of them is counted off). */
static bool take_up_channel(GameState *g, unsigned channel)
{
    set_battle_script_offset(g, (uint8_t)(channel * 4));
    set_battle_script_pointer(g, channel_place(g, channel));
    set_battle_script_channel(g, (uint16_t)channel);
    uint8_t wait = battle_script_waits(g, channel);
    if (wait == 0)
        return false;
    set_battle_script_waits(g, channel, (uint8_t)(wait - 1));
    return true;
}

/* A command whose operation is C: run as the original's handler, whose time
 * it takes. */
static void run_operation(GameState *g, uint8_t command, const ScriptOpcode *row)
{
    TIMED_AS(g, handler_of(g, command), script_move_on(g, row, script_operate(g, row)));
}

/* How a command the channel comes to runs: its operation, its bridge (the
 * sound commands, run as their handler's entry), or the original's handler;
 * and, told after it, that the operation ran. */
typedef enum { RUN_THE_OPERATION, RUN_THE_BRIDGE, RUN_THE_HANDLER, THE_OPERATION_RAN } CommandRuns;

static CommandRuns how_it_runs(const GameState *g, uint8_t command, const ScriptOpcode *row)
{
    if (!row || !script_runs_here(g, row))
        return RUN_THE_HANDLER;
    return bridge_of(command) ? RUN_THE_BRIDGE : RUN_THE_OPERATION;
}

// $C1:4C9E
/* Run a channel for a frame: count off a frame of its wait, or run its
 * commands until one ends the frame (or it holds), and keep its place. A
 * command that isn't the operation's is the routine `elsewhere` runs. */
static bool run_channel(GameState *g, unsigned channel,
                        bool (*elsewhere)(void *ctx, uint8_t command, const ScriptOpcode *row, CommandRuns runs), void *ctx)
{
    if (take_up_channel(g, channel))
        return true;
    for (;;) {
        set_battle_script_continue(g, false);
        int command = next_command(g);
        if (command == HOLDS)
            break;
        const ScriptOpcode *row = script_opcode((uint8_t)command);
        CommandRuns runs = how_it_runs(g, (uint8_t)command, row);
        if (runs == RUN_THE_OPERATION) {
            run_operation(g, (uint8_t)command, row);
            runs = THE_OPERATION_RAN;
        }
        if (!elsewhere(ctx, (uint8_t)command, row, runs))
            return false;
        if (!battle_script_continue(g))
            break;
    }
    set_channel_place(g, channel, battle_script_pointer(g));
    return true;
}

/* On the machine: the operation leaves A clear as every handler does; the
 * registers the original's runner calls a handler with are the command's
 * byte doubled in X (its table's index), Y and A clear. */
static void enter_as_the_runner_does(Snes *s, uint8_t command)
{
    s->cpu.x = (uint16_t)(command * 2);
    s->cpu.y = 0;
    s->cpu.a = 0;
}

static bool elsewhere_on_the_machine(void *ctx, uint8_t command, const ScriptOpcode *row, CommandRuns runs)
{
    (void)row;
    Snes *s = ctx;
    GameState *g = state_of(s);
    if (runs == THE_OPERATION_RAN) {
        s->cpu.a = 0;
        return true;
    }
    enter_as_the_runner_does(s, command);
    /* a bridge runs as its handler, called as the runner calls it */
    if (runs == RUN_THE_BRIDGE)
        return battle_run(s, 0xC14CD4, handler_of(g, command), battle_script_command_entry);
    /* no handler reads the overflow the one before it left, which a
       command in C doesn't reproduce */
    /* dc-evidence: ct_fuzz --poison-dc battle_run_script_channel, seed 1: 5000 states,
       1994 call-outs scrambled, 0 mismatched */
    return battle_call(s, 0xC14CD4, handler_of(g, command), DC_AL | DC_N | DC_V | DC_Z | DC_C);
}

/* ---- The channels, each frame ------------------------------------------------------------- */

/* The channels run, each from a call of its own in the original ($C1:4C9E
 * is called from a site a channel): the channel's routine (`channel`) calls
 * the commands, which is why the frame's run calls it through the machine
 * rather than as C there (docs/PORTING.md, "Call-outs"). */
static const uint16_t channel_calls[SCRIPT_CHANNELS] = {
    0x4BC8, 0x4BD5, 0x4BE2, 0x4BEF, 0x4C04, 0, 0, 0, 0x4C1C, 0x4C2E, 0x4C40, 0x4C52, 0x4C64, 0x4C76, 0x4C88, 0x4C9A,
};

static bool channel_running(const GameState *g, unsigned channel) { return battle_script_running(g, channel) != 0; }

// $C1:4BBE
/* Run the channels for a frame: the battle's own, the other targets' while
 * the action has some, and, unless the numbers over the combatants are being
 * shown, the enemies', each told its enemy. */
static bool run_scripts(GameState *g, bool (*run)(void *ctx, unsigned channel), void *ctx)
{
    for (unsigned channel = 0; channel < OTHER_TARGETS_CHANNEL; channel++)
        if (channel_running(g, channel) && !run(ctx, channel))
            return false;
    if (channel_running(g, OTHER_TARGETS_CHANNEL) && battle_action_other_targets(g) != 0 && !run(ctx, OTHER_TARGETS_CHANNEL))
        return false;
    if (battle_number_step(g) != 0)
        return true;
    for (unsigned channel = FIRST_EFFECT_CHANNEL; channel < SCRIPT_CHANNELS; channel++) {
        if (!channel_running(g, channel))
            continue;
        set_battle_script_effect(g, (uint8_t)(channel - FIRST_EFFECT_CHANNEL));
        if (!run(ctx, channel))
            return false;
    }
    return true;
}

static bool channel_on_the_machine(void *ctx, unsigned channel)
{
    Snes *s = ctx;
    s->cpu.y = (uint16_t)channel;
    m_set_a8(s, (uint8_t)(channel * 4));
    /* The channel reads neither X nor the overflow it is called with (the
     * showing's steps before it, battle/action_showing.c, are C and leave
     * neither as the original's do).
     * dc-evidence: ct_fuzz --poison-dc battle_run_scripts, seed 1: 3000 states,
     * 15072 call-outs scrambled, 0 mismatched */
    return battle_call(s, 0xC10000u | channel_calls[channel], 0xC14C9E, DC_X | DC_N | DC_V | DC_Z | DC_C);
}

/* ---- As C ------------------------------------------------------------------------------------ */

typedef struct {
    GameState *game;
    Platform *pf;
} ScriptsAsC;

/* A bridge's command, the sounds, as C; a command whose handler is the
 * original's goes no further. */
static bool command_elsewhere(void *ctx, uint8_t command, const ScriptOpcode *row, CommandRuns runs)
{
    ScriptsAsC *c = ctx;
    GameState *g = c->game;
    if (runs == THE_OPERATION_RAN)
        return true;
    if (runs != RUN_THE_BRIDGE)
        return battle_needs_the_machine(g, handler_of(g, command), "an animation script's command the port doesn't run");
    ScriptCommand decoded = script_decode(g, row);
    TIMED_AS(g, handler_of(g, command), script_play_the_sound(g, c->pf, command, &decoded));
    script_move_on(g, row, SCRIPT_NEXT);
    return true;
}

static bool run_a_channel(void *ctx, unsigned channel)
{
    ScriptsAsC *c = ctx;
    bool done = false;
    TIMED_AS(c->game, 0xC14C9E, done = run_channel(c->game, channel, command_elsewhere, ctx));
    return done;
}

bool battle_run_scripts(GameState *g, Platform *pf)
{
    ScriptsAsC c = { g, pf };
    return run_scripts(g, run_a_channel, &c);
}

/* ---- The entries ------------------------------------------------------------------------ */

/* The channel in Y, its pointer's offset in A (four times it). */
/* The operation leaves A clear as every handler does, where the channel
 * ran one last. */
void battle_run_script_channel(Snes *s)
{
    if (run_channel(state_of(s), s->cpu.y, elsewhere_on_the_machine, s))
        m_rts(s);
}

void battle_run_scripts_entry(Snes *s)
{
    if (run_scripts(state_of(s), channel_on_the_machine, s))
        m_rts(s);
}

/* The channel the original names in Y, and its pointer's offset in A, which
 * it indexes with as a word (A's high byte 0, as its callers leave it). */
bool battle_script_channel_in_y(Snes *s)
{
    return battle_on_page(s) && s->cpu.y < SCRIPT_CHANNELS && s->cpu.a == (uint16_t)(s->cpu.y * 4);
}

/* The commands whose handler goes on into another's once what it found makes
 * the command do what the other does: the end unless listed into the end's
 * where nobody is listed, the return at the top into the plain return's where
 * the point is at the top, the throw that waits first into the plain throw's
 * once its wait is over. Entered so, the command's own operation runs there
 * and does the same. */
enum {
    END = 0x00, RETURN_FROM_CALL = 0x1F, RETURN_AT_THE_TOP = 0x21, END_UNLESS_LISTED = 0x25, THROW_WAITING_FIRST = 0xD4,
    THROW = 0xD6
};

/* The end's handler doesn't read the operand the port decides by, so it is
 * read where the game keeps scripts: the ROM's banks or work RAM's, within
 * the bank. */
enum { WORK_RAM_BANK = 0x7E, LAST_WORK_RAM_BANK = 0x7F, FIRST_ROM_BANK = 0xC0, BANK_END = 0xFFFF };

static bool operand_where_scripts_are(const GameState *g)
{
    uint32_t at = battle_script_pointer(g);
    uint8_t bank = (uint8_t)(at >> 16);
    return (bank >= FIRST_ROM_BANK || bank == WORK_RAM_BANK || bank == LAST_WORK_RAM_BANK) && (uint16_t)at != BANK_END;
}

/* A command's handler, as its place in bank C1: what a guard is told it is
 * entered at. */
static uint16_t handler_in_bank(const GameState *g, uint8_t command) { return (uint16_t)battle_script_handler(g, command); }

static bool goes_on_into(const GameState *g, uint8_t command, const ScriptOpcode *row, uint16_t handler)
{
    switch (command) {
    case END_UNLESS_LISTED:
        return handler == handler_in_bank(g, END) && operand_where_scripts_are(g)
               && script_ends_unless_listed(g, script_decode(g, row).listed);
    case RETURN_AT_THE_TOP: return handler == handler_in_bank(g, RETURN_FROM_CALL) && script_point_at_the_top(g);
    case THROW_WAITING_FIRST: return handler == handler_in_bank(g, THROW) && script_throw_wait_over(g, WAIT_BEFORE);
    default: return false;
    }
}

/* A command whose operation is C, where the original's handler is entered:
 * the command's byte where the channel's runner leaves it, its operands from
 * the channel's place, and what the operation needs in range; and either the
 * command one this handler serves, entered from the runner with Y clear (the
 * operands are read through it) and A clear, or one whose handler goes on
 * into this one. */
static bool command_ready(Snes *s, uint16_t handler)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    uint16_t channel = battle_script_channel(g);
    uint8_t command = battle_script_command(g), offset = battle_script_offset(g);
    const ScriptOpcode *row = command <= LAST_COMMAND ? script_opcode(command) : NULL;
    bool ready = battle_on_page(s) && channel < SCRIPT_CHANNELS && offset == channel * 4 && row && script_runs_here(g, row)
                 && ((handler_in_bank(g, command) == handler && s->cpu.a == 0 && s->cpu.y == 0)
                     || goes_on_into(g, command, row, handler));
    vars_peek(false);
    return ready;
}

#define COMMAND_GUARD(name, handler) \
    bool name(Snes *s) { return command_ready(s, handler); }

COMMAND_GUARD(script_end_ready, 0x4CF0)
COMMAND_GUARD(script_halt_ready, 0x4CF7)
COMMAND_GUARD(script_call_ready, 0x56CC)
COMMAND_GUARD(script_return_from_call_ready, 0x56FB)
COMMAND_GUARD(script_wait_frames_ready, 0x5714)
COMMAND_GUARD(script_return_at_the_top_ready, 0x5722)
COMMAND_GUARD(script_wait_for_variable_ready, 0x572C)
COMMAND_GUARD(script_wait_for_mark_a_ready, 0x5743)
COMMAND_GUARD(script_wait_for_mark_b_ready, 0x5756)
COMMAND_GUARD(script_end_unless_listed_ready, 0x5769)
COMMAND_GUARD(script_set_variable_ready, 0x58EC)
COMMAND_GUARD(script_set_mark_a_ready, 0x58FE)
COMMAND_GUARD(script_set_mark_b_ready, 0x590C)
COMMAND_GUARD(script_count_variable_up_ready, 0x5931)
COMMAND_GUARD(script_count_mark_a_up_ready, 0x5940)
COMMAND_GUARD(script_count_mark_b_up_ready, 0x594B)
COMMAND_GUARD(script_count_variable_down_ready, 0x5956)
COMMAND_GUARD(script_count_mark_a_down_ready, 0x5965)
COMMAND_GUARD(script_count_mark_b_down_ready, 0x5970)
COMMAND_GUARD(script_add_to_variable_ready, 0x597B)
COMMAND_GUARD(script_add_to_mark_a_ready, 0x5991)
COMMAND_GUARD(script_add_variable_to_mark_a_ready, 0x59A3)
COMMAND_GUARD(script_copy_variable_ready, 0x59E4)
COMMAND_GUARD(script_store_mark_a_ready, 0x59FA)
COMMAND_GUARD(script_add_held_ready, 0x5A70)
COMMAND_GUARD(script_swap_with_mark_a_ready, 0x5A99)
COMMAND_GUARD(script_swap_with_mark_a_point_x_ready, 0x5AAF)
COMMAND_GUARD(script_swap_with_mark_a_point_y_ready, 0x5AC5)
COMMAND_GUARD(script_skip_ready, 0x5D18)
COMMAND_GUARD(script_play_animation_ready, 0x4D04)
COMMAND_GUARD(script_play_and_wait_shown_ready, 0x4E22)
COMMAND_GUARD(script_play_and_wait_ready, 0x4EA2)
COMMAND_GUARD(script_own_speed_ready, 0x4F1A)
COMMAND_GUARD(script_set_speed_ready, 0x4F54)
COMMAND_GUARD(script_play_and_wait_marked_ready, 0x577D)
COMMAND_GUARD(script_drawn_on_top_ready, 0x5806)
COMMAND_GUARD(script_drawn_on_top_off_ready, 0x5840)
COMMAND_GUARD(script_hidden_ready, 0x5844)
COMMAND_GUARD(script_hidden_off_ready, 0x5879)
COMMAND_GUARD(script_sprite_back_in_place_ready, 0x587D)
COMMAND_GUARD(script_bring_back_ready, 0x58AB)
COMMAND_GUARD(script_withdrawn_off_ready, 0x5CF1)
COMMAND_GUARD(script_withdrawn_ready, 0x5D12)
COMMAND_GUARD(script_trail_ready, 0x691B)
COMMAND_GUARD(script_trail_off_ready, 0x69BA)
COMMAND_GUARD(script_glide_sprite_ready, 0x6F9A)
COMMAND_GUARD(script_glide_sprite_home_ready, 0x6FEF)
COMMAND_GUARD(script_shadow_ready, 0x6FFC)
COMMAND_GUARD(script_shadow_off_ready, 0x702B)
COMMAND_GUARD(script_attribute_bits_ready, 0x753A)
COMMAND_GUARD(script_attribute_bits_back_ready, 0x7579)
COMMAND_GUARD(script_anchor_follows_ready, 0x5694)
COMMAND_GUARD(script_anchor_stays_ready, 0x56BB)
COMMAND_GUARD(script_palettes_paused_ready, 0x58DC)
COMMAND_GUARD(script_palettes_paused_off_ready, 0x58E4)
COMMAND_GUARD(script_palette_animation_ready, 0x5D93)
COMMAND_GUARD(script_palette_animation_off_ready, 0x5DC1)
COMMAND_GUARD(script_color_cycles_off_ready, 0x5F9A)
COMMAND_GUARD(script_all_effects_shown_ready, 0x5FA5)
COMMAND_GUARD(script_all_effects_shown_off_ready, 0x5FB3)
COMMAND_GUARD(script_effect_shown_ready, 0x5FC1)
COMMAND_GUARD(script_effect_shown_off_ready, 0x5FD3)
COMMAND_GUARD(script_effect_drawn_with_ready, 0x6059)
COMMAND_GUARD(script_effect_drawn_with_behind_ready, 0x605F)
COMMAND_GUARD(script_palette_frame_ready, 0x5D1D)
COMMAND_GUARD(script_flash_with_ready, 0x5DD3)
COMMAND_GUARD(script_flash_off_ready, 0x5E87)
COMMAND_GUARD(script_rotate_colors_ready, 0x5EDF)
COMMAND_GUARD(script_blink_ready, 0x5F13)
COMMAND_GUARD(script_line_to_coordinates_ready, 0x4FBF)
COMMAND_GUARD(script_line_to_the_point_ready, 0x4FD1)
COMMAND_GUARD(script_line_to_a_place_ready, 0x4FE3)
COMMAND_GUARD(script_chase_to_coordinates_ready, 0x51EE)
COMMAND_GUARD(script_chase_to_the_point_ready, 0x5205)
COMMAND_GUARD(script_chase_to_a_place_ready, 0x521C)
COMMAND_GUARD(script_walk_to_coordinates_ready, 0x5409)
COMMAND_GUARD(script_walk_to_the_point_ready, 0x541B)
COMMAND_GUARD(script_walk_to_a_place_ready, 0x542D)
COMMAND_GUARD(script_put_at_coordinates_ready, 0x5611)
COMMAND_GUARD(script_put_at_the_point_ready, 0x566C)
COMMAND_GUARD(script_put_at_a_place_ready, 0x567D)
COMMAND_GUARD(script_point_at_place_ready, 0x591A)
COMMAND_GUARD(script_slot_at_place_ready, 0x59B9)
COMMAND_GUARD(script_point_off_place_ready, 0x5A0C)
COMMAND_GUARD(script_point_at_the_mover_ready, 0x5A3E)
COMMAND_GUARD(script_show_numbers_ready, 0x5ADB)
COMMAND_GUARD(script_face_ready, 0x5FDE)
COMMAND_GUARD(script_take_heading_ready, 0x6093)
COMMAND_GUARD(script_heading_between_ready, 0x60E6)
COMMAND_GUARD(script_turn_heading_ready, 0x6154)
COMMAND_GUARD(script_play_sound_ready, 0x619E)
COMMAND_GUARD(script_play_sound_at_ready, 0x61C1)
COMMAND_GUARD(script_weapon_swing_sound_ready, 0x61EB)
COMMAND_GUARD(script_weapon_hit_sound_ready, 0x61F5)
COMMAND_GUARD(script_screen_effect_ready, 0x621B)
COMMAND_GUARD(script_slot_follows_place_ready, 0x623C)
COMMAND_GUARD(script_set_heading_ready, 0x6258)
COMMAND_GUARD(script_jump_to_a_place_ready, 0x6299)
COMMAND_GUARD(script_jump_to_the_point_ready, 0x672E)
COMMAND_GUARD(script_jump_down_to_a_place_ready, 0x6743)
COMMAND_GUARD(script_jump_down_to_the_point_ready, 0x674A)
COMMAND_GUARD(script_spiral_out_ready, 0x6751)
COMMAND_GUARD(script_spiral_for_ready, 0x69C3)
COMMAND_GUARD(script_spiral_for_stopping_ready, 0x6AF0)
COMMAND_GUARD(script_orbit_a_place_ready, 0x6CF3)
COMMAND_GUARD(script_orbit_the_point_ready, 0x6F31)
COMMAND_GUARD(script_orbit_flat_a_place_ready, 0x6F53)
COMMAND_GUARD(script_orbit_flat_the_point_ready, 0x6F75)
COMMAND_GUARD(script_aim_throw_ready, 0x705A)
COMMAND_GUARD(script_throw_rise_ready, 0x720E)
COMMAND_GUARD(script_throw_waits_before_ready, 0x7349)
COMMAND_GUARD(script_throw_waits_after_ready, 0x73AD)
/* D4's handler goes on into D6's here once its wait is over: where the
   original runs D4 (its needs refused), this sees D4's byte and refuses too
   (180 calls in the library; docs/TODO.md). */
COMMAND_GUARD(script_throw_moves_ready, 0x7364)
COMMAND_GUARD(script_point_along_ready, 0x740B)
COMMAND_GUARD(script_flicker_ready, 0x74B4)

/* A command whose operation is C, or a bridge, entered as its handler. A
 * bridge whose call-out the original carried on from leaves the machine
 * there. */
void battle_script_command_entry(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t byte = battle_script_command(g);
    const ScriptOpcode *row = script_opcode(byte);
    ScriptBridge bridge = bridge_of(byte);
    ScriptFlow flow = SCRIPT_NEXT;
    if (bridge) {
        ScriptCommand command = script_decode(g, row);
        if (!bridge(s, &command))
            return;
    } else {
        flow = script_operate(g, row);
    }
    script_move_on(g, row, flow);
    s->cpu.a = 0;
    m_rts(s);
}
