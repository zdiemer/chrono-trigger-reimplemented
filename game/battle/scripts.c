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
 * The commands dispatch through the C table below. A row whose operation is
 * C runs it (battle/script_flow.c and the other script_*.c), timed as the
 * original's handler, whose address the row keeps; the rest still call the
 * original's handler, with the channel's place in the script in
 * battle_script_pointer, which the handler reads its operands through and
 * moves on past them; a command that lets the channel go on within the frame
 * sets battle_script_continue. The number display's commands are ported
 * (battle/numbers.c) and reached that way. */
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/effects.h"
#include "battle/entries.h"
#include "battle/script_language.h"
#include "port/native.h"
#include "vars.h"

enum { LAST_COMMAND = 0xDA };

/* The language: each command's operation, or none where it is still the
 * original's, and the original's handler, in bank C1. */
typedef struct {
    ScriptOperation operation;
    uint16_t handler;
    uint8_t form;
} ScriptCommand;

enum { HANDLER_BANK = 0xC1 };

static const ScriptCommand commands[LAST_COMMAND + 1] = {
    [0x00] = { script_end, 0x4CF0 },
    [0x01] = { script_halt, 0x4CF7 },
    [0x02] = { script_play_animation, 0x4D04, PLAY_LOOPING },
    [0x03] = { script_play_shown_and_wait, 0x4E22 },
    [0x04] = { script_play_and_wait, 0x4EA2 },
    [0x05] = { script_play_and_wait, 0x4EA2 },
    [0x06] = { script_play_and_wait, 0x4EA2 },
    [0x07] = { script_own_speed, 0x4F1A },
    [0x08] = { script_speed, 0x4F54, 0 },
    [0x09] = { script_speed, 0x4F54, 1 },
    [0x0A] = { script_speed, 0x4F54, 2 },
    [0x0B] = { script_speed, 0x4F54, 3 },
    [0x0C] = { script_speed, 0x4F54, 4 },
    [0x0D] = { script_speed, 0x4F54, 5 },
    [0x0E] = { script_speed, 0x4F54, 6 },
    [0x0F] = { script_speed, 0x4F54, 7 },
    [0x10] = { NULL, 0x4FBF },
    [0x11] = { NULL, 0x4FD1 },
    [0x12] = { NULL, 0x4FE3 },
    [0x13] = { NULL, 0x51EE },
    [0x14] = { NULL, 0x5205 },
    [0x15] = { NULL, 0x521C },
    [0x16] = { NULL, 0x5409 },
    [0x17] = { NULL, 0x541B },
    [0x18] = { NULL, 0x542D },
    [0x19] = { NULL, 0x5611 },
    [0x1A] = { NULL, 0x566C },
    [0x1B] = { NULL, 0x567D },
    [0x1C] = { script_anchor_follows, 0x5694 },
    [0x1D] = { script_anchor_stays, 0x56BB },
    [0x1E] = { script_call, 0x56CC },
    [0x1F] = { script_return, 0x56FB },
    [0x20] = { script_wait, 0x5714 },
    [0x21] = { script_return_unless_point_low, 0x5722 },
    [0x22] = { script_wait_for_value, 0x572C, SCRIPT_VARIABLE },
    [0x23] = { script_wait_for_value, 0x5743, SCRIPT_MARK_A },
    [0x24] = { script_wait_for_value, 0x5756, SCRIPT_MARK_B },
    [0x25] = { script_end_unless_listed, 0x5769 },
    [0x26] = { script_play_and_wait_marked, 0x577D },
    [0x27] = { script_drawn_on_top, 0x5806, 1 },
    [0x28] = { script_drawn_on_top, 0x5840, 0 },
    [0x29] = { script_hidden, 0x5844, 1 },
    [0x2A] = { script_hidden, 0x5879, 0 },
    [0x2B] = { script_sprite_back_in_place, 0x587D },
    [0x2C] = { script_bring_back, 0x58AB },
    [0x2D] = { script_palettes_paused, 0x58DC, 1 },
    [0x2E] = { script_palettes_paused, 0x58E4, 0 },
    [0x2F] = { script_halt, 0x4CF7 },
    [0x30] = { script_set_value, 0x58EC, SCRIPT_VARIABLE },
    [0x31] = { script_set_value, 0x58FE, SCRIPT_MARK_A },
    [0x32] = { script_set_value, 0x590C, SCRIPT_MARK_B },
    [0x33] = { NULL, 0x591A },
    [0x34] = { script_count_up, 0x5931, SCRIPT_VARIABLE },
    [0x35] = { script_count_up, 0x5940, SCRIPT_MARK_A },
    [0x36] = { script_count_up, 0x594B, SCRIPT_MARK_B },
    [0x37] = { script_count_down, 0x5956, SCRIPT_VARIABLE },
    [0x38] = { script_count_down, 0x5965, SCRIPT_MARK_A },
    [0x39] = { script_count_down, 0x5970, SCRIPT_MARK_B },
    [0x3A] = { script_add_to_value, 0x597B, SCRIPT_VARIABLE },
    [0x3B] = { script_add_to_value, 0x5991, SCRIPT_MARK_A },
    [0x3C] = { script_add_variable_to_mark_a, 0x59A3 },
    [0x3D] = { NULL, 0x59B9 },
    [0x3E] = { NULL, 0x59B9 },
    [0x3F] = { NULL, 0x59B9 },
    [0x40] = { NULL, 0x59B9 },
    [0x41] = { script_copy_variable, 0x59E4 },
    [0x42] = { script_store_mark_a, 0x59FA },
    [0x43] = { NULL, 0x5A0C },
    [0x44] = { NULL, 0x5A3E },
    [0x45] = { NULL, 0x59B9 },
    [0x46] = { NULL, 0x59B9 },
    [0x47] = { NULL, 0x623C },
    [0x48] = { NULL, 0x623C },
    [0x49] = { script_add_held, 0x5A70 },
    [0x4A] = { script_swap_with_mark_a, 0x5A99, SCRIPT_MARK_B },
    [0x4B] = { script_halt, 0x4CF7 },
    [0x4C] = { script_halt, 0x4CF7 },
    [0x4D] = { script_swap_with_mark_a, 0x5A99, SCRIPT_MARK_B },
    [0x4E] = { script_swap_with_mark_a, 0x5AAF, SCRIPT_POINT_X },
    [0x4F] = { script_swap_with_mark_a, 0x5AC5, SCRIPT_POINT_Y },
    [0x50] = { NULL, 0x5ADB },
    [0x51] = { NULL, 0x5ADB },
    [0x52] = { NULL, 0x5ADB },
    [0x53] = { NULL, 0x5ADB },
    [0x54] = { NULL, 0x5ADB },
    [0x55] = { NULL, 0x5ADB },
    [0x56] = { script_halt, 0x4CF7 },
    [0x57] = { script_halt, 0x4CF7 },
    [0x58] = { script_halt, 0x4CF7 },
    [0x59] = { script_halt, 0x4CF7 },
    [0x5A] = { script_halt, 0x4CF7 },
    [0x5B] = { script_halt, 0x4CF7 },
    [0x5C] = { script_halt, 0x4CF7 },
    [0x5D] = { script_withdrawn, 0x5CF1, 0 },
    [0x5E] = { script_withdrawn, 0x5D12, 1 },
    [0x5F] = { script_skip, 0x5D18 },
    [0x60] = { script_palette_frame, 0x5D1D },
    [0x61] = { script_palette_animation, 0x5D93, 0 },
    [0x62] = { script_palette_animation, 0x5D93, 1 },
    [0x63] = { script_palette_animation, 0x5D93, 2 },
    [0x64] = { script_palette_animation, 0x5D93, 3 },
    [0x65] = { script_palette_animation_off, 0x5DC1, 0 },
    [0x66] = { script_palette_animation_off, 0x5DC1, 1 },
    [0x67] = { script_palette_animation_off, 0x5DC1, 2 },
    [0x68] = { script_palette_animation_off, 0x5DC1, 3 },
    [0x69] = { script_flash, 0x5DD3 },
    [0x6A] = { script_flash_off, 0x5E87 },
    [0x6B] = { script_rotate_colors, 0x5EDF },
    [0x6C] = { script_blink, 0x5F13 },
    [0x6D] = { script_colour_cycles_off, 0x5F9A },
    [0x6E] = { script_all_effects_shown, 0x5FA5, 1 },
    [0x6F] = { script_all_effects_shown, 0x5FB3, 0 },
    [0x70] = { script_effect_shown, 0x5FC1, 1 },
    [0x71] = { script_effect_shown, 0x5FD3, 0 },
    [0x72] = { NULL, 0x5FDE },
    [0x73] = { script_effect_drawn_with, 0x6059, BATTLE_RECORD_IN_FRONT },
    [0x74] = { script_effect_drawn_with, 0x605F, BATTLE_RECORD_BEHIND },
    [0x75] = { NULL, 0x6093 },
    [0x76] = { NULL, 0x60E6 },
    [0x77] = { NULL, 0x6154 },
    [0x78] = { NULL, 0x619E },
    [0x79] = { NULL, 0x619E },
    [0x7A] = { NULL, 0x61C1 },
    [0x7B] = { NULL, 0x61C1 },
    [0x7C] = { NULL, 0x61EB },
    [0x7D] = { NULL, 0x61F5 },
    [0x7E] = { script_halt, 0x4CF7 },
    [0x7F] = { script_halt, 0x4CF7 },
    [0x80] = { NULL, 0x621B },
    [0x81] = { NULL, 0x623C },
    [0x82] = { NULL, 0x623C },
    [0x83] = { NULL, 0x623C },
    [0x84] = { NULL, 0x623C },
    [0x85] = { NULL, 0x6258 },
    [0x86] = { script_halt, 0x4CF7 },
    [0x87] = { script_halt, 0x4CF7 },
    [0x88] = { script_halt, 0x4CF7 },
    [0x89] = { script_halt, 0x4CF7 },
    [0x8A] = { script_halt, 0x4CF7 },
    [0x8B] = { script_halt, 0x4CF7 },
    [0x8C] = { script_halt, 0x4CF7 },
    [0x8D] = { script_halt, 0x4CF7 },
    [0x8E] = { script_halt, 0x4CF7 },
    [0x8F] = { script_halt, 0x4CF7 },
    [0x90] = { script_halt, 0x4CF7 },
    [0x91] = { script_halt, 0x4CF7 },
    [0x92] = { script_halt, 0x4CF7 },
    [0x93] = { script_halt, 0x4CF7 },
    [0x94] = { script_halt, 0x4CF7 },
    [0x95] = { script_halt, 0x4CF7 },
    [0x96] = { script_halt, 0x4CF7 },
    [0x97] = { script_halt, 0x4CF7 },
    [0x98] = { NULL, 0x6299 },
    [0x99] = { NULL, 0x672E },
    [0x9A] = { NULL, 0x6743 },
    [0x9B] = { NULL, 0x674A },
    [0x9C] = { NULL, 0x6299 },
    [0x9D] = { NULL, 0x672E },
    [0x9E] = { script_halt, 0x4CF7 },
    [0x9F] = { script_halt, 0x4CF7 },
    [0xA0] = { script_halt, 0x4CF7 },
    [0xA1] = { script_halt, 0x4CF7 },
    [0xA2] = { NULL, 0x6751 },
    [0xA3] = { script_halt, 0x4CF7 },
    [0xA4] = { script_trail, 0x691B, 1 },
    [0xA5] = { script_trail, 0x69BA, 0 },
    [0xA6] = { script_halt, 0x4CF7 },
    [0xA7] = { script_halt, 0x4CF7 },
    [0xA8] = { NULL, 0x69C3 },
    [0xA9] = { NULL, 0x6AF0 },
    [0xAA] = { script_halt, 0x4CF7 },
    [0xAB] = { script_halt, 0x4CF7 },
    [0xAC] = { script_halt, 0x4CF7 },
    [0xAD] = { script_halt, 0x4CF7 },
    [0xAE] = { script_halt, 0x4CF7 },
    [0xAF] = { script_halt, 0x4CF7 },
    [0xB0] = { script_halt, 0x4CF7 },
    [0xB1] = { script_halt, 0x4CF7 },
    [0xB2] = { script_halt, 0x4CF7 },
    [0xB3] = { script_halt, 0x4CF7 },
    [0xB4] = { script_halt, 0x4CF7 },
    [0xB5] = { script_halt, 0x4CF7 },
    [0xB6] = { script_halt, 0x4CF7 },
    [0xB7] = { script_halt, 0x4CF7 },
    [0xB8] = { script_halt, 0x4CF7 },
    [0xB9] = { script_halt, 0x4CF7 },
    [0xBA] = { script_halt, 0x4CF7 },
    [0xBB] = { script_halt, 0x4CF7 },
    [0xBC] = { script_halt, 0x4CF7 },
    [0xBD] = { script_halt, 0x4CF7 },
    [0xBE] = { script_halt, 0x4CF7 },
    [0xBF] = { script_halt, 0x4CF7 },
    [0xC0] = { NULL, 0x6CF3 },
    [0xC1] = { NULL, 0x6F31 },
    [0xC2] = { NULL, 0x6F53 },
    [0xC3] = { NULL, 0x6F75 },
    [0xC4] = { script_glide_sprite, 0x6F9A, GLIDE_TO_HEIGHT },
    [0xC5] = { script_glide_sprite, 0x6FEF, GLIDE_HOME },
    [0xC6] = { script_halt, 0x4CF7 },
    [0xC7] = { script_halt, 0x4CF7 },
    [0xC8] = { script_halt, 0x4CF7 },
    [0xC9] = { script_halt, 0x4CF7 },
    [0xCA] = { script_halt, 0x4CF7 },
    [0xCB] = { script_halt, 0x4CF7 },
    [0xCC] = { script_halt, 0x4CF7 },
    [0xCD] = { script_halt, 0x4CF7 },
    [0xCE] = { script_halt, 0x4CF7 },
    [0xCF] = { script_halt, 0x4CF7 },
    [0xD0] = { script_shadow, 0x6FFC, 1 },
    [0xD1] = { script_shadow, 0x702B, 0 },
    [0xD2] = { NULL, 0x705A },
    [0xD3] = { NULL, 0x720E },
    [0xD4] = { NULL, 0x7349 },
    [0xD5] = { NULL, 0x73AD },
    [0xD6] = { NULL, 0x7364 },
    [0xD7] = { NULL, 0x740B },
    [0xD8] = { NULL, 0x74B4 },
    [0xD9] = { script_attribute_bits, 0x753A },
    [0xDA] = { script_attribute_bits_back, 0x7579 },
};

static uint32_t handler_of(uint8_t command) { return (uint32_t)HANDLER_BANK << 16 | commands[command].handler; }

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
 * it takes, and leaving A clear as every handler does. */
static void run_operation(Snes *s, uint8_t command)
{
    GameState *g = state_of(s);
    const ScriptCommand *row = &commands[command];
    ScriptStep step;
    NATIVE_TIME_AS(s, handler_of(command), step = row->operation(g, script_channel_running(g, command), row->form));
    script_take_step(g, step);
    s->cpu.a = 0;
}

// $C1:4C9E
/* Run a channel for a frame: count off a frame of its wait, or run its
 * commands until one ends the frame (or it holds), and keep its place. */
static bool run_channel(Snes *s, unsigned channel)
{
    GameState *g = state_of(s);
    if (take_up_channel(g, channel))
        return true;
    for (;;) {
        set_battle_script_continue(g, false);
        int command = next_command(g);
        if (command == HOLDS)
            break;
        if (commands[command].operation) {
            run_operation(s, (uint8_t)command);
        } else {
            s->cpu.x = (uint16_t)(command * 2);
            s->cpu.y = 0;
            s->cpu.a = 0;
            /* no handler reads the overflow the one before it left, which a
               command in C doesn't reproduce */
            /* dc-evidence: ct_fuzz --poison-dc battle_run_script_channel, seed 1: 5000 states,
               1994 call-outs scrambled, 0 mismatched */
            if (!battle_call(s, 0xC14CD4, handler_of((uint8_t)command), DC_AL | DC_N | DC_V | DC_Z | DC_C))
                return false;
        }
        if (!battle_script_continue(g))
            break;
    }
    set_channel_place(g, channel, battle_script_pointer(g));
    return true;
}

/* ---- The channels, each frame ------------------------------------------------------------- */

/* Each channel is run from a call of its own in the original ($C1:4C9E is
 * called from a site a channel): the channel's routine is the entry above,
 * and it calls the commands, which is why the frame's run calls it through
 * the machine rather than as C (docs/PORTING.md, "Call-outs"). */
static const uint16_t channel_calls[SCRIPT_CHANNELS] = {
    0x4BC8, 0x4BD5, 0x4BE2, 0x4BEF, 0x4C04, 0, 0, 0, 0x4C1C, 0x4C2E, 0x4C40, 0x4C52, 0x4C64, 0x4C76, 0x4C88, 0x4C9A,
};

static bool call_channel(Snes *s, unsigned channel)
{
    s->cpu.y = (uint16_t)channel;
    m_set_a8(s, (uint8_t)(channel * 4));
    return battle_call(s, 0xC10000u | channel_calls[channel], 0xC14C9E, DC_N | DC_Z | DC_C);
}

static bool channel_running(const GameState *g, unsigned channel) { return battle_script_running(g, channel) != 0; }

// $C1:4BBE
/* Run the channels for a frame: the battle's own, the other targets' while
 * the action has some, and, unless the numbers over the combatants are being
 * shown, the enemies', each told its enemy. */
static bool run_scripts(Snes *s)
{
    GameState *g = state_of(s);
    for (unsigned channel = 0; channel < OTHER_TARGETS_CHANNEL; channel++)
        if (channel_running(g, channel) && !call_channel(s, channel))
            return false;
    if (channel_running(g, OTHER_TARGETS_CHANNEL) && battle_action_other_targets(g) != 0
        && !call_channel(s, OTHER_TARGETS_CHANNEL))
        return false;
    if (battle_number_step(g) != 0)
        return true;
    for (unsigned channel = FIRST_EFFECT_CHANNEL; channel < SCRIPT_CHANNELS; channel++) {
        if (!channel_running(g, channel))
            continue;
        set_battle_script_enemy(g, (uint8_t)(channel - FIRST_EFFECT_CHANNEL));
        if (!call_channel(s, channel))
            return false;
    }
    return true;
}

/* ---- The entries ------------------------------------------------------------------------ */

/* The channel in Y, its pointer's offset in A (four times it). */
void battle_run_script_channel(Snes *s)
{
    if (run_channel(s, s->cpu.y))
        m_rts(s);
}

void battle_run_scripts(Snes *s)
{
    if (run_scripts(s))
        m_rts(s);
}

/* The channel the original names in Y, and its pointer's offset in A, which
 * it indexes with as a word (A's high byte 0, as its callers leave it). */
bool battle_script_channel_in_y(Snes *s)
{
    return battle_on_page(s) && s->cpu.y < SCRIPT_CHANNELS && s->cpu.a == (uint16_t)(s->cpu.y * 4);
}

/* A command whose operation is C, where the original's handler is entered:
 * the command's byte where the channel's runner leaves it, one this handler
 * serves, its operands from the channel's place, Y clear (they are read
 * through it) and A clear. */
static bool command_ready(Snes *s, uint16_t handler)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    uint16_t channel = battle_script_channel(g);
    uint8_t command = battle_script_command(g), offset = battle_script_offset(g);
    vars_peek(false);
    return battle_on_page(s) && s->cpu.a == 0 && s->cpu.y == 0 && channel < SCRIPT_CHANNELS && offset == channel * 4
           && command <= LAST_COMMAND && commands[command].operation && commands[command].handler == handler;
}

#define COMMAND_GUARD(name, handler) \
    bool name(Snes *s) { return command_ready(s, handler); }

/* ... and, for a command that acts on what its channel animates, that being
 * something it can index by: a combatant's battle slot, for a combatant
 * channel, and on the other targets' channels a list with a first target
 * (from channel 4's subject on, and from channel 1's, where the shadows start
 * it), each target a combatant; on an effect channel, one of the effects,
 * which the original reads as a word. */
enum { LIST_MOST = 16, SHADOW_LIST_START = 1 };

static bool list_of_combatants(const GameState *g, unsigned first, bool needs_one)
{
    for (unsigned at = first; at < first + LIST_MOST; at++) {
        uint8_t combatant;
        if (!script_listed(g, at, &combatant))
            return !needs_one || at > first;
        if (combatant >= BATTLE_COMBATANTS)
            return false;
    }
    return false;
}

static bool subjects_ready(const GameState *g)
{
    uint8_t channel = (uint8_t)battle_script_channel(g);
    if (channel >= FIRST_EFFECT_CHANNEL)
        return battle_script_enemy(g) < BATTLE_EFFECTS && battle_script_enemy_spare(g) == 0;
    if (channel != OTHER_TARGETS_CHANNEL && battle_script_subjects(g, channel) >= BATTLE_COMBATANTS)
        return false;
    if (channel < OTHER_TARGETS_CHANNEL)
        return true;
    return list_of_combatants(g, OTHER_TARGETS_START, true) && list_of_combatants(g, SHADOW_LIST_START, false);
}

static bool command_on_subjects_ready(Snes *s, uint16_t handler)
{
    if (!command_ready(s, handler))
        return false;
    vars_peek(true);
    bool ready = subjects_ready(state_of(s));
    vars_peek(false);
    return ready;
}

#define SUBJECTS_GUARD(name, handler) \
    bool name(Snes *s) { return command_on_subjects_ready(s, handler); }

/* ... and, for a command that acts on the channel's effect on any channel,
 * that being one of the effects, which the original reads as a word. */
static bool command_on_effect_ready(Snes *s, uint16_t handler)
{
    if (!command_ready(s, handler))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ready = battle_script_enemy(g) < BATTLE_EFFECTS && battle_script_enemy_spare(g) == 0;
    vars_peek(false);
    return ready;
}

#define EFFECT_GUARD(name, handler) \
    bool name(Snes *s) { return command_on_effect_ready(s, handler); }

/* ... and, for a color command, the other targets made of combatants on
 * every channel from the fourth on (an effect channel's colors change them),
 * its list from the fourth channel's subject (where the rotation looks for a
 * flashed enemy) made of combatants too, and
 * the background colors the rule cycles starting among the background's, as
 * the original's copies to their last one need. */
enum { ROTATION_LIST_START = 3, BACKGROUND_COLORS = 128 };

static bool command_on_colors_ready(Snes *s, uint16_t handler)
{
    if (!command_on_subjects_ready(s, handler))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ready = list_of_combatants(g, ROTATION_LIST_START, false) && list_of_combatants(g, OTHER_TARGETS_START, false)
                 && battle_background_cycle(g) < BACKGROUND_COLORS;
    vars_peek(false);
    return ready;
}

#define COLORS_GUARD(name, handler) \
    bool name(Snes *s) { return command_on_colors_ready(s, handler); }

COMMAND_GUARD(script_end_ready, 0x4CF0)
COMMAND_GUARD(script_halt_ready, 0x4CF7)
COMMAND_GUARD(script_call_ready, 0x56CC)
COMMAND_GUARD(script_return_ready, 0x56FB)
COMMAND_GUARD(script_wait_ready, 0x5714)
COMMAND_GUARD(script_return_unless_point_low_ready, 0x5722)
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
COMMAND_GUARD(script_swap_marks_ready, 0x5A99)
COMMAND_GUARD(script_swap_mark_a_with_point_x_ready, 0x5AAF)
COMMAND_GUARD(script_swap_mark_a_with_point_y_ready, 0x5AC5)
COMMAND_GUARD(script_skip_ready, 0x5D18)
SUBJECTS_GUARD(script_play_animation_ready, 0x4D04)
SUBJECTS_GUARD(script_play_shown_and_wait_ready, 0x4E22)
SUBJECTS_GUARD(script_play_and_wait_ready, 0x4EA2)
SUBJECTS_GUARD(script_own_speed_ready, 0x4F1A)
SUBJECTS_GUARD(script_speed_ready, 0x4F54)
SUBJECTS_GUARD(script_play_and_wait_marked_ready, 0x577D)
SUBJECTS_GUARD(script_drawn_on_top_ready, 0x5806)
SUBJECTS_GUARD(script_not_drawn_on_top_ready, 0x5840)
SUBJECTS_GUARD(script_hide_ready, 0x5844)
SUBJECTS_GUARD(script_show_ready, 0x5879)
SUBJECTS_GUARD(script_sprite_back_in_place_ready, 0x587D)
SUBJECTS_GUARD(script_bring_back_ready, 0x58AB)
SUBJECTS_GUARD(script_return_to_plane_ready, 0x5CF1)
SUBJECTS_GUARD(script_withdraw_ready, 0x5D12)
SUBJECTS_GUARD(script_trail_on_ready, 0x691B)
SUBJECTS_GUARD(script_trail_off_ready, 0x69BA)
SUBJECTS_GUARD(script_glide_sprite_ready, 0x6F9A)
SUBJECTS_GUARD(script_glide_sprite_home_ready, 0x6FEF)
SUBJECTS_GUARD(script_shadow_on_ready, 0x6FFC)
SUBJECTS_GUARD(script_shadow_off_ready, 0x702B)
SUBJECTS_GUARD(script_attribute_bits_ready, 0x753A)
SUBJECTS_GUARD(script_attribute_bits_back_ready, 0x7579)
SUBJECTS_GUARD(script_anchor_follows_ready, 0x5694)
COMMAND_GUARD(script_anchor_stays_ready, 0x56BB)
COMMAND_GUARD(script_pause_palettes_ready, 0x58DC)
COMMAND_GUARD(script_resume_palettes_ready, 0x58E4)
COMMAND_GUARD(script_palette_animation_ready, 0x5D93)
COMMAND_GUARD(script_palette_animation_off_ready, 0x5DC1)
COMMAND_GUARD(script_colour_cycles_off_ready, 0x5F9A)
COMMAND_GUARD(script_show_all_effects_ready, 0x5FA5)
COMMAND_GUARD(script_hide_all_effects_ready, 0x5FB3)
EFFECT_GUARD(script_show_effect_ready, 0x5FC1)
EFFECT_GUARD(script_hide_effect_ready, 0x5FD3)
EFFECT_GUARD(script_effect_in_front_ready, 0x6059)
EFFECT_GUARD(script_effect_behind_ready, 0x605F)
COMMAND_GUARD(script_palette_frame_ready, 0x5D1D)
COLORS_GUARD(script_flash_ready, 0x5DD3)
COLORS_GUARD(script_flash_off_ready, 0x5E87)
COLORS_GUARD(script_rotate_colors_ready, 0x5EDF)
COLORS_GUARD(script_blink_ready, 0x5F13)

void battle_script_command_entry(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t command = battle_script_command(g);
    const ScriptCommand *row = &commands[command];
    script_take_step(g, row->operation(g, script_channel_running(g, command), row->form));
    s->cpu.a = 0;
    m_rts(s);
}
